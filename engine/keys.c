/* Key names and chord parsing. Shared vocabulary between config.json, the UI
   and the hook.

   Two things make this less trivial than a lookup table. A modifier can be
   either part of a chord ("Alt+S") or the trigger itself ("RAlt"), and the
   difference is positional, not lexical. And a modifier name carries a side:
   "Ctrl" means either one, "LCtrl" means that specific key. */
#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static const MhModGroup g_groups[MH_GROUP_COUNT] = {
    { MH_MOD_CTRL,  MH_HELD_LCTRL,  MH_HELD_RCTRL,  VK_LCONTROL, VK_RCONTROL, "Ctrl"  },
    { MH_MOD_ALT,   MH_HELD_LALT,   MH_HELD_RALT,   VK_LMENU,    VK_RMENU,    "Alt"   },
    { MH_MOD_SHIFT, MH_HELD_LSHIFT, MH_HELD_RSHIFT, VK_LSHIFT,   VK_RSHIFT,   "Shift" },
    { MH_MOD_WIN,   MH_HELD_LWIN,   MH_HELD_RWIN,   VK_LWIN,     VK_RWIN,     "Win"   }
};

const MhModGroup *mh_mod_groups(void) { return g_groups; }

/* The side lives in the table rather than being derived from the VK, because
   "Win" and "LWin" have to resolve to the same VK yet mean different things. */
typedef struct { const char *name; UINT vk; int side; } KeyName;

/* The first entry for a VK is the canonical name used when formatting back to
   text. Later entries are accepted aliases. */
static const KeyName g_keys[] = {
    {"Ctrl",VK_CONTROL,MH_SIDE_ANY},   {"Control",VK_CONTROL,MH_SIDE_ANY},
    {"Alt",VK_MENU,MH_SIDE_ANY},       {"Menu",VK_MENU,MH_SIDE_ANY},
    {"Shift",VK_SHIFT,MH_SIDE_ANY},
    {"Win",VK_LWIN,MH_SIDE_ANY},
    {"LCtrl",VK_LCONTROL,MH_SIDE_LEFT},   {"RCtrl",VK_RCONTROL,MH_SIDE_RIGHT},
    {"LAlt",VK_LMENU,MH_SIDE_LEFT},       {"RAlt",VK_RMENU,MH_SIDE_RIGHT},
    {"LShift",VK_LSHIFT,MH_SIDE_LEFT},    {"RShift",VK_RSHIFT,MH_SIDE_RIGHT},
    {"LWin",VK_LWIN,MH_SIDE_LEFT},        {"RWin",VK_RWIN,MH_SIDE_RIGHT},

    {"CapsLock",VK_CAPITAL,0},{"Caps",VK_CAPITAL,0},
    {"NumLock",VK_NUMLOCK,0},{"ScrollLock",VK_SCROLL,0},
    {"Tab",VK_TAB,0},{"Enter",VK_RETURN,0},{"Return",VK_RETURN,0},
    {"Esc",VK_ESCAPE,0},{"Escape",VK_ESCAPE,0},
    {"Space",VK_SPACE,0},{"Backspace",VK_BACK,0},{"BS",VK_BACK,0},
    {"Delete",VK_DELETE,0},{"Del",VK_DELETE,0},
    {"Insert",VK_INSERT,0},{"Ins",VK_INSERT,0},
    {"Home",VK_HOME,0},{"End",VK_END,0},
    {"PageUp",VK_PRIOR,0},{"PgUp",VK_PRIOR,0},
    {"PageDown",VK_NEXT,0},{"PgDn",VK_NEXT,0},
    {"Left",VK_LEFT,0},{"Right",VK_RIGHT,0},{"Up",VK_UP,0},{"Down",VK_DOWN,0},
    {"PrintScreen",VK_SNAPSHOT,0},{"Pause",VK_PAUSE,0},{"Apps",VK_APPS,0},

    /* punctuation - names, not glyphs, so '+' never collides with the separator */
    {"Minus",VK_OEM_MINUS,0},{"Equals",VK_OEM_PLUS,0},
    {"LBracket",VK_OEM_4,0},{"RBracket",VK_OEM_6,0},{"Backslash",VK_OEM_5,0},
    {"Semicolon",VK_OEM_1,0},{"Quote",VK_OEM_7,0},{"Backtick",VK_OEM_3,0},
    {"Comma",VK_OEM_COMMA,0},{"Period",VK_OEM_PERIOD,0},{"Slash",VK_OEM_2,0},

    {"Numpad0",VK_NUMPAD0,0},{"Numpad1",VK_NUMPAD1,0},{"Numpad2",VK_NUMPAD2,0},
    {"Numpad3",VK_NUMPAD3,0},{"Numpad4",VK_NUMPAD4,0},{"Numpad5",VK_NUMPAD5,0},
    {"Numpad6",VK_NUMPAD6,0},{"Numpad7",VK_NUMPAD7,0},{"Numpad8",VK_NUMPAD8,0},
    {"Numpad9",VK_NUMPAD9,0},
    {"NumpadMultiply",VK_MULTIPLY,0},{"NumpadAdd",VK_ADD,0},
    {"NumpadSubtract",VK_SUBTRACT,0},{"NumpadDecimal",VK_DECIMAL,0},
    {"NumpadDivide",VK_DIVIDE,0},

    {"VolumeUp",VK_VOLUME_UP,0},{"VolumeDown",VK_VOLUME_DOWN,0},
    {"VolumeMute",VK_VOLUME_MUTE,0},
    {"MediaNext",VK_MEDIA_NEXT_TRACK,0},{"MediaPrev",VK_MEDIA_PREV_TRACK,0},
    {"MediaStop",VK_MEDIA_STOP,0},{"MediaPlayPause",VK_MEDIA_PLAY_PAUSE,0},
    {"BrowserBack",VK_BROWSER_BACK,0},{"BrowserForward",VK_BROWSER_FORWARD,0}
};

