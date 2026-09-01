/* The alert sound: off the engine thread, on a device that stays open.
 *
 * Two things are wrong with calling PlaySound straight from the scold box.
 * It is slow to start - the call opens the audio device, and Windows powers
 * the endpoint down between sounds, so an alert for a shortcut pressed after
 * a quiet spell arrives about a second late, long after the window it was
 * supposed to accompany. And it blocks: the engine thread is the thread the
 * hook callback runs on, and a callback that overruns LowLevelHooksTimeout
 * gets the hook thrown out, so a second spent inside PlaySound is four times
 * the whole budget.
 *
 * So one worker thread owns everything to do with audio. It reads chord.wav
 * once, keeps a wave device open for the life of the process, and sits on an
 * event. mh_sound_alert() is a SetEvent - it cannot block and cannot fail.
 * Playing is then one waveOutWrite into a device that is already awake.
 *
 * Since Vista the audio engine reroutes open streams when the default device
 * changes, so holding the device open does not pin the sound to whatever was
 * plugged in at boot. */
#include "common.h"
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEEP_ALIVE_MS 20000   /* how often to nudge an idle endpoint */
#define SILENCE_MS      120   /* length of that nudge */

static HANDLE        g_thread, g_wake;
static volatile LONG g_quit, g_play, g_active = 1;

static HWAVEOUT     g_dev;
static WAVEFORMATEX g_fmt;
static BYTE        *g_file;      /* the whole wav, kept for the payload inside */
static BYTE        *g_pcm;       /* points into g_file: the data chunk */
static DWORD        g_pcmLen;
static BYTE        *g_silence;
static DWORD        g_silenceLen;
static WAVEHDR      g_hdr, g_quiet;
static int          g_prepared;

/* ---------------------------------------------------------------- wav --- */

static BYTE *read_file(const wchar_t *path, DWORD *len) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD size, got = 0;
    BYTE *buf;

    if (f == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size < 44 || size > 8u * 1024 * 1024) {
        CloseHandle(f);
        return NULL;
    }
    buf = (BYTE *)malloc(size);
    if (!buf) { CloseHandle(f); return NULL; }
    if (!ReadFile(f, buf, size, &got, NULL) || got != size) {
        free(buf);
        CloseHandle(f);
        return NULL;
    }
    CloseHandle(f);
    *len = size;
    return buf;
}

static DWORD rd32(const BYTE *p) {
    return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) |
           ((DWORD)p[3] << 24);
}

/* RIFF is a flat list of tagged chunks; walk it looking for "fmt " and "data"
   instead of trusting them to sit at the canonical offsets. */
static int parse_wav(BYTE *file, DWORD len) {
    DWORD pos = 12;

    if (len < 12 || memcmp(file, "RIFF", 4) || memcmp(file + 8, "WAVE", 4))
        return 0;

    while (pos + 8 <= len) {
        DWORD size = rd32(file + pos + 4);
        BYTE *body = file + pos + 8;

        if (size > len - pos - 8) size = len - pos - 8;

        if (!memcmp(file + pos, "fmt ", 4) && size >= 16) {
            memset(&g_fmt, 0, sizeof(g_fmt));
            memcpy(&g_fmt, body, size < sizeof(g_fmt) ? size : sizeof(g_fmt));
            g_fmt.cbSize = 0;   /* nothing here needs the trailing extra bytes */
        } else if (!memcmp(file + pos, "data", 4)) {
            g_pcm    = body;
            g_pcmLen = size;
        }
        pos += 8 + size + (size & 1);   /* chunks are word-aligned */
    }
    return g_pcm && g_pcmLen && g_fmt.nChannels && g_fmt.nSamplesPerSec &&
           g_fmt.nBlockAlign;
}

/* The Windows 98 critical stop was chord.wav, and it still ships with
   Windows. Fall back to the current sound scheme if it ever stops. */
static int load_wav(void) {
    wchar_t win[MAX_PATH], path[MAX_PATH];
    DWORD len;

    if (!GetWindowsDirectoryW(win, MAX_PATH)) return 0;
    if (_snwprintf(path, MAX_PATH - 1, L"%s\\Media\\chord.wav", win) <= 0)
        return 0;
    path[MAX_PATH - 1] = 0;

    g_file = read_file(path, &len);
    if (!g_file) return 0;
    if (!parse_wav(g_file, len)) {
        free(g_file);
        g_file = NULL;
        g_pcm  = NULL;
        return 0;
    }

    g_silenceLen = g_fmt.nAvgBytesPerSec / 1000 * SILENCE_MS;
    g_silenceLen -= g_silenceLen % g_fmt.nBlockAlign;
    if (g_silenceLen) g_silence = (BYTE *)calloc(g_silenceLen, 1);
    return 1;
}

/* ------------------------------------------------------------- device --- */

