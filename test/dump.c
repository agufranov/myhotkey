/* Loads a config through the engine's own parser and prints what it made of
   it. The answer to "why did my rule not fire" starts here.

   Usage: dump.exe [path]   (default: the real %APPDATA% config) */
#include "../engine/common.h"
#include <stdio.h>

static const char *action_name(ActionType t) {
    switch (t) {
        case ACT_NONE:   return "block";
        case ACT_COMBO:  return "combo";
        case ACT_TEXT:   return "text";
        case ACT_LAUNCH: return "launch";
        case ACT_LAYOUT: return "layout";
    }
    return "?";
}

int wmain(int argc, wchar_t **argv) {
    static RuleSet rs;
    wchar_t path[MAX_PATH];
    char err[256];
    int i;

    if (argc > 1) {
        wcsncpy(path, argv[1], MAX_PATH - 1);
        path[MAX_PATH - 1] = 0;
    } else if (!mh_config_default_path(path, MAX_PATH)) {
        printf("could not work out the config path\n");
        return 1;
    }
    wprintf(L"config: %s\n", path);

    if (!mh_config_load(path, &rs, err, sizeof(err))) {
        printf("LOAD FAILED: %s\n", err);
        return 1;
    }

    printf("%d rule%s\n\n", rs.count, rs.count == 1 ? "" : "s");
    for (i = 0; i < rs.count; i++) {
        const Rule *r = &rs.rules[i];
        char trigger[64];
        int s;

        mh_format_chord(&r->trigger, trigger, sizeof(trigger));
        printf("[%d] %-18s %-8s %s", i, trigger, action_name(r->action.type),
               r->enabled ? "" : "(disabled) ");
        if (r->onRelease) printf("on-release ");
        if (r->whenApp)   wprintf(L"%s=%s ", r->whenAppIsExclude ? L"not-app" : L"app", r->whenApp);

        switch (r->action.type) {
        case ACT_COMBO:
            printf("-> ");
            for (s = 0; s < r->action.stepCount; s++) {
                char step[64];
                mh_format_chord(&r->action.steps[s], step, sizeof(step));
                printf("%s%s", s ? ", " : "", step);
            }
            break;
        case ACT_TEXT:   wprintf(L"-> \"%s\"", r->action.text); break;
        case ACT_LAUNCH: wprintf(L"-> %s %s", r->action.path,
                                 r->action.args ? r->action.args : L""); break;
        case ACT_LAYOUT: printf("-> %s", r->action.layoutDelta < 0 ? "prev" : "next"); break;
        default: break;
        }
        printf("\n     trigger vk=0x%02X mods=0x%X sides=0x%X\n",
               r->trigger.vk, r->trigger.mods, r->trigger.sides);
    }
    mh_config_free(&rs);
    return 0;
}