#define KEY_COUNT (sizeof(g_keys) / sizeof(g_keys[0]))

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

/* Resolve a single token. Returns 0 if the name is unknown. */
static int lookup(const char *name, UINT *vk, int *side) {
    size_t i, n;

    *side = MH_SIDE_ANY;
    if (!name || !*name) return 0;
    n = strlen(name);

    if (n == 1) {
        char c = (char)toupper((unsigned char)name[0]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { *vk = (UINT)c; return 1; }
        if (c == '+') { *vk = VK_OEM_PLUS; return 1; }
    }
    if ((name[0] == 'F' || name[0] == 'f') && n >= 2 && n <= 3) {
        int num = atoi(name + 1);
        if (num >= 1 && num <= 24) { *vk = VK_F1 + (UINT)num - 1; return 1; }
    }
    for (i = 0; i < KEY_COUNT; i++) {
        if (ieq(g_keys[i].name, name)) {
            *vk   = g_keys[i].vk;
            *side = g_keys[i].side;
            return 1;
        }
    }
    return 0;
}

UINT mh_vk_from_name(const char *name) {
    UINT vk;
    int side;
    return lookup(name, &vk, &side) ? vk : 0;
}

const char *mh_name_from_vk(UINT vk) {
    static char buf[8];
    size_t i;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        buf[0] = (char)vk; buf[1] = 0; return buf;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        sprintf(buf, "F%u", vk - VK_F1 + 1); return buf;
    }
    for (i = 0; i < KEY_COUNT; i++)
        if (g_keys[i].vk == vk) return g_keys[i].name;
    return NULL;
}

UINT mh_mod_bit_for_vk(UINT vk) {
    switch (vk) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return MH_MOD_CTRL;
        case VK_MENU:    case VK_LMENU:    case VK_RMENU:    return MH_MOD_ALT;
        case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:   return MH_MOD_SHIFT;
        case VK_LWIN:    case VK_RWIN:                       return MH_MOD_WIN;
    }
    return 0;
}

/* Which held-bit a physical key press sets. The low-level hook always reports
   the side-specific VK, but map the generic ones to the left key anyway so a
   synthesised event does not go untracked. */
UINT mh_held_bit_for_vk(UINT vk) {
    switch (vk) {
        case VK_LCONTROL: case VK_CONTROL: return MH_HELD_LCTRL;
        case VK_RCONTROL:                  return MH_HELD_RCTRL;
        case VK_LMENU:    case VK_MENU:    return MH_HELD_LALT;
        case VK_RMENU:                     return MH_HELD_RALT;
        case VK_LSHIFT:   case VK_SHIFT:   return MH_HELD_LSHIFT;
        case VK_RSHIFT:                    return MH_HELD_RSHIFT;
        case VK_LWIN:                      return MH_HELD_LWIN;
        case VK_RWIN:                      return MH_HELD_RWIN;
    }
    return 0;
}

/* Keys on the extended part of the scancode set. SendInput needs the flag or
   apps that read scancodes (games, terminals, RDP) see the wrong key. */
