/* Executing what a rule fires. */
#include "common.h"
#include <shellapi.h>
#include <string.h>

/* Stamped on every event we inject, so the hook can tell our own synthetic
   input apart from that of other tools (which also set LLKHF_INJECTED). */
#define MH_SIGNATURE 0x4D484B59UL

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

/* Lift every modifier the user is physically holding, recording what was
   lifted so it can be put back. */
static int lift_all(UINT held, UINT *lifted) {
    const MhModGroup *g = mh_mod_groups();
    int i, n = 0;
    for (i = 0; i < MH_GROUP_COUNT; i++) {
        if (held & g[i].heldL) { send_key(g[i].vkL, 0); lifted[n++] = g[i].vkL; }
        if (held & g[i].heldR) { send_key(g[i].vkR, 0); lifted[n++] = g[i].vkR; }
    }
    return n;
}

static void restore(const UINT *keys, int n, int down) {
    int i;
    for (i = n - 1; i >= 0; i--) send_key(keys[i], down);
}

/* Send one chord, reconciled against the modifiers the user physically holds:
   lift the ones the chord does not want, add the ones it does, then put the
   keyboard back exactly as it was.

   Without this, a rule mapping Alt+S to plain Left would leak the still-held
   Alt into the target app, which would see Alt+Left instead. */
void mh_send_chord(const MhChord *c, UINT held) {
    const MhModGroup *g = mh_mod_groups();
    UINT lifted[8], pressed[8];
    int nLifted = 0, nPressed = 0;
    int i;

    for (i = 0; i < MH_GROUP_COUNT; i++) {
        int heldL = (held & g[i].heldL) != 0;
        int heldR = (held & g[i].heldR) != 0;

        if (!(c->mods & g[i].groupBit)) {
            if (heldL) { send_key(g[i].vkL, 0); lifted[nLifted++] = g[i].vkL; }
            if (heldR) { send_key(g[i].vkR, 0); lifted[nLifted++] = g[i].vkR; }
            continue;
        }
        switch ((int)MH_SIDE_GET(c->sides, i)) {
        case MH_SIDE_LEFT:
            if (!heldL) { send_key(g[i].vkL, 1); pressed[nPressed++] = g[i].vkL; }
            break;
        case MH_SIDE_RIGHT:
            if (!heldR) { send_key(g[i].vkR, 1); pressed[nPressed++] = g[i].vkR; }
            break;
        default:
            /* Either side satisfies it, so reuse whatever is already down. */
            if (!heldL && !heldR) { send_key(g[i].vkL, 1); pressed[nPressed++] = g[i].vkL; }
            break;
        }
    }

    if (c->vk) { send_key(c->vk, 1); send_key(c->vk, 0); }

    restore(pressed, nPressed, 0);
    restore(lifted,  nLifted,  1);
}

/* Ask the focused window to change layout directly. More reliable than faking
   Ctrl+Shift, which only works if that is the user's layout-switch setting. */
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

void mh_action_run(const Action *a, UINT held) {
    int i;
    switch (a->type) {
    case ACT_NONE:
        break;
    case ACT_COMBO:
        for (i = 0; i < a->stepCount; i++) mh_send_chord(&a->steps[i], held);
        break;
    case ACT_TEXT: {
        UINT lifted[8];
        int n;
        const wchar_t *p;
        /* A held Ctrl would turn every letter into a shortcut, so the text
           has to be typed on a clean keyboard. */
        n = lift_all(held, lifted);
        for (p = a->text; p && *p; p++) send_unicode(*p);
        restore(lifted, n, 1);
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
