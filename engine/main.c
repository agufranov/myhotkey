/* MyHotkey engine.
 *
 * A hidden window, a tray icon and a message loop. Everything the hook decides
 * lands here as a posted message, which keeps the hook callback short enough
 * that Windows never evicts it. */
#include "common.h"
#include <shellapi.h>
#include <stdio.h>

#define MH_CLASS       L"MyHotkeyEngineWindow"
#define TIMER_RELOAD   1
#define TIMER_HOOK     2
#define TIMER_CAPTURE  3

/* A recorder left armed swallows the whole keyboard, so it gets a deadline. */
#define CAPTURE_TIMEOUT_MS 60000

#define ID_TRAY_ENABLED 100
#define ID_TRAY_EDIT    101
#define ID_TRAY_RELOAD  102
#define ID_TRAY_FOLDER  103
#define ID_TRAY_EXIT    104

int mh_config_write_default(const wchar_t *path);

static HWND    g_hwnd;
static RuleSet g_rules;
static wchar_t g_configPath[MAX_PATH];
static HANDLE  g_configWatch = INVALID_HANDLE_VALUE;
static NOTIFYICONDATAW g_nid;
static UINT    g_taskbarCreated;
static char    g_lastError[256];
static int     g_capturing;

int  mh_engine_rule_count(void)   { return g_rules.count; }
int  mh_engine_capturing(void)    { return g_capturing; }
void mh_engine_set_capturing(int on) {
    g_capturing = on;
    if (on) SetTimer(g_hwnd, TIMER_CAPTURE, CAPTURE_TIMEOUT_MS, NULL);
    else    KillTimer(g_hwnd, TIMER_CAPTURE);
}

/* ---------------------------------------------------------------- tray --- */

