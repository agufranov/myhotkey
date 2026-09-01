/* The scold box: a Windows 98 error dialog for pressing the old shortcut.
 *
 * The habit trainer. A rule with action "scold" swallows the combo it fires
 * on, and this window pops up instead - so the hand that keeps reaching for
 * the old shortcut learns the new one, the way hardtime.vim weans people off
 * arrow keys. The retro look is the point: it should feel like a slap from
 * 1998, not a polite toast.
 *
 * Everything is drawn by hand with classic GDI - DrawEdge and
 * DrawFrameControl still render the unthemed Win9x visuals on modern
 * Windows, so the raised grey frame, the caption close button and the OK
 * button come out pixel-authentic for free. Only the title gradient and the
 * critical-stop icon are painted manually.
 *
 * The window never takes focus (WS_EX_NOACTIVATE): stealing the keyboard
 * from whatever the user is typing in would be a real punishment, and the
 * punishment here is strictly theatrical. It closes itself after a moment,
 * or on any click. Runs on the engine thread; the engine's own message loop
 * dispatches its paint and timer, none of which goes near the hook. */
#include "common.h"
#include <stdio.h>
#include <string.h>

#define SCOLD_CLASS      L"MyHotkeyScoldWindow"
#define SCOLD_TIMEOUT_MS 2500

#define EDGE     3   /* raised border plus one pixel of face */
#define TITLE_H 18
#define PAD     14
#define ICON_SZ 32
#define BTN_W   75
#define BTN_H   23

static HWND    g_wnd;
static HFONT   g_font, g_fontBold;
static wchar_t g_msg[256];
static RECT    g_textRc;   /* client coords, computed when shown */
static RECT    g_okRc;

static const wchar_t kDefaultMsg[] = L"Эта комбинация переназначена!";

/* Navy to sky blue, left to right - the active-caption gradient of Win98. */
static void paint_title_gradient(HDC dc, const RECT *tb) {
    int w = tb->right - tb->left, x;
    for (x = 0; x < w; x += 4) {
        int t = w > 1 ? x * 255 / (w - 1) : 0;
        RECT s = { tb->left + x, tb->top, tb->left + x + 4, tb->bottom };
        HBRUSH b = CreateSolidBrush(RGB(16 * t / 255, 132 * t / 255,
                                        128 + 80 * t / 255));
        if (s.right > tb->right) s.right = tb->right;
        FillRect(dc, &s, b);
        DeleteObject(b);
    }
}

/* The classic critical stop: a red circle with a fat white cross. */
static void paint_icon(HDC dc, int x, int y) {
    HBRUSH red  = CreateSolidBrush(RGB(220, 0, 0));
    HPEN   rim  = CreatePen(PS_SOLID, 1, RGB(128, 0, 0));
    HPEN   line = CreatePen(PS_SOLID, 4, RGB(255, 255, 255));
    HGDIOBJ ob  = SelectObject(dc, red);
    HGDIOBJ op  = SelectObject(dc, rim);

    Ellipse(dc, x, y, x + ICON_SZ, y + ICON_SZ);

    SelectObject(dc, line);
    MoveToEx(dc, x + 10, y + 10, NULL); LineTo(dc, x + ICON_SZ - 10, y + ICON_SZ - 10);
    MoveToEx(dc, x + ICON_SZ - 10, y + 10, NULL); LineTo(dc, x + 10, y + ICON_SZ - 10);

    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(line);
    DeleteObject(rim);
    DeleteObject(red);
}

