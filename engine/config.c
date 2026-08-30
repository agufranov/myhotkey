/* config.json -> RuleSet.
 *
 * The file is the single source of truth shared with the UI. A human can edit
 * it by hand, but nobody has to: every field here has a widget on the other
 * side. Errors carry the offending rule index so the UI can point at a row. */
#include "common.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

static wchar_t *dup_utf8(const char *s) {
    int n;
    wchar_t *w;
    if (!s) return NULL;
    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

int mh_config_default_path(wchar_t *buf, size_t cap) {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) return 0;
    if (_snwprintf(buf, cap, L"%s\\MyHotkey", appdata) < 0) return 0;
    CreateDirectoryW(buf, NULL);
    if (_snwprintf(buf, cap, L"%s\\MyHotkey\\config.json", appdata) < 0) return 0;
    return 1;
}

static char *read_all(const wchar_t *path) {
    HANDLE f;
    LARGE_INTEGER size;
    DWORD got = 0;
    char *buf;

    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(f, &size) || size.QuadPart > (16 << 20)) { CloseHandle(f); return NULL; }

    buf = (char *)malloc((size_t)size.QuadPart + 1);
    if (!buf) { CloseHandle(f); return NULL; }
    if (!ReadFile(f, buf, (DWORD)size.QuadPart, &got, NULL)) { free(buf); CloseHandle(f); return NULL; }
    buf[got] = 0;
    CloseHandle(f);
    return buf;
}

static int parse_action(const Json *jact, Action *a, char *err, size_t errCap, int index) {
    const char *type = json_str(json_get(jact, "type"), "combo");

    memset(a, 0, sizeof(*a));

    if (strcmp(type, "combo") == 0) {
        const Json *keys = json_get(jact, "keys");
        int i, n;
        a->type = ACT_COMBO;
        n = json_count(keys);
        if (n <= 0) {
            _snprintf(err, errCap - 1, "rule %d: action \"combo\" needs a non-empty \"keys\" list", index);
            return 0;
        }
        if (n > MH_MAX_STEPS) n = MH_MAX_STEPS;
        for (i = 0; i < n; i++) {
            const char *s = json_str(json_at(keys, i), NULL);
            if (!s || !mh_parse_chord(s, &a->steps[i])) {
                _snprintf(err, errCap - 1, "rule %d: cannot parse key \"%s\"", index, s ? s : "(not a string)");
                return 0;
            }
        }
        a->stepCount = n;
        return 1;
    }
    if (strcmp(type, "text") == 0) {
        a->type = ACT_TEXT;
        a->text = dup_utf8(json_str(json_get(jact, "text"), ""));
        return 1;
    }
    if (strcmp(type, "launch") == 0) {
        const char *path = json_str(json_get(jact, "path"), NULL);
        if (!path) {
            _snprintf(err, errCap - 1, "rule %d: action \"launch\" needs a \"path\"", index);
            return 0;
        }
        a->type = ACT_LAUNCH;
        a->path = dup_utf8(path);
        a->args = dup_utf8(json_str(json_get(jact, "args"), NULL));
        return 1;
    }
    if (strcmp(type, "layout") == 0) {
        const char *dir = json_str(json_get(jact, "direction"), "next");
        a->type = ACT_LAYOUT;
        a->layoutDelta = (strcmp(dir, "prev") == 0) ? -1 : 1;
        return 1;
    }
    if (strcmp(type, "block") == 0 || strcmp(type, "none") == 0) {
        a->type = ACT_NONE;
        return 1;
    }

    _snprintf(err, errCap - 1, "rule %d: unknown action type \"%s\"", index, type);
    return 0;
}