static void close_device(void) {
    if (!g_dev) return;
    waveOutReset(g_dev);
    if (g_prepared) {
        waveOutUnprepareHeader(g_dev, &g_hdr, sizeof(g_hdr));
        if (g_silence) waveOutUnprepareHeader(g_dev, &g_quiet, sizeof(g_quiet));
        g_prepared = 0;
    }
    waveOutClose(g_dev);
    g_dev = NULL;
}

static int open_device(void) {
    if (g_dev) return 1;
    if (!g_pcm) return 0;
    if (waveOutOpen(&g_dev, WAVE_MAPPER, &g_fmt, 0, 0, CALLBACK_NULL)
            != MMSYSERR_NOERROR) {
        g_dev = NULL;
        return 0;
    }

    memset(&g_hdr, 0, sizeof(g_hdr));
    g_hdr.lpData         = (LPSTR)g_pcm;
    g_hdr.dwBufferLength = g_pcmLen;
    waveOutPrepareHeader(g_dev, &g_hdr, sizeof(g_hdr));

    if (g_silence) {
        memset(&g_quiet, 0, sizeof(g_quiet));
        g_quiet.lpData         = (LPSTR)g_silence;
        g_quiet.dwBufferLength = g_silenceLen;
        waveOutPrepareHeader(g_dev, &g_quiet, sizeof(g_quiet));
    }
    g_prepared = 1;
    return 1;
}

static int write_header(WAVEHDR *h) {
    if (h->dwFlags & WHDR_INQUEUE) return 1;   /* already on its way */
    h->dwFlags &= ~WHDR_DONE;
    return waveOutWrite(g_dev, h, sizeof(*h)) == MMSYSERR_NOERROR;
}

static void play_alert(void) {
    if (!open_device()) {
        PlaySoundW(L"SystemHand", NULL, SND_ALIAS | SND_ASYNC);
        return;
    }

    /* A second scold on the heels of the first restarts the sound: letting
       them queue would drift further behind the presses with every one. */
    if (g_hdr.dwFlags & WHDR_INQUEUE) waveOutReset(g_dev);

    if (write_header(&g_hdr)) return;

    /* The device went away under us - a headset unplugged, a driver reset.
       Reopen once; if even that fails, let the sound scheme handle it. */
    close_device();
    if (open_device() && write_header(&g_hdr)) return;
    PlaySoundW(L"SystemHand", NULL, SND_ALIAS | SND_ASYNC);
}

/* Windows suspends an idle endpoint, and waking it costs the better part of a
   second - exactly the delay this file exists to remove. An occasional pinch
   of silence keeps the stream running. */
static void keep_alive(void) {
    if (!g_dev || !g_silence) return;
    if (g_hdr.dwFlags & WHDR_INQUEUE) return;   /* the alert is holding it up */
    write_header(&g_quiet);
}

/* ------------------------------------------------------------- thread --- */

static DWORD WINAPI sound_thread(LPVOID arg) {
    (void)arg;

    /* Both of these take a moment, which is the whole reason they happen here
       and not on the thread that dispatches the hook. */
    load_wav();
    open_device();

    for (;;) {
        /* Timed wait while scolding is on - that timeout is the nudge. With
           nothing to scold about there is nothing to keep awake, so wait for
           a signal and let the audio hardware idle like everyone else. */
        DWORD r = WaitForSingleObject(g_wake, g_active ? KEEP_ALIVE_MS : INFINITE);

        if (g_quit) break;
        if (InterlockedExchange((LONG *)&g_play, 0)) { play_alert(); continue; }
        if (!g_active)          close_device();
        else if (r == WAIT_TIMEOUT) keep_alive();
        else                    open_device();   /* just switched on: warm up */
    }

    close_device();
    return 0;
}

void mh_sound_init(void) {
    if (g_thread) return;
    g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g_wake) return;
    g_thread = CreateThread(NULL, 0, sound_thread, NULL, 0, NULL);
}

void mh_sound_alert(void) {
    if (!g_wake) {   /* the worker never started; at least make a noise */
        PlaySoundW(L"SystemHand", NULL, SND_ALIAS | SND_ASYNC);
        return;
    }
    InterlockedExchange((LONG *)&g_play, 1);
    SetEvent(g_wake);
}

/* Called from the engine thread whenever the rules change. */
void mh_sound_set_active(int on) {
    if (InterlockedExchange((LONG *)&g_active, on ? 1 : 0) == (on ? 1 : 0)) return;
    if (g_wake) SetEvent(g_wake);
}

void mh_sound_stop(void) {
    if (!g_thread) return;
    InterlockedExchange((LONG *)&g_quit, 1);
    SetEvent(g_wake);
    WaitForSingleObject(g_thread, 2000);
    CloseHandle(g_thread);
    CloseHandle(g_wake);
    g_thread = NULL;
    g_wake   = NULL;
}