int mh_vk_is_extended(UINT vk) {
    switch (vk) {
        case VK_RCONTROL: case VK_RMENU:
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR:  case VK_NEXT:
        case VK_LEFT:   case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_NUMLOCK: case VK_DIVIDE: case VK_SNAPSHOT:
        case VK_LWIN:   case VK_RWIN:  case VK_APPS:
            return 1;
    }
    return 0;
}

/* "Alt+S", "Ctrl+Shift", "LCtrl+S", "RAlt", "CapsLock" -> MhChord.

   A lone modifier is the trigger key itself; a modifier followed by anything
   else is part of the chord. That positional rule is what lets "RAlt" and
   "RAlt+S" both mean the obvious thing. */
int mh_parse_chord(const char *text, MhChord *out) {
    char tok[8][32];
    UINT vks[8];
    int  sides[8];
    int  count = 0, i, mainIndex = -1;
    size_t ti = 0;
    const char *p;

    if (!text || !out) return 0;
    out->vk = 0; out->mods = 0; out->sides = 0;

    /* split on '+', tolerating padding and a trailing literal plus key */
    for (p = text; ; p++) {
        if (*p && *p != '+') {
            if (ti == 0 && *p == ' ') continue;
            if (ti < sizeof(tok[0]) - 1) tok[count][ti++] = *p;
            continue;
        }
        if (*p == '+' && ti == 0 && p[1] == '\0') { tok[count][0] = '+'; ti = 1; }
        tok[count][ti] = 0;
        while (ti > 0 && tok[count][ti - 1] == ' ') tok[count][--ti] = 0;

        if (ti > 0) {
            if (!lookup(tok[count], &vks[count], &sides[count])) return 0;
            if (++count == 8) return 0;
        }
        ti = 0;
        if (!*p) break;
    }
    if (count == 0) return 0;

    /* A single token is always the key itself, modifier or not. */
    if (count == 1) {
        out->vk = vks[0];
        return 1;
    }

    for (i = 0; i < count; i++) {
        UINT group = mh_mod_bit_for_vk(vks[i]);
        if (group) {
            int g;
            for (g = 0; g < MH_GROUP_COUNT; g++) {
                if (g_groups[g].groupBit != group) continue;
                out->mods |= group;
                out->sides = MH_SIDE_PUT(out->sides, g, sides[i]);
            }
        } else {
            if (mainIndex >= 0) return 0;   /* two real keys in one chord */
            mainIndex = i;
        }
    }
    if (mainIndex >= 0) out->vk = vks[mainIndex];
    return 1;
}

int mh_format_chord(const MhChord *c, char *buf, size_t cap) {
    const char *kn;
    int g;

    buf[0] = 0;
    for (g = 0; g < MH_GROUP_COUNT; g++) {
        char part[16];
        int side;
        if (!(c->mods & g_groups[g].groupBit)) continue;
        side = (int)MH_SIDE_GET(c->sides, g);
        sprintf(part, "%s%s+",
                side == MH_SIDE_LEFT ? "L" : side == MH_SIDE_RIGHT ? "R" : "",
                g_groups[g].name);
        strncat(buf, part, cap - strlen(buf) - 1);
    }
    if (c->vk) {
        kn = mh_name_from_vk(c->vk);
        if (!kn) return 0;
        strncat(buf, kn, cap - strlen(buf) - 1);
    } else {
        size_t n = strlen(buf);          /* drop the dangling '+' */
        if (n) buf[n - 1] = 0;
    }
    return 1;
}

/* Does the currently held set of modifier keys satisfy this chord?

   Matching is exact: a modifier the chord does not ask for must not be down,
   so Alt+S never fires on Ctrl+Alt+S. The one exception is the chord's own
   trigger key - a rule on RAlt must still match while RAlt is what is held. */
int mh_chord_matches(const MhChord *c, UINT held) {
    UINT selfGroup = mh_mod_bit_for_vk(c->vk);
    int g;

    for (g = 0; g < MH_GROUP_COUNT; g++) {
        int l, r, want;
        if (g_groups[g].groupBit == selfGroup) continue;

        l    = (held & g_groups[g].heldL) != 0;
        r    = (held & g_groups[g].heldR) != 0;
        want = (int)MH_SIDE_GET(c->sides, g);

        if (c->mods & g_groups[g].groupBit) {
            if (want == MH_SIDE_LEFT  && !l) return 0;
            if (want == MH_SIDE_RIGHT && !r) return 0;
            if (want == MH_SIDE_ANY && !l && !r) return 0;
        } else if (l || r) {
            return 0;
        }
    }
    return 1;
}