int mh_config_load(const wchar_t *path, RuleSet *out, char *err, size_t errCap) {
    char *text;
    Json *root, *jrules;
    int i, n;

    memset(out, 0, sizeof(*out));
    if (err && errCap) err[0] = 0;

    text = read_all(path);
    if (!text) {
        _snprintf(err, errCap - 1, "cannot read the config file");
        return 0;
    }

    root = json_parse(text, err, errCap);
    free(text);
    if (!root) return 0;

    jrules = json_get(root, "rules");
    if (!jrules || jrules->type != JS_ARR) {
        _snprintf(err, errCap - 1, "the config needs a \"rules\" array");
        json_free(root);
        return 0;
    }

    n = json_count(jrules);
    if (n > MH_MAX_RULES) n = MH_MAX_RULES;

    for (i = 0; i < n; i++) {
        Json *jr = json_at(jrules, i);
        Rule *r  = &out->rules[out->count];
        const char *trigger, *on, *app;

        memset(r, 0, sizeof(*r));
        r->enabled = json_bool(json_get(jr, "enabled"), 1);

        trigger = json_str(json_get(jr, "trigger"), NULL);
        if (!trigger || !mh_parse_chord(trigger, &r->trigger)) {
            _snprintf(err, errCap - 1, "rule %d: cannot parse trigger \"%s\"",
                      i, trigger ? trigger : "(missing)");
            goto bad;
        }
        /* A trigger has to end in a real key. Modifiers alone would mean the
           rule fires on every Ctrl press, which is never what anyone wants. */
        if (r->trigger.vk == 0) {
            _snprintf(err, errCap - 1, "rule %d: trigger \"%s\" is modifiers only", i, trigger);
            goto bad;
        }

        r->id   = dup_utf8(json_str(json_get(jr, "id"), NULL));
        r->name = dup_utf8(json_str(json_get(jr, "name"), NULL));

        on = json_str(json_get(jr, "on"), "press");
        r->onRelease = (strcmp(on, "release") == 0);

        app = json_str(json_get(jr, "app"), NULL);
        if (app) {
            r->whenApp = dup_utf8(app);
        } else {
            app = json_str(json_get(jr, "notApp"), NULL);
            if (app) { r->whenApp = dup_utf8(app); r->whenAppIsExclude = 1; }
        }

        if (!parse_action(json_get(jr, "action"), &r->action, err, errCap, i)) goto bad;

        out->count++;
    }

    json_free(root);
    return 1;

bad:
    json_free(root);
    mh_config_free(out);
    return 0;
}

void mh_config_free(RuleSet *rs) {
    int i;
    for (i = 0; i < rs->count; i++) {
        Rule *r = &rs->rules[i];
        free(r->id);
        free(r->name);
        free(r->whenApp);
        free(r->action.text);
        free(r->action.path);
        free(r->action.args);
    }
    memset(rs, 0, sizeof(*rs));
}

/* Written once, on first run, so a fresh install already does something and
   the user has a worked example to look at. */
static const char *kDefaultConfig =
"{\n"
"  \"version\": 1,\n"
"  \"rules\": [\n"
"    {\n"
"      \"id\": \"caps-layout\",\n"
"      \"name\": \"CapsLock switches the keyboard layout\",\n"
"      \"enabled\": true,\n"
"      \"trigger\": \"CapsLock\",\n"
"      \"on\": \"release\",\n"
"      \"action\": { \"type\": \"combo\", \"keys\": [\"Ctrl+Shift\"] }\n"
"    },\n"
"    {\n"
"      \"id\": \"alt-s-back\",\n"
"      \"name\": \"Alt+S goes back\",\n"
"      \"enabled\": true,\n"
"      \"trigger\": \"Alt+S\",\n"
"      \"action\": { \"type\": \"combo\", \"keys\": [\"Alt+Left\"] }\n"
"    }\n"
"  ]\n"
"}\n";

int mh_config_write_default(const wchar_t *path) {
    HANDLE f;
    DWORD written = 0;
    DWORD len = (DWORD)strlen(kDefaultConfig);

    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    WriteFile(f, kDefaultConfig, len, &written, NULL);
    CloseHandle(f);
    return written == len;
}
