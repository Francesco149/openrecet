/*
 * movie.c — see movie.h.  Line refs are retail VAs.
 */
#include "movie.h"

#include "audio_fade.h"          /* DAT_056e5778 (the BGM slider) */
#include "music.h"               /* DAT_005d1960 / _DAT_005d1580 */
#include "scene_title.h"         /* DAT_09643518 */
#include "x87.h"

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

static IGraphBuilder *s_graph;   /* ppv          */
static IMediaControl *s_ctl;     /* DAT_006483ac */
static IVideoWindow  *s_win;     /* DAT_006483b0 */
static IMediaEvent   *s_evt;     /* DAT_006483b4 */
static IBasicAudio   *s_audio;   /* DAT_006483b8 */
static HWND s_owner;
#endif
static int32_t s_movie_mode;     /* DAT_006483bc: 1 = the movie, 0 = a BGM stream */
static int s_silent;

void movie_set_owner(void *hwnd)
{
#ifdef _WIN32
    s_owner = (HWND)hwnd;
#else
    (void)hwnd;
#endif
}

void movie_set_silent(int on) { s_silent = on; }

void movie_release(void)
{
#ifdef _WIN32
    if (s_ctl) IMediaControl_Stop(s_ctl);
    if (s_win) IVideoWindow_put_Visible(s_win, OAFALSE);
    if (s_audio) { IBasicAudio_Release(s_audio); s_audio = NULL; }
    if (s_evt) { IMediaEvent_Release(s_evt); s_evt = NULL; }
    if (s_win) { IVideoWindow_Release(s_win); s_win = NULL; }
    if (s_ctl) { IMediaControl_Release(s_ctl); s_ctl = NULL; }
    if (s_graph) { IGraphBuilder_Release(s_graph); s_graph = NULL; }
#endif
}

int movie_open(const char *file)
{
    movie_release();
    s_movie_mode = 1;
#ifdef _WIN32
    /* retail's COM is up from the DirectMusic init; the port's may not be
     * (--silent-audio skips it) */
    CoInitialize(NULL);
    if (CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IGraphBuilder,
                         (void **)&s_graph) >= 0 &&
        IGraphBuilder_QueryInterface(s_graph, &IID_IMediaControl, (void **)&s_ctl) >= 0 &&
        IGraphBuilder_QueryInterface(s_graph, &IID_IVideoWindow, (void **)&s_win) >= 0 &&
        IGraphBuilder_QueryInterface(s_graph, &IID_IMediaEvent, (void **)&s_evt) >= 0) {
        /* FUN_0047aa3b: retail's directory is the exe's; the port reads its
         * game data from the working directory */
        char path[512], full[1024];
        WCHAR wide[260];
        snprintf(path, sizeof path, "%s", file);
        if (GetFullPathNameA(path, sizeof full, full, NULL) == 0)
            snprintf(full, sizeof full, "%s", path);
        MultiByteToWideChar(CP_ACP, 0, full, -1, wide, 260);
        if (IGraphBuilder_RenderFile(s_graph, wide, NULL) >= 0) {
            IVideoWindow_put_Owner(s_win, (OAHWND)s_owner);
            IVideoWindow_put_WindowStyle(s_win, 0x46000000);   /* WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN */
            RECT rc = { 0, 0, 0, 0 };
            GetClientRect(s_owner, &rc);
            IVideoWindow_SetWindowPosition(s_win, rc.left, rc.top, rc.right, rc.bottom);
            IGraphBuilder_QueryInterface(s_graph, &IID_IBasicAudio, (void **)&s_audio);
            if (s_audio) IBasicAudio_put_Volume(s_audio, s_silent ? -10000 : 0);
            if (IMediaControl_Run(s_ctl) >= 0) return 1;
        }
    }
#else
    (void)file;
#endif
    movie_release();
    return 0;
}

int movie_poll(void)
{
#ifdef _WIN32
    static const int32_t k_vol[10] = { -10000, -5000, -2500, -1800, -1300, -1100, -900, -700, -500, -300 };
    if (s_graph == NULL) return 0;
    int32_t sl = audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM);
    if (sl < 0 || sl > 9) sl = 9;
    const int32_t vol = k_vol[sl] + 9600 * x87_ftol((long double)g_music.target_volume - 1.0);
    if (s_audio) IBasicAudio_put_Volume(s_audio, s_silent ? -10000 : vol);
    long ev = 0;
    if (s_evt) IMediaEvent_WaitForCompletion(s_evt, 0, &ev);
    if (s_movie_mode == 0) {
        /* PORT-DEBT(stub, FUN_0040cea6): the BGM stream's loop (FUN_0040cf88,
         * DAT_006483c0) — the port plays its BGM through DirectMusic */
        return 0;
    }
    if (ev == EC_COMPLETE) {
        g_scene_title_anim.frame_counter = 0;   /* DAT_09643518 */
        g_music.current_track = -1;             /* DAT_005d1960 */
        movie_release();
    }
    return 1;
#else
    return 0;
#endif
}
