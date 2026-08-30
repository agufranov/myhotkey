/* Round-trip and validation checks for the chord parser.
   Build: see test\run.bat */
#include "../engine/common.h"
#include <stdio.h>
#include <string.h>

static int g_fail;

static void expect_ok(const char *text, UINT vk, UINT mods) {
    MhChord c;
    char back[64];
    if (!mh_parse_chord(text, &c)) {
        printf("FAIL  %-16s did not parse\n", text);
        g_fail++;
        return;
    }
    if (c.vk != vk || c.mods != mods) {
        printf("FAIL  %-16s got vk=0x%02X mods=%u, wanted vk=0x%02X mods=%u\n",
               text, c.vk, c.mods, vk, mods);
        g_fail++;
        return;
    }
    mh_format_chord(&c, back, sizeof(back));
    printf("ok    %-16s -> vk=0x%02X mods=%u -> \"%s\"\n", text, c.vk, c.mods, back);
}

static void expect_bad(const char *text) {
    MhChord c;
    if (mh_parse_chord(text, &c)) {
        printf("FAIL  %-16s parsed but should not have\n", text);
        g_fail++;
        return;
    }
    printf("ok    %-16s rejected\n", text);
}

int main(void) {
    /* the two rules this whole project started from */
    expect_ok("CapsLock", VK_CAPITAL, 0);
    expect_ok("Alt+S",    'S',        MH_MOD_ALT);
    expect_ok("Alt+Left", VK_LEFT,    MH_MOD_ALT);

    /* modifiers-only, the layout-switch combo */
    expect_ok("Ctrl+Shift", 0, MH_MOD_CTRL | MH_MOD_SHIFT);

    /* case and spacing should not matter */
    expect_ok("alt+s",      'S', MH_MOD_ALT);
    expect_ok("ALT + S",    'S', MH_MOD_ALT);
    expect_ok("caps",       VK_CAPITAL, 0);

    /* every modifier at once */
    expect_ok("Ctrl+Alt+Shift+Win+F5", VK_F4 + 1,
              MH_MOD_CTRL | MH_MOD_ALT | MH_MOD_SHIFT | MH_MOD_WIN);

    /* punctuation is spelled out so '+' stays unambiguous */
    expect_ok("Ctrl+Comma",  VK_OEM_COMMA, MH_MOD_CTRL);
    expect_ok("Ctrl+Minus",  VK_OEM_MINUS, MH_MOD_CTRL);

    /* function keys and the numpad */
    expect_ok("F12",     VK_F12,    0);
    expect_ok("Numpad7", VK_NUMPAD7, 0);

    /* nonsense must be rejected, not silently ignored */
    expect_bad("");
    expect_bad("NotAKey");
    expect_bad("Alt+NotAKey");
    expect_bad("A+B");            /* two real keys in one chord */

    printf("\n%s\n", g_fail ? "FAILURES PRESENT" : "all checks passed");
    return g_fail ? 1 : 0;
}