static void tray_add(void) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = MH_WM_TRAY;
    /* Ask for the small-icon size explicitly: the tray renders at 16-20 px,
       and letting Windows shrink the 32 px frame instead turns it to mush. */
    g_nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1),
                                    IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0);
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy(g_nid.szTip, MH_APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void tray_tip(const wchar_t *text) {
    _snwprintf(g_nid.szTip, 127, L"%s", text);
    g_nid.szTip[127] = 0;
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void tray_notify(const wchar_t *title, const wchar_t *body, DWORD icon) {
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = icon;
    _snwprintf(n.szInfoTitle, 63, L"%s", title);
    _snwprintf(n.szInfo, 255, L"%s", body);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

/* -------------------------------------------------------------- config --- */

/* Whether any rule could scold right now. The alert sound keeps the audio
   endpoint awake to stay instant, and there is no reason to pay for that on a
   config where nothing nags. Mirrors scold_target() in hook.c. */
static int scolding_possible(const RuleSet *rs) {
    int i;
    for (i = 0; i < rs->count; i++) {
        const Rule *r = &rs->rules[i];
        if (!r->enabled) continue;
        if (r->action.type == ACT_SCOLD) return 1;
        if (rs->scoldOldShortcuts && r->scold &&
            r->action.type == ACT_COMBO && r->action.stepCount == 1) return 1;
    }
    return 0;
}

static void reload_config(int announceSuccess) {
    /* static: a RuleSet is ~150 KB, too much to put on the stack */
    static RuleSet fresh;
    wchar_t tip[128];
    char err[256];
    int i, enabled = 0;

    /* The hook callback runs on this same thread, dispatched by our message
       loop, so swapping the rule set here cannot race with a keystroke. */
    if (!mh_config_load(g_configPath, &fresh, err, sizeof(err))) {
        wchar_t werr[256];
        MultiByteToWideChar(CP_UTF8, 0, err, -1, werr, 256);
        _snprintf(g_lastError, sizeof(g_lastError) - 1, "%s", err);
        tray_notify(L"MyHotkey: ошибка в настройках", werr, NIIF_ERROR);
        tray_tip(L"MyHotkey — ошибка в настройках, работают прежние правила");
        return;
    }

    g_lastError[0] = 0;
    mh_hook_set_rules(NULL);
    mh_config_free(&g_rules);
    g_rules = fresh;
    mh_hook_set_rules(&g_rules);
    mh_sound_set_active(scolding_possible(&g_rules));

    /* Enabled ones, not the total: rules ship switched off, so a count of
       everything loaded would claim the app is doing work it is not. */
    for (i = 0; i < g_rules.count; i++)
        if (g_rules.rules[i].enabled) enabled++;

    _snwprintf(tip, 127, L"MyHotkey — правил включено: %d", enabled);
    tray_tip(tip);
    if (announceSuccess)
        tray_notify(MH_APP_NAME, tip, NIIF_INFO);
}

static void watch_config(void) {
    wchar_t dir[MAX_PATH];
    wchar_t *slash;

    wcscpy(dir, g_configPath);
    slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;

    g_configWatch = FindFirstChangeNotificationW(dir, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
}

static void open_editor(void) {
    wchar_t exe[MAX_PATH], *slash;

    /* Prefer the real UI if it is installed next to us; fall back to whatever
       opens .json so the product is still usable before the UI ships. */
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    slash = wcsrchr(exe, L'\\');
    if (slash) {
        wcscpy(slash + 1, L"myhotkey-ui.exe");
        if (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES) {
            ShellExecuteW(NULL, L"open", exe, NULL, NULL, SW_SHOWNORMAL);
            return;
        }
    }
    ShellExecuteW(NULL, L"open", g_configPath, NULL, NULL, SW_SHOWNORMAL);
}

static void open_folder(void) {
    wchar_t arg[MAX_PATH + 16];
    _snwprintf(arg, MAX_PATH + 15, L"/select,\"%s\"", g_configPath);
    ShellExecuteW(NULL, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
}

static void show_menu(void) {
    HMENU menu = CreatePopupMenu();
    POINT pt;

    AppendMenuW(menu, MF_STRING | (mh_hook_get_enabled() ? MF_CHECKED : 0),
                ID_TRAY_ENABLED, L"Перехват включён");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EDIT,   L"Настроить сочетания…");
    AppendMenuW(menu, MF_STRING, ID_TRAY_RELOAD, L"Перечитать настройки");
    AppendMenuW(menu, MF_STRING, ID_TRAY_FOLDER, L"Открыть папку настроек");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT,   L"Выход");

    GetCursorPos(&pt);
    /* Required, or the menu refuses to close when the user clicks elsewhere. */
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

/* ------------------------------------------------------------- window --- */

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreated) {   /* explorer restarted, put the icon back */
        tray_add();
        return 0;
    }

    switch (msg) {
    case MH_WM_RUN_ACTION: {
        int index = (int)wp;
        if (index >= 0 && index < g_rules.count)
            mh_action_run(&g_rules.rules[index].action, (UINT)lp);
        return 0;
    }

    case MH_WM_CAPTURED: {
        /* The UI is recording a shortcut. The hook has already swallowed the
           key, so nothing else on the system saw it - that is what lets the
           recorder catch Alt+Tab and Win+R. */
        MhChord c;
        char text[64], line[96];
        mh_chord_from_event((UINT)wp, (UINT)lp, &c);
        if (mh_format_chord(&c, text, sizeof(text))) {
            _snprintf(line, sizeof(line) - 1, "key %s\n", text);
            line[sizeof(line) - 1] = 0;
            mh_ipc_push(line);
        }
        return 0;
    }

    case MH_WM_SCOLD: {
        /* Name both halves. "This is wrong" teaches nothing; "Alt+Left is now
           Alt+A" is the whole point of the alert. */
        int index = (int)wp;
        const Rule *r;
        char oldTxt[64], newTxt[64];
        wchar_t oldW[64], newW[64], msg[256];

        if (index < 0 || index >= g_rules.count) return 0;
        r = &g_rules.rules[index];
        if (r->action.type != ACT_COMBO || r->action.stepCount != 1) return 0;
        if (!mh_format_chord(&r->action.steps[0], oldTxt, sizeof(oldTxt))) return 0;
        if (!mh_format_chord(&r->trigger, newTxt, sizeof(newTxt))) return 0;

        MultiByteToWideChar(CP_UTF8, 0, oldTxt, -1, oldW, 64);
        MultiByteToWideChar(CP_UTF8, 0, newTxt, -1, newW, 64);
        _snwprintf(msg, 255, L"%s переназначено!\nТеперь это %s.", oldW, newW);
        msg[255] = 0;
        mh_scold_show(msg);
        return 0;
    }

    case MH_WM_RELOAD:
        reload_config(1);
        return 0;

    case MH_WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) show_menu();
        else if (LOWORD(lp) == WM_LBUTTONDBLCLK) open_editor();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_TRAY_ENABLED: mh_hook_set_enabled(!mh_hook_get_enabled()); break;
        case ID_TRAY_EDIT:    open_editor();      break;
        case ID_TRAY_RELOAD:  reload_config(1);   break;
        case ID_TRAY_FOLDER:  open_folder();      break;
        case ID_TRAY_EXIT:    DestroyWindow(hwnd); break;
        }
        return 0;

    case WM_TIMER:
        if (wp == TIMER_RELOAD) {
            KillTimer(hwnd, TIMER_RELOAD);   /* debounced: editors save in bursts */
            reload_config(0);
        } else if (wp == TIMER_CAPTURE) {
            /* The UI armed the recorder and never disarmed it - it crashed, or
               froze. Give the keyboard back rather than leave it swallowed. */
            KillTimer(hwnd, TIMER_CAPTURE);
            mh_hook_set_capture(0);
            g_capturing = 0;
            mh_ipc_push("capture timeout\n");
        } else if (wp == TIMER_HOOK) {
            /* Windows drops a hook that once ran too slowly and tells nobody.
               Re-arming it costs microseconds, so just do it periodically. */
            mh_hook_refresh();
        }
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* --------------------------------------------------------------- main --- */

/* One engine per session. A second launch just tells the first to reload,
   which is what double-clicking the exe after an edit should do anyway.

   But a held mutex is not proof that an engine is running. A reinstall kills
   the old engine and starts the new one in the same breath, and the corpse
   keeps its handles for a moment after taskkill returns - measured at around
   half a second. Believing the mutex there leaves the machine with no engine
   at all while the installer reports success, which is exactly what a silent
   install does. So ask the holder to reload, then check whether it is still
   there a second later; if it has died, the singleton is ours after all. */
static HANDLE claim_singleton(void) {
    int attempt;

    for (attempt = 0; attempt < 40; attempt++) {   /* ~4 s of corpses at most */
        HANDLE mutex = CreateMutexW(NULL, TRUE, MH_MUTEX_NAME);
        HWND   existing;
        HANDLE owner;
        DWORD  pid = 0, waited;

        if (mutex && GetLastError() != ERROR_ALREADY_EXISTS) return mutex;
        if (mutex) CloseHandle(mutex);

        /* No window yet: either the holder is on its way out, or it is a
           twin launched the same instant and has not created it yet. */
        existing = FindWindowW(MH_CLASS, NULL);
        if (!existing) { Sleep(100); continue; }

        /* Harmless if it is dying, and instant if it is not. */
        PostMessageW(existing, MH_WM_RELOAD, 0, 0);

        GetWindowThreadProcessId(existing, &pid);
        owner = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : NULL;
        if (!owner) return NULL;   /* cannot watch it; assume it is healthy */

        waited = WaitForSingleObject(owner, 1000);
        CloseHandle(owner);
        if (waited == WAIT_TIMEOUT) return NULL;   /* still running - it wins */
    }
    return NULL;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show) {
    WNDCLASSEXW wc;
    HANDLE mutex;
    MSG msg;

    (void)prev; (void)cmdline; (void)show;

    mutex = claim_singleton();
    if (!mutex) return 0;   /* another engine has it, and it is alive */

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = MH_CLASS;
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, MH_CLASS, MH_APP_NAME, 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, inst, NULL);
    if (!g_hwnd) return 1;

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    /* The engine normally runs elevated, started by Task Scheduler with
       highest privileges so the hook can see keys in elevated windows, and
       Windows then drops messages posted to it by ordinary processes. The
       reload a second launch sends is one of those, so let it through. It
       grants nothing: the same user can already edit config.json, and the
       engine reloads that on its own the moment the file changes. */
    ChangeWindowMessageFilterEx(g_hwnd, MH_WM_RELOAD, MSGFLT_ALLOW, NULL);

    if (!mh_config_default_path(g_configPath, MAX_PATH)) return 1;
    if (GetFileAttributesW(g_configPath) == INVALID_FILE_ATTRIBUTES)
        mh_config_write_default(g_configPath);

    tray_add();
    mh_sound_init();

    mh_hook_set_target(g_hwnd);
    if (!mh_hook_install()) {
        tray_notify(L"MyHotkey", L"Не удалось установить перехват клавиатуры.", NIIF_ERROR);
        return 1;
    }

    reload_config(0);
    watch_config();
    if (!mh_ipc_start(g_hwnd))
        tray_notify(MH_APP_NAME, L"Канал управления не открылся — окно настроек "
                                 L"не сможет подключиться.", NIIF_WARNING);
    SetTimer(g_hwnd, TIMER_HOOK, 60000, NULL);

    for (;;) {
        HANDLE waits[2];
        DWORD count = 0, r;

        if (g_configWatch != INVALID_HANDLE_VALUE) waits[count++] = g_configWatch;
        if (mh_ipc_event())                        waits[count++] = mh_ipc_event();

        r = MsgWaitForMultipleObjects(count, waits, FALSE, INFINITE, QS_ALLINPUT);

        if (r < WAIT_OBJECT_0 + count) {
            HANDLE signalled = waits[r - WAIT_OBJECT_0];
            if (signalled == g_configWatch) {
                SetTimer(g_hwnd, TIMER_RELOAD, 250, NULL);
                FindNextChangeNotification(g_configWatch);
            } else {
                mh_ipc_pump();
            }
            continue;
        }

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

done:
    mh_ipc_stop();
    mh_sound_stop();
    mh_hook_remove();
    mh_config_free(&g_rules);
    if (g_configWatch != INVALID_HANDLE_VALUE) FindCloseChangeNotification(g_configWatch);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
