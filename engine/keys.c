/* Key-name table. Shared vocabulary between config.json, the UI and the hook. */
#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

typedef struct { const char *name; UINT vk; } KeyName;

/* First entry for a VK is the canonical name used when formatting back to text.
   Later duplicates are accepted aliases. */
static const KeyName g_keys[] = {
    /* modifiers - usable both as a mask and as a standalone trigger key */
    {"Ctrl",VK_CONTROL},{"Control",VK_CONTROL},
    {"Alt",VK_MENU},{"Menu",VK_MENU},
    {"Shift",VK_SHIFT},
    {"LCtrl",VK_LCONTROL},{"RCtrl",VK_RCONTROL},
    {"LAlt",VK_LMENU},{"RAlt",VK_RMENU},
    {"LShift",VK_LSHIFT},{"RShift",VK_RSHIFT},
    {"Win",VK_LWIN},{"LWin",VK_LWIN},{"RWin",VK_RWIN},

    {"CapsLock",VK_CAPITAL},{"Caps",VK_CAPITAL},
    {"NumLock",VK_NUMLOCK},{"ScrollLock",VK_SCROLL},
    {"Tab",VK_TAB},{"Enter",VK_RETURN},{"Return",VK_RETURN},
    {"Esc",VK_ESCAPE},{"Escape",VK_ESCAPE},
    {"Space",VK_SPACE},{"Backspace",VK_BACK},{"BS",VK_BACK},
    {"Delete",VK_DELETE},{"Del",VK_DELETE},{"Insert",VK_INSERT},{"Ins",VK_INSERT},
    {"Home",VK_HOME},{"End",VK_END},
    {"PageUp",VK_PRIOR},{"PgUp",VK_PRIOR},{"PageDown",VK_NEXT},{"PgDn",VK_NEXT},
    {"Left",VK_LEFT},{"Right",VK_RIGHT},{"Up",VK_UP},{"Down",VK_DOWN},
    {"PrintScreen",VK_SNAPSHOT},{"Pause",VK_PAUSE},{"Apps",VK_APPS},

    /* punctuation - names, not glyphs, so '+' never collides with the separator */
    {"Minus",VK_OEM_MINUS},{"Equals",VK_OEM_PLUS},
    {"LBracket",VK_OEM_4},{"RBracket",VK_OEM_6},{"Backslash",VK_OEM_5},
    {"Semicolon",VK_OEM_1},{"Quote",VK_OEM_7},{"Backtick",VK_OEM_3},
    {"Comma",VK_OEM_COMMA},{"Period",VK_OEM_PERIOD},{"Slash",VK_OEM_2},

    /* numpad */
    {"Numpad0",VK_NUMPAD0},{"Numpad1",VK_NUMPAD1},{"Numpad2",VK_NUMPAD2},
    {"Numpad3",VK_NUMPAD3},{"Numpad4",VK_NUMPAD4},{"Numpad5",VK_NUMPAD5},
    {"Numpad6",VK_NUMPAD6},{"Numpad7",VK_NUMPAD7},{"Numpad8",VK_NUMPAD8},
    {"Numpad9",VK_NUMPAD9},
    {"NumpadMultiply",VK_MULTIPLY},{"NumpadAdd",VK_ADD},
    {"NumpadSubtract",VK_SUBTRACT},{"NumpadDecimal",VK_DECIMAL},
    {"NumpadDivide",VK_DIVIDE},

    /* media / browser */
    {"VolumeUp",VK_VOLUME_UP},{"VolumeDown",VK_VOLUME_DOWN},{"VolumeMute",VK_VOLUME_MUTE},
    {"MediaNext",VK_MEDIA_NEXT_TRACK},{"MediaPrev",VK_MEDIA_PREV_TRACK},
    {"MediaStop",VK_MEDIA_STOP},{"MediaPlayPause",VK_MEDIA_PLAY_PAUSE},
    {"BrowserBack",VK_BROWSER_BACK},{"BrowserForward",VK_BROWSER_FORWARD},
};

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

UINT mh_vk_from_name(const char *name) {
    size_t i, n;
    if (!name || !*name) return 0;
    n = strlen(name);

    /* A-Z and 0-9 map straight onto their VK codes */
    if (n == 1) {
        char c = (char)toupper((unsigned char)name[0]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (UINT)c;
    }
    /* F1..F24 */
    if ((name[0] == 'F' || name[0] == 'f') && n >= 2 && n <= 3) {
        int num = atoi(name + 1);
        if (num >= 1 && num <= 24) return VK_F1 + num - 1;
    }
    for (i = 0; i < sizeof(g_keys)/sizeof(g_keys[0]); i++)
        if (ieq(g_keys[i].name, name)) return g_keys[i].vk;
    return 0;
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
    for (i = 0; i < sizeof(g_keys)/sizeof(g_keys[0]); i++)
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

/* Keys that live on the extended part of the scancode set. SendInput needs the
   flag or apps that read scancodes (games, terminals, RDP) see the wrong key. */
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

/* "Alt+S", "Ctrl+Shift", "CapsLock" -> MhChord. Returns 1 on success. */
int mh_parse_chord(const char *text, MhChord *out) {
    char tok[32];
    size_t ti = 0;
    const char *p;
    int sawKey = 0;

    if (!text || !out) return 0;
    out->vk = 0; out->mods = 0;

    for (p = text; ; p++) {
        if (*p && *p != '+') {
            if (ti == 0 && *p == ' ') continue;        /* leading padding */
            if (ti < sizeof(tok) - 1) tok[ti++] = *p;
            continue;
        }
        /* '+' as the very first char of a token means the literal plus key */
        if (*p == '+' && ti == 0 && p[1] == '\0') { ti = 1; tok[0] = '+'; }
        tok[ti] = 0;
        while (ti > 0 && tok[ti-1] == ' ') tok[--ti] = 0;

        if (ti > 0) {
            UINT bit;
            UINT vk = ieq(tok, "+") ? (UINT)VK_OEM_PLUS : mh_vk_from_name(tok);
            if (!vk) return 0;
            bit = mh_mod_bit_for_vk(vk);
            /* A bare modifier is a modifier only when something follows it.
               Trailing modifiers stay in the mask: "Ctrl+Shift" is mods-only. */
            if (bit && (vk == VK_CONTROL || vk == VK_MENU ||
                        vk == VK_SHIFT || vk == VK_LWIN)) {
                out->mods |= bit;
            } else {
                if (sawKey) return 0;
                out->vk = vk; sawKey = 1;
            }
        }
        ti = 0;
        if (!*p) break;
    }
    return (out->vk || out->mods) ? 1 : 0;
}

int mh_format_chord(const MhChord *c, char *buf, size_t cap) {
    const char *kn;
    buf[0] = 0;
    if (c->mods & MH_MOD_CTRL)  strncat(buf, "Ctrl+",  cap - strlen(buf) - 1);
    if (c->mods & MH_MOD_ALT)   strncat(buf, "Alt+",   cap - strlen(buf) - 1);
    if (c->mods & MH_MOD_SHIFT) strncat(buf, "Shift+", cap - strlen(buf) - 1);
    if (c->mods & MH_MOD_WIN)   strncat(buf, "Win+",   cap - strlen(buf) - 1);
    if (c->vk) {
        kn = mh_name_from_vk(c->vk);
        if (!kn) return 0;
        strncat(buf, kn, cap - strlen(buf) - 1);
    } else {
        size_t n = strlen(buf);          /* drop the dangling '+' */
        if (n) buf[n-1] = 0;
    }
    return 1;
}
