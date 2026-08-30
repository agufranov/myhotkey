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

/* ---------------------------------------------------------------- tray --- */

static void tray_add(void) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = MH_WM_TRAY;
    g_nid.hIcon            = LoadIconW(NULL, IDI_APPLICATION);
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

static void reload_config(int announceSuccess) {
    /* static: a RuleSet is ~150 KB, too much to put on the stack */
    static RuleSet fresh;
    wchar_t tip[128];
    char err[256];

    /* The hook callback runs on this same thread, dispatched by our message
       loop, so swapping the rule set here cannot race with a keystroke. */
    if (!mh_config_load(g_configPath, &fresh, err, sizeof(err))) {
        wchar_t werr[256];
        MultiByteToWideChar(CP_UTF8, 0, err, -1, werr, 256);
        _snprintf(g_lastError, sizeof(g_lastError) - 1, "%s", err);
        tray_notify(L"MyHotkey: config error", werr, NIIF_ERROR);
        tray_tip(L"MyHotkey - config error, old rules still active");
        return;
    }

    g_lastError[0] = 0;
    mh_hook_set_rules(NULL);
    mh_config_free(&g_rules);
    g_rules = fresh;
    mh_hook_set_rules(&g_rules);

    _snwprintf(tip, 127, L"MyHotkey - %d rule%s active", g_rules.count,
               g_rules.count == 1 ? L"" : L"s");
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
                ID_TRAY_ENABLED, L"Enabled");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EDIT,   L"Edit shortcuts...");
    AppendMenuW(menu, MF_STRING, ID_TRAY_RELOAD, L"Reload config");
    AppendMenuW(menu, MF_STRING, ID_TRAY_FOLDER, L"Open config folder");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT,   L"Exit");

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

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show) {
    WNDCLASSEXW wc;
    HANDLE mutex;
    MSG msg;

    (void)prev; (void)cmdline; (void)show;

    /* One engine per session. A second launch just tells the first to reload,
       which is what double-clicking the exe after an edit should do anyway. */
    mutex = CreateMutexW(NULL, TRUE, MH_MUTEX_NAME);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(MH_CLASS, NULL);
        if (existing) PostMessageW(existing, MH_WM_RELOAD, 0, 0);
        return 0;
    }

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

    if (!mh_config_default_path(g_configPath, MAX_PATH)) return 1;
    if (GetFileAttributesW(g_configPath) == INVALID_FILE_ATTRIBUTES)
        mh_config_write_default(g_configPath);

    tray_add();

    mh_hook_set_target(g_hwnd);
    if (!mh_hook_install()) {
        tray_notify(L"MyHotkey", L"Could not install the keyboard hook.", NIIF_ERROR);
        return 1;
    }

    reload_config(0);
    watch_config();
    SetTimer(g_hwnd, TIMER_HOOK, 60000, NULL);

    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(
            g_configWatch == INVALID_HANDLE_VALUE ? 0 : 1,
            &g_configWatch, FALSE, INFINITE, QS_ALLINPUT);

        if (r == WAIT_OBJECT_0 && g_configWatch != INVALID_HANDLE_VALUE) {
            SetTimer(g_hwnd, TIMER_RELOAD, 250, NULL);
            FindNextChangeNotification(g_configWatch);
            continue;
        }

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

done:
    mh_hook_remove();
    mh_config_free(&g_rules);
    if (g_configWatch != INVALID_HANDLE_VALUE) FindCloseChangeNotification(g_configWatch);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
