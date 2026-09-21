/* Named-pipe server: how the UI talks to the engine.
 *
 * Line-based text, one command per line, one reply per command. Deliberately
 * dumb so a UI in any language can speak it with a stream reader and no
 * marshalling layer.
 *
 * All of it runs on the engine's own thread, driven by the same message loop
 * that dispatches the keyboard hook. No worker thread, so no locking, and no
 * chance of a rule set being swapped underneath a keystroke. */
#include "common.h"
#include <sddl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define IPC_BUF 2048

static HANDLE     g_pipe = INVALID_HANDLE_VALUE;
static HANDLE     g_event;
static HANDLE     g_writeEvent;
static OVERLAPPED g_ov;
static HWND       g_target;
static int        g_connected;
static char       g_in[IPC_BUF];

/* Only this user may connect. Matters because the engine may be running
   elevated - started by Task Scheduler with highest privileges so it can see
   keys in elevated windows - while the UI is not. Without an explicit
   descriptor the default one would refuse the very process meant to talk. */
static SECURITY_ATTRIBUTES *pipe_security(SECURITY_ATTRIBUTES *sa) {
    HANDLE token;
    char buf[256];
    TOKEN_USER *tu = (TOKEN_USER *)buf;
    DWORD got = 0;
    LPWSTR sidText = NULL;
    wchar_t sddl[256];

    sa->nLength = sizeof(*sa);
    sa->bInheritHandle = FALSE;
    sa->lpSecurityDescriptor = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return NULL;
    if (!GetTokenInformation(token, TokenUser, tu, sizeof(buf), &got)) {
        CloseHandle(token);
        return NULL;
    }
    CloseHandle(token);

    if (!ConvertSidToStringSidW(tu->User.Sid, &sidText)) return NULL;
    _snwprintf(sddl, 255, L"D:(A;;GA;;;%s)(A;;GA;;;SY)", sidText);
    sddl[255] = 0;
    LocalFree(sidText);

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl, SDDL_REVISION_1, &sa->lpSecurityDescriptor, NULL))
        return NULL;
    return sa;
}

/* ------------------------------------------------------------- plumbing --- */

static void start_read(void) {
    ResetEvent(g_event);
    memset(&g_ov, 0, sizeof(g_ov));
    g_ov.hEvent = g_event;
    if (!ReadFile(g_pipe, g_in, IPC_BUF - 1, NULL, &g_ov) &&
        GetLastError() != ERROR_IO_PENDING) {
        SetEvent(g_event);   /* let the pump notice and reset the connection */
    }
}

static void start_connect(void) {
    g_connected = 0;
    ResetEvent(g_event);
    memset(&g_ov, 0, sizeof(g_ov));
    g_ov.hEvent = g_event;
    if (!ConnectNamedPipe(g_pipe, &g_ov)) {
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED) SetEvent(g_event);
        else if (err != ERROR_IO_PENDING) SetEvent(g_event);
    }
}

static void reset_connection(void) {
    DisconnectNamedPipe(g_pipe);
    mh_hook_set_capture(0);      /* never leave the keyboard swallowed */
    start_connect();
}

void mh_ipc_push(const char *line) {
    OVERLAPPED ov;
    DWORD written = 0;
    DWORD len = (DWORD)strlen(line);

    if (!g_connected || g_pipe == INVALID_HANDLE_VALUE) return;

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = g_writeEvent;
    ResetEvent(g_writeEvent);

    if (!WriteFile(g_pipe, line, len, NULL, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) { reset_connection(); return; }
        /* Bounded so a wedged UI can never stall the keyboard. */
        if (WaitForSingleObject(g_writeEvent, 1000) != WAIT_OBJECT_0) {
            CancelIo(g_pipe);
            reset_connection();
            return;
        }
    }
    GetOverlappedResult(g_pipe, &ov, &written, FALSE);
}

static void reply(const char *fmt, ...) {
    char line[IPC_BUF];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    line[sizeof(line) - 2] = 0;
    strcat(line, "\n");
    mh_ipc_push(line);
}

/* -------------------------------------------------------------- command --- */

static int word_is(const char *s, const char *word, const char **rest) {
    size_t n = strlen(word);
    if (_strnicmp(s, word, n) != 0) return 0;
    if (s[n] && s[n] != ' ') return 0;
    if (rest) {
        *rest = s + n;
        while (**rest == ' ') (*rest)++;
    }
    return 1;
}

static void handle(const char *cmd) {
    const char *arg;

    if (word_is(cmd, "ping", NULL)) {
        reply("pong");
    } else if (word_is(cmd, "status", NULL)) {
        /* hook=0 means the engine is up but remaps nothing, which the settings
           window must not paint as healthy. */
        reply("status enabled=%d rules=%d capture=%d hook=%d",
              mh_hook_get_enabled(), mh_engine_rule_count(), mh_engine_capturing(),
              mh_hook_alive());
    } else if (word_is(cmd, "reload", NULL)) {
        PostMessageW(g_target, MH_WM_RELOAD, 0, 0);
        reply("ok");
    } else if (word_is(cmd, "enable", &arg)) {
        mh_hook_set_enabled(*arg != '0');
        reply("ok");
    } else if (word_is(cmd, "capture", &arg)) {
        int on = word_is(arg, "on", NULL);
        mh_hook_set_capture(on);
        mh_engine_set_capturing(on);
        reply("ok");
    } else if (word_is(cmd, "version", NULL)) {
        reply("version 1");
    } else {
        reply("error unknown command");
    }
}

/* One read can carry several lines if the UI wrote them back to back. */
static void handle_buffer(char *text) {
    char *line = text;
    char *p;
    for (p = text; ; p++) {
        if (*p == '\r') { *p = 0; continue; }
        if (*p == '\n' || *p == 0) {
            int last = (*p == 0);
            *p = 0;
            if (*line) handle(line);
            if (last) return;
            line = p + 1;
        }
    }
}

/* ----------------------------------------------------------------- pump --- */

void mh_ipc_pump(void) {
    DWORD moved = 0;

    if (g_pipe == INVALID_HANDLE_VALUE) return;

    if (!GetOverlappedResult(g_pipe, &g_ov, &moved, FALSE)) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_INCOMPLETE) return;
        reset_connection();
        return;
    }

    if (!g_connected) {
        g_connected = 1;
        start_read();
        return;
    }

    if (moved == 0) { reset_connection(); return; }

    g_in[moved] = 0;
    handle_buffer(g_in);
    start_read();
}

HANDLE mh_ipc_event(void) { return g_event; }

int mh_ipc_start(HWND target) {
    SECURITY_ATTRIBUTES sa, *psa;

    g_target = target;
    g_event      = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_writeEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_event || !g_writeEvent) return 0;

    psa = pipe_security(&sa);
    g_pipe = CreateNamedPipeW(MH_PIPE_NAME,
                              PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                              1, IPC_BUF, IPC_BUF, 0, psa);
    if (psa && psa->lpSecurityDescriptor) LocalFree(psa->lpSecurityDescriptor);
    if (g_pipe == INVALID_HANDLE_VALUE) return 0;

    start_connect();
    return 1;
}

void mh_ipc_stop(void) {
    if (g_pipe != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(g_pipe);
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
    if (g_event)      { CloseHandle(g_event);      g_event = NULL; }
    if (g_writeEvent) { CloseHandle(g_writeEvent); g_writeEvent = NULL; }
}
