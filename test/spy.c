/* Prints every key event the low-level hook receives, and which rule of the
 * live config would fire on it.
 *
 * Exists because guessing what a key arrives as, or why a rule did not match,
 * is how you write a matching bug. This mirrors the decision the engine makes
 * in hook.c, so a disagreement between the two is itself the finding.
 *
 * Usage: spy.exe [seconds]   (default 5)
 */
#include "../engine/common.h"
#include <stdio.h>
#include <stdlib.h>

static HHOOK   g_hook;
static RuleSet g_rules;
static UINT    g_held;

static int find_rule(UINT vk) {
    int i;
    for (i = 0; i < g_rules.count; i++) {
        const Rule *r = &g_rules.rules[i];
        if (!r->enabled) continue;
        if (!mh_chord_key_matches(&r->trigger, vk)) continue;
        if (!mh_chord_matches(&r->trigger, g_held)) continue;
        return i;
    }
    return -1;
}

static LRESULT CALLBACK proc(int code, WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lParam;
    UINT vk, heldBit;
    int down, rule;
    const char *name;

    if (code != HC_ACTION) return CallNextHookEx(g_hook, code, wParam, lParam);

    /* Mirror the engine: the AltGr phantom Ctrl is reported but never counted. */
    if (k->vkCode == VK_LCONTROL && k->scanCode == 0x21D) {
        printf("%-4s vk=0xA2 scan=0x21D  <- AltGr phantom, ignored\n",
               (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) ? "DOWN" : "UP");
        fflush(stdout);
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    vk      = k->vkCode;
    down    = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    heldBit = mh_held_bit_for_vk(vk);
    name    = mh_name_from_vk(vk);

    printf("%-4s vk=0x%02X scan=0x%02X t=%lu %s%s%-10s held=0x%02X",
           down ? "DOWN" : "UP", (unsigned)vk, (unsigned)k->scanCode,
           (unsigned long)k->time,
           (k->flags & LLKHF_EXTENDED) ? "ext " : "    ",
           (k->flags & LLKHF_INJECTED) ? "inj " : "    ",
           name ? name : "?", (unsigned)g_held);

    if (down) {
        rule = find_rule(vk);
        if (rule >= 0) {
            char trigger[64];
            mh_format_chord(&g_rules.rules[rule].trigger, trigger, sizeof(trigger));
            printf("   -> rule %d (%s)", rule, trigger);
        } else {
            printf("   -> no rule");
        }
    }
    printf("\n");
    fflush(stdout);

    if (heldBit) { if (down) g_held |= heldBit; else g_held &= ~heldBit; }
    return CallNextHookEx(g_hook, code, wParam, lParam);
}

int main(int argc, char **argv) {
    int seconds = (argc > 1) ? atoi(argv[1]) : 5;
    wchar_t path[MAX_PATH];
    char err[256];
    DWORD until;
    MSG msg;

    if (mh_config_default_path(path, MAX_PATH)) {
        if (mh_config_load(path, &g_rules, err, sizeof(err)))
            printf("config: %d rules\n", g_rules.count);
        else
            printf("config did not load: %s\n", err);
    }

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, proc, GetModuleHandleW(NULL), 0);
    if (!g_hook) { printf("could not install the hook\n"); return 1; }

    printf("listening for %d seconds\n", seconds);
    fflush(stdout);

    until = GetTickCount() + (DWORD)seconds * 1000;
    while (GetTickCount() < until) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
    }
    UnhookWindowsHookEx(g_hook);
    printf("done\n");
    return 0;
}
