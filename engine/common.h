/* MyHotkey engine - shared declarations */
#ifndef MH_COMMON_H
#define MH_COMMON_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#define MH_APP_NAME     L"MyHotkey"
#define MH_MUTEX_NAME   L"Local\\MyHotkeyEngineSingleton"
#define MH_PIPE_NAME    L"\\\\.\\pipe\\myhotkey"

#define MH_MAX_RULES    512
#define MH_MAX_STEPS     32

/* Modifier mask. Deliberately side-agnostic: a rule asks for "Ctrl",
   not "left Ctrl". Side-specific triggers are a separate feature. */
enum {
    MH_MOD_CTRL  = 1 << 0,
    MH_MOD_ALT   = 1 << 1,
    MH_MOD_SHIFT = 1 << 2,
    MH_MOD_WIN   = 1 << 3
};

typedef struct {
    UINT vk;    /* 0 = modifiers only, e.g. the Ctrl+Shift layout combo */
    UINT mods;
} MhChord;

typedef enum {
    ACT_NONE = 0,   /* swallow the key, do nothing else */
    ACT_COMBO,      /* send a sequence of chords */
    ACT_TEXT,       /* type a string */
    ACT_LAUNCH,     /* start a program or open a URL */
    ACT_LAYOUT      /* switch keyboard layout directly, no key faking */
} ActionType;

typedef struct {
    ActionType type;
    MhChord      steps[MH_MAX_STEPS];
    int        stepCount;
    wchar_t   *text;    /* ACT_TEXT */
    wchar_t   *path;    /* ACT_LAUNCH */
    wchar_t   *args;    /* ACT_LAUNCH */
    int        layoutDelta; /* ACT_LAYOUT: +1 next, -1 previous, 0 toggle */
} Action;

typedef struct {
    int       enabled;
    wchar_t  *id;
    wchar_t  *name;
    MhChord     trigger;
    int       onRelease;   /* fire on key-up instead of key-down */
    wchar_t  *whenApp;     /* NULL = everywhere; else foreground exe name */
    int       whenAppIsExclude;
    Action    action;
} Rule;

typedef struct {
    Rule rules[MH_MAX_RULES];
    int  count;
} RuleSet;

/* keys.c */
UINT        mh_vk_from_name(const char *name);
const char *mh_name_from_vk(UINT vk);
int         mh_parse_chord(const char *text, MhChord *out);
int         mh_format_chord(const MhChord *c, char *buf, size_t cap);
UINT        mh_mod_bit_for_vk(UINT vk);
int         mh_vk_is_extended(UINT vk);

/* action.c */
void mh_action_run(const Action *a, UINT heldMods);
void mh_send_chord(const MhChord *c, UINT heldMods);

/* config.c */
int  mh_config_load(const wchar_t *path, RuleSet *out, char *err, size_t errCap);
void mh_config_free(RuleSet *rs);
int  mh_config_default_path(wchar_t *buf, size_t cap);
int  mh_config_write_default(const wchar_t *path);

/* Messages the hook posts to the engine window. Everything heavy happens
   there, not in the hook callback. */
#define MH_WM_RUN_ACTION  (WM_APP + 1)   /* wParam = rule index, lParam = held mods */
#define MH_WM_CAPTURED    (WM_APP + 2)   /* wParam = vk,         lParam = held mods */
#define MH_WM_RELOAD      (WM_APP + 3)
#define MH_WM_TRAY        (WM_APP + 4)

/* hook.c */
int  mh_hook_install(void);
void mh_hook_remove(void);
int  mh_hook_refresh(void);
int  mh_hook_alive(void);
void mh_hook_set_rules(RuleSet *rs);
void mh_hook_set_target(HWND hwnd);
void mh_hook_set_enabled(int enabled);
int  mh_hook_get_enabled(void);
void mh_hook_set_capture(int on);

#endif
