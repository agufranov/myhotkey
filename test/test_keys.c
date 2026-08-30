/* Checks for chord parsing, round-trip formatting and modifier matching.
   Build: see test\run.bat */
#include "../engine/common.h"
#include <stdio.h>
#include <string.h>

static int g_fail;
static int g_ran;

static void fail(const char *what, const char *detail) {
    printf("FAIL  %-22s %s\n", what, detail);
    g_fail++;
}

/* Parses, checks the fields, then checks it formats back to canonical text. */
static void chord(const char *text, UINT vk, UINT mods, const char *canonical) {
    MhChord c;
    char back[64], detail[160];

    g_ran++;
    if (!mh_parse_chord(text, &c)) { fail(text, "did not parse"); return; }
    if (c.vk != vk || c.mods != mods) {
        sprintf(detail, "got vk=0x%02X mods=%u, wanted vk=0x%02X mods=%u",
                c.vk, c.mods, vk, mods);
        fail(text, detail);
        return;
    }
    if (!mh_format_chord(&c, back, sizeof(back))) { fail(text, "did not format"); return; }
    if (strcmp(back, canonical) != 0) {
        sprintf(detail, "formatted as \"%s\", wanted \"%s\"", back, canonical);
        fail(text, detail);
        return;
    }
    printf("ok    %-22s -> %s\n", text, back);
}

static void rejected(const char *text) {
    MhChord c;
    g_ran++;
    if (mh_parse_chord(text, &c)) { fail(text, "parsed but should not have"); return; }
    printf("ok    %-22s rejected\n", text);
}

/* Does this chord accept that physical key as its trigger? */
static void key(const char *text, UINT eventVk, int want) {
    MhChord c;
    char detail[96];
    g_ran++;
    if (!mh_parse_chord(text, &c)) { fail(text, "did not parse"); return; }
    if (mh_chord_key_matches(&c, eventVk) != want) {
        sprintf(detail, "vk 0x%02X: %s", eventVk,
                want ? "should have been accepted" : "should have been refused");
        fail(text, detail);
        return;
    }
    printf("ok    %-22s %s vk=0x%02X\n", text,
           want ? "accepts   " : "refuses   ", eventVk);
}

/* Does this chord fire while exactly these modifier keys are held down? */
static void match(const char *text, UINT held, int want) {
    MhChord c;
    g_ran++;
    if (!mh_parse_chord(text, &c)) { fail(text, "did not parse"); return; }
    if (mh_chord_matches(&c, held) != want) {
        fail(text, want ? "should have matched but did not"
                        : "should not have matched but did");
        return;
    }
    printf("ok    %-22s %s held=0x%02X\n", text,
           want ? "fires on  " : "ignores   ", held);
}

int main(void) {
    printf("--- parsing and round trip ---\n");

    /* the two rules this whole project started from */
    chord("CapsLock",   VK_CAPITAL, 0,           "CapsLock");
    chord("Alt+S",      'S',        MH_MOD_ALT,  "Alt+S");
    chord("Alt+Left",   VK_LEFT,    MH_MOD_ALT,  "Alt+Left");

    /* modifiers only - the layout-switch combo, valid as an action */
    chord("Ctrl+Shift", 0, MH_MOD_CTRL | MH_MOD_SHIFT, "Ctrl+Shift");

    /* case and padding must not matter */
    chord("alt+s",   'S', MH_MOD_ALT, "Alt+S");
    chord("ALT + S", 'S', MH_MOD_ALT, "Alt+S");
    chord("caps",    VK_CAPITAL, 0,   "CapsLock");

    chord("Ctrl+Alt+Shift+Win+F5", VK_F5,
          MH_MOD_CTRL | MH_MOD_ALT | MH_MOD_SHIFT | MH_MOD_WIN,
          "Ctrl+Alt+Shift+Win+F5");

    chord("Ctrl+Comma", VK_OEM_COMMA, MH_MOD_CTRL, "Ctrl+Comma");
    chord("F12",     VK_F12,     0, "F12");
    chord("Numpad7", VK_NUMPAD7, 0, "Numpad7");

    printf("\n--- a lone modifier is the trigger key itself ---\n");
    chord("RAlt",  VK_RMENU,    0, "RAlt");
    chord("LCtrl", VK_LCONTROL, 0, "LCtrl");
    chord("Ctrl",  VK_CONTROL,  0, "Ctrl");

    printf("\n--- side-specific modifiers inside a chord ---\n");
    chord("LCtrl+S", 'S', MH_MOD_CTRL, "LCtrl+S");
    chord("RCtrl+S", 'S', MH_MOD_CTRL, "RCtrl+S");
    chord("RAlt+S",  'S', MH_MOD_ALT,  "RAlt+S");

    printf("\n--- nonsense must be rejected, not silently ignored ---\n");
    rejected("");
    rejected("NotAKey");
    rejected("Alt+NotAKey");
    rejected("A+B");            /* two real keys in one chord */

    printf("\n--- matching against held keys ---\n");
    match("Alt+S", MH_HELD_LALT, 1);
    match("Alt+S", MH_HELD_RALT, 1);          /* "Alt" means either side */
    match("Alt+S", 0, 0);                     /* nothing held */
    match("Alt+S", MH_HELD_LALT | MH_HELD_LCTRL, 0);  /* exact: Ctrl+Alt+S is not Alt+S */

    match("LCtrl+S", MH_HELD_LCTRL, 1);
    match("LCtrl+S", MH_HELD_RCTRL, 0);       /* the point of the whole feature */
    match("RCtrl+S", MH_HELD_RCTRL, 1);
    match("RCtrl+S", MH_HELD_LCTRL, 0);

    match("CapsLock", 0, 1);
    match("CapsLock", MH_HELD_LSHIFT, 0);     /* Shift+CapsLock is a different chord */

    /* A rule on a modifier key must still fire while that key is what is
       held - its own group is excluded from the exactness check. */
    match("RAlt", MH_HELD_RALT, 1);
    match("RAlt", MH_HELD_RALT | MH_HELD_LCTRL, 0);

    printf("\n--- which physical key the trigger accepts ---\n");
    key("RAlt",  VK_RMENU,    1);
    key("RAlt",  VK_LMENU,    0);   /* the lone-modifier side must survive */
    key("LAlt",  VK_LMENU,    1);
    key("LAlt",  VK_RMENU,    0);
    key("Alt",   VK_LMENU,    1);   /* generic name accepts either side */
    key("Alt",   VK_RMENU,    1);
    key("LCtrl", VK_LCONTROL, 1);
    key("LCtrl", VK_RCONTROL, 0);
    key("Ctrl",  VK_RCONTROL, 1);
    key("Alt+S", 'S',         1);
    key("Alt+S", 'D',         0);

    printf("\n%d checks, %s\n", g_ran,
           g_fail ? "FAILURES PRESENT" : "all passed");
    return g_fail ? 1 : 0;
}