static void paint(HDC dc, RECT rc) {
    RECT tb, btn, cap;
    HGDIOBJ of;

    DrawEdge(dc, &rc, EDGE_RAISED, BF_RECT);

    tb.left   = EDGE;
    tb.top    = EDGE;
    tb.right  = rc.right - EDGE;
    tb.bottom = EDGE + TITLE_H;
    paint_title_gradient(dc, &tb);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    of = SelectObject(dc, g_fontBold);
    cap = tb;
    cap.left += 6;
    DrawTextW(dc, MH_APP_NAME, -1, &cap,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    btn.left   = tb.right - 18;
    btn.top    = tb.top + 2;
    btn.right  = tb.right - 2;
    btn.bottom = tb.bottom - 2;
    DrawFrameControl(dc, &btn, DFC_CAPTION, DFCS_CAPTIONCLOSE);

    paint_icon(dc, EDGE + PAD, g_textRc.top +
               ((g_textRc.bottom - g_textRc.top) - ICON_SZ) / 2);

    SetTextColor(dc, RGB(0, 0, 0));
    SelectObject(dc, g_font);
    DrawTextW(dc, g_msg, -1, &g_textRc,
              DT_WORDBREAK | DT_NOPREFIX | DT_VCENTER);

    DrawFrameControl(dc, &g_okRc, DFC_BUTTON, DFCS_BUTTONPUSH);
    DrawTextW(dc, L"OK", -1, &g_okRc,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    SelectObject(dc, of);
}

static void hide(HWND hwnd) {
    KillTimer(hwnd, 1);
    ShowWindow(hwnd, SW_HIDE);
}

static LRESULT CALLBACK scold_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        paint(dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:      /* the whole window is one big OK button */
        hide(hwnd);
        return 0;
    case WM_TIMER:
        hide(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int ensure_window(void) {
    WNDCLASSEXW wc;

    if (g_wnd) return 1;

    /* MS Sans Serif is the bitmap font of the era and still ships with
       Windows; NONANTIALIASED keeps its edges as crisp as they were in 98. */
    g_font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         0, 0, NONANTIALIASED_QUALITY, 0, L"MS Sans Serif");
    g_fontBold = CreateFontW(-11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                             0, 0, NONANTIALIASED_QUALITY, 0, L"MS Sans Serif");

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = scold_proc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(192, 192, 192));
    wc.lpszClassName = SCOLD_CLASS;
    if (!RegisterClassExW(&wc)) return 0;

    g_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            SCOLD_CLASS, L"", WS_POPUP, 0, 0, 10, 10,
                            NULL, NULL, GetModuleHandleW(NULL), NULL);
    return g_wnd != NULL;
}

void mh_scold_show(const wchar_t *message) {
    RECT work, tr;
    HDC dc;
    HGDIOBJ of;
    int textW, textH, w, h, x, y;

    if (!ensure_window()) return;

    /* Sound first: it is one SetEvent, and the ear notices the gap before the
       eye does. */
    mh_sound_alert();

    _snwprintf(g_msg, 255, L"%s",
               message && message[0] ? message : kDefaultMsg);
    g_msg[255] = 0;

    tr.left = tr.top = tr.bottom = 0;
    tr.right = 300;
    dc = GetDC(g_wnd);
    of = SelectObject(dc, g_font);
    DrawTextW(dc, g_msg, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, of);
    ReleaseDC(g_wnd, dc);

    textW = tr.right;
    textH = tr.bottom > ICON_SZ ? tr.bottom : ICON_SZ;

    w = EDGE + PAD + ICON_SZ + PAD + textW + PAD + EDGE;
    if (w < 280) w = 280;
    h = EDGE + TITLE_H + PAD + textH + PAD + BTN_H + PAD + EDGE;

    g_textRc.left   = EDGE + PAD + ICON_SZ + PAD;
    g_textRc.top    = EDGE + TITLE_H + PAD;
    g_textRc.right  = g_textRc.left + textW;
    g_textRc.bottom = g_textRc.top + textH;

    g_okRc.left   = (w - BTN_W) / 2;
    g_okRc.top    = h - EDGE - PAD - BTN_H;
    g_okRc.right  = g_okRc.left + BTN_W;
    g_okRc.bottom = g_okRc.top + BTN_H;

    work.left = work.top = 0;
    work.right = GetSystemMetrics(SM_CXSCREEN);
    work.bottom = GetSystemMetrics(SM_CYSCREEN);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    x = work.left + (work.right - work.left - w) / 2;
    y = work.top + (work.bottom - work.top - h) / 2;

    SetWindowPos(g_wnd, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_wnd, NULL, TRUE);
    SetTimer(g_wnd, 1, SCOLD_TIMEOUT_MS, NULL);
}
