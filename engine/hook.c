/* The low-level keyboard hook.
 *
 * Rule: this callback must stay fast and allocation-free. If it takes longer
 * than LowLevelHooksTimeout (250 ms by default) Windows silently evicts the
 * hook and every remap stops working with no error anywhere. So matching is
 * table lookups only, and the actual work is posted to the engine window. */
#include "common.h"
#include <string.h>

#define MH_SIGNATURE 0x4D484B59UL

/* On layouts that have AltGr - Russian among them - Windows fabricates a left
   Ctrl press immediately in front of every right Alt, so applications that
   predate AltGr still see Ctrl+Alt. It gives that phantom the scan code 0x21D,
   which no physical key can produce, so it can be told apart with certainty.
   Left untreated it breaks every rule involving the right Alt: the rule asks
   for no other modifier, and Windows has just claimed Ctrl is down. */
#define MH_ALTGR_PHANTOM_CTRL 0x21D

static HHOOK         g_hook;
static HWINEVENTHOOK g_winEvent;
static RuleSet      *g_rules;
static HWND          g_target;
static volatile LONG g_enabled = 1;
static volatile LONG g_capture;

static UINT    g_held;                 /* MH_HELD_* bits */
static short   g_swallowed[256];       /* vk -> rule index, or -1 */
static wchar_t g_fgExe[64];            /* cached foreground process name */

/* ------------------------------------------------------- foreground app --- */

static void refresh_foreground(HWND hwnd) {
    DWORD pid = 0;
    HANDLE proc;
    wchar_t path[MAX_PATH];
    DWORD len = MAX_PATH;
    const wchar_t *base;

    g_fgExe[0] = 0;
    if (!hwnd) return;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return;
    proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return;
    if (QueryFullProcessImageNameW(proc, 0, path, &len)) {
        base = wcsrchr(path, L'\\');
        base = base ? base + 1 : path;
        wcsncpy(g_fgExe, base, 63);
        g_fgExe[63] = 0;
    }
    CloseHandle(proc);
}

static void CALLBACK on_foreground(HWINEVENTHOOK h, DWORD ev, HWND hwnd,
                                   LONG idObj, LONG idChild, DWORD tid, DWORD t) {
    (void)h; (void)ev; (void)idChild; (void)tid; (void)t;
    if (idObj == OBJID_WINDOW) refresh_foreground(hwnd);
}

/* ------------------------------------------------------------- matching --- */

/* Read the real keyboard state back into g_held. Needed after the hook has
   been off for an instant, because any release during that window was missed
   and the modifier would otherwise stay stuck down forever. */
static void resync_held(void) {
    const MhModGroup *g = mh_mod_groups();
    int i;
    g_held = 0;
    for (i = 0; i < MH_GROUP_COUNT; i++) {
        if (GetAsyncKeyState((int)g[i].vkL) & 0x8000) g_held |= g[i].heldL;
        if (GetAsyncKeyState((int)g[i].vkR) & 0x8000) g_held |= g[i].heldR;
    }
}

static int app_matches(const Rule *r) {
    if (!r->whenApp) return 1;
    if (_wcsicmp(r->whenApp, g_fgExe) == 0) return !r->whenAppIsExclude;
    return r->whenAppIsExclude;
}

static int find_rule(UINT vk) {
    int i;
    if (!g_rules) return -1;
    for (i = 0; i < g_rules->count; i++) {
        const Rule *r = &g_rules->rules[i];
        if (!r->enabled) continue;
        if (!mh_chord_key_matches(&r->trigger, vk)) continue;
        if (!mh_chord_matches(&r->trigger, g_held)) continue;
        if (!app_matches(r)) continue;
        return i;
    }
    return -1;
}

/* ---------------------------------------------------------------- hook --- */

