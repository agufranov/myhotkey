/* Executing what a rule fires. */
#include "common.h"
#include <shellapi.h>
#include <string.h>

/* Stamped on every event we inject, so the hook can tell our own synthetic
   input apart from that of other tools (which also set LLKHF_INJECTED). */
#define MH_SIGNATURE 0x4D484B59UL   /* 'MHKY' */

ULONG_PTR mh_signature(void) { return (ULONG_PTR)MH_SIGNATURE; }

static void send_key(UINT vk, int down) {
    INPUT in;
    memset(&in, 0, sizeof(in));
    in.type           = INPUT_KEYBOARD;
    in.ki.wVk         = (WORD)vk;
    in.ki.wScan       = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags     = (down ? 0 : KEYEVENTF_KEYUP)
                      | (mh_vk_is_extended(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
    in.ki.dwExtraInfo = (ULONG_PTR)MH_SIGNATURE;
    SendInput(1, &in, sizeof(in));
}

static void send_unicode(WCHAR ch) {
    INPUT in[2];
    int i;
    memset(in, 0, sizeof(in));
    for (i = 0; i < 2; i++) {
        in[i].type           = INPUT_KEYBOARD;
        in[i].ki.wScan       = ch;
        in[i].ki.dwFlags     = KEYEVENTF_UNICODE | (i ? KEYEVENTF_KEYUP : 0);
        in[i].ki.dwExtraInfo = (ULONG_PTR)MH_SIGNATURE;
    }
    SendInput(2, in, sizeof(in[0]));
}

/* Which physical key is actually down for a modifier the user is holding.
   Matters on release: they may be on the right-hand Ctrl, not the left. */
static void release_mod(UINT bit) {
    struct { UINT bit; UINT l, r; } m[] = {
        { MH_MOD_CTRL,  VK_LCONTROL, VK_RCONTROL },
        { MH_MOD_ALT,   VK_LMENU,    VK_RMENU    },
        { MH_MOD_SHIFT, VK_LSHIFT,   VK_RSHIFT   },
        { MH_MOD_WIN,   VK_LWIN,     VK_RWIN     },
    };
    int i;
    for (i = 0; i < 4; i++) {
        if (m[i].bit != bit) continue;
        if (GetAsyncKeyState((int)m[i].l) & 0x8000) send_key(m[i].l, 0);
        if (GetAsyncKeyState((int)m[i].r) & 0x8000) send_key(m[i].r, 0);
    }
}

static UINT press_vk_for_mod(UINT bit) {
    switch (bit) {
        case MH_MOD_CTRL:  return VK_LCONTROL;
        case MH_MOD_ALT:   return VK_LMENU;
        case MH_MOD_SHIFT: return VK_LSHIFT;
        case MH_MOD_WIN:   return VK_LWIN;
    }
    return 0;
}

static const UINT g_modBits[4] = { MH_MOD_CTRL, MH_MOD_ALT, MH_MOD_SHIFT, MH_MOD_WIN };

/* Send one chord, reconciling it against the modifiers the user physically
   holds: lift the ones the chord does not want, add the ones it does, then put
   the keyboard back exactly as it was. Without this, Alt+S -> Left would leak
   the held Alt into the target app and arrive as Alt+Left. */
void mh_send_chord(const MhChord *c, UINT heldMods) {
    UINT toRelease = heldMods & ~c->mods;
    UINT toPress   = c->mods  & ~heldMods;
    int i;

    for (i = 0; i < 4; i++) if (toRelease & g_modBits[i]) release_mod(g_modBits[i]);
    for (i = 0; i < 4; i++) if (toPress   & g_modBits[i]) send_key(press_vk_for_mod(g_modBits[i]), 1);

    if (c->vk) { send_key(c->vk, 1); send_key(c->vk, 0); }

    for (i = 3; i >= 0; i--) if (toPress   & g_modBits[i]) send_key(press_vk_for_mod(g_modBits[i]), 0);
    for (i = 3; i >= 0; i--) if (toRelease & g_modBits[i]) send_key(press_vk_for_mod(g_modBits[i]), 1);
}

/* Ask the focused window to change layout directly. More reliable than faking
   Ctrl+Shift, which depends on the user's layout-switch setting being that. */
static void switch_layout(int delta) {
    HWND fg = GetForegroundWindow(), target;
    DWORD me, other;
    if (!fg) return;
    me    = GetCurrentThreadId();
    other = GetWindowThreadProcessId(fg, NULL);
    target = fg;
    if (other != me && AttachThreadInput(me, other, TRUE)) {
        HWND f = GetFocus();
        if (f) target = f;
        AttachThreadInput(me, other, FALSE);
    }
    PostMessageW(target, WM_INPUTLANGCHANGEREQUEST,
                 delta < 0 ? INPUTLANGCHANGE_BACKWARD : INPUTLANGCHANGE_FORWARD,
                 (LPARAM)(delta < 0 ? HKL_PREV : HKL_NEXT));
}

void mh_action_run(const Action *a, UINT heldMods) {
    int i;
    switch (a->type) {
    case ACT_NONE:
        break;
    case ACT_COMBO:
        for (i = 0; i < a->stepCount; i++) mh_send_chord(&a->steps[i], heldMods);
        break;
    case ACT_TEXT: {
        const wchar_t *p;
        for (i = 0; i < 4; i++) if (heldMods & g_modBits[i]) release_mod(g_modBits[i]);
        for (p = a->text; p && *p; p++) send_unicode(*p);
        for (i = 3; i >= 0; i--) if (heldMods & g_modBits[i]) send_key(press_vk_for_mod(g_modBits[i]), 1);
        break;
    }
    case ACT_LAUNCH:
        if (a->path) ShellExecuteW(NULL, L"open", a->path, a->args, NULL, SW_SHOWNORMAL);
        break;
    case ACT_LAYOUT:
        switch_layout(a->layoutDelta);
        break;
    }
}