static LRESULT CALLBACK keyboard_proc(int code, WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lParam;
    UINT vk, heldBit;
    int down, rule;

    if (code != HC_ACTION) return CallNextHookEx(g_hook, code, wParam, lParam);

    /* Never react to our own synthetic input, or we loop forever. Other tools'
       injected input is left alone deliberately - remapping a remap is valid. */
    if (k->dwExtraInfo == (ULONG_PTR)MH_SIGNATURE)
        return CallNextHookEx(g_hook, code, wParam, lParam);

    vk      = k->vkCode;
    down    = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    heldBit = mh_held_bit_for_vk(vk);

    /* Pass the AltGr phantom straight through - applications still need it to
       produce the right characters - but never let it count as a held key. */
    if (vk == VK_LCONTROL && k->scanCode == MH_ALTGR_PHANTOM_CTRL)
        return CallNextHookEx(g_hook, code, wParam, lParam);

    /* Capture mode: the UI is recording a shortcut. Swallow everything so the
       browser, the shell and the focused app never see it, and hand the key to
       the UI. This is why the recorder can catch Alt+Tab and Win+R. */
    if (InterlockedCompareExchange(&g_capture, 0, 0)) {
        if (down && g_target)
            PostMessageW(g_target, MH_WM_CAPTURED, vk, (LPARAM)g_held);
        if (heldBit) { if (down) g_held |= heldBit; else g_held &= ~heldBit; }
        return 1;
    }

    if (!InterlockedCompareExchange(&g_enabled, 0, 0)) {
        if (heldBit) { if (down) g_held |= heldBit; else g_held &= ~heldBit; }
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    if (down) {
        /* Auto-repeat of an already swallowed key: keep swallowing it. */
        if (g_swallowed[vk & 0xFF] >= 0) return 1;

        rule = find_rule(vk);
        if (rule >= 0) {
            g_swallowed[vk & 0xFF] = (short)rule;
            if (!g_rules->rules[rule].onRelease && g_target)
                PostMessageW(g_target, MH_WM_RUN_ACTION, (WPARAM)rule, (LPARAM)g_held);
            /* Deliberately not recorded in g_held: a modifier consumed by a
               rule is that rule's trigger, not a modifier for the next key. */
            return 1;
        }
        if (heldBit) g_held |= heldBit;
    } else {
        rule = g_swallowed[vk & 0xFF];
        if (rule >= 0) {
            g_swallowed[vk & 0xFF] = -1;
            if (g_rules && rule < g_rules->count &&
                g_rules->rules[rule].onRelease && g_target)
                PostMessageW(g_target, MH_WM_RUN_ACTION, (WPARAM)rule, (LPARAM)g_held);
            return 1;   /* swallow the key-up too, so the app sees no orphan */
        }
        if (heldBit) g_held &= ~heldBit;
    }

    return CallNextHookEx(g_hook, code, wParam, lParam);
}

/* --------------------------------------------------------------- setup --- */

int mh_hook_install(void) {
    int i;
    for (i = 0; i < 256; i++) g_swallowed[i] = -1;
    resync_held();

    if (!g_hook)
        g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, GetModuleHandleW(NULL), 0);
    if (!g_winEvent)
        g_winEvent = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                     NULL, on_foreground, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    refresh_foreground(GetForegroundWindow());
    return g_hook != NULL;
}

void mh_hook_remove(void) {
    if (g_hook)     { UnhookWindowsHookEx(g_hook); g_hook = NULL; }
    if (g_winEvent) { UnhookWinEvent(g_winEvent);  g_winEvent = NULL; }
}

/* Windows evicts a slow hook silently and there is no way to be told. Cheap
   insurance: tear it down and put it back on a timer. Costs microseconds. */
int mh_hook_refresh(void) {
    if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = NULL; }
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, GetModuleHandleW(NULL), 0);
    resync_held();
    return g_hook != NULL;
}

int  mh_hook_alive(void)           { return g_hook != NULL; }
void mh_hook_set_target(HWND hwnd) { g_target = hwnd; }
void mh_hook_set_enabled(int e)    { InterlockedExchange(&g_enabled, e ? 1 : 0); }
int  mh_hook_get_enabled(void)     { return (int)InterlockedCompareExchange(&g_enabled, 0, 0); }
void mh_hook_set_capture(int on)   { InterlockedExchange(&g_capture, on ? 1 : 0); }

/* Swapping the rule set invalidates every index in g_swallowed. Without this,
   a key held across a config reload would fire whatever rule now happens to
   sit at the old index. */
void mh_hook_set_rules(RuleSet *rs) {
    int i;
    g_rules = rs;
    for (i = 0; i < 256; i++) g_swallowed[i] = -1;
}
