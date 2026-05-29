/*
 * audio.c — DirectMusic 8 BGM backend.
 *
 * Mirrors FUN_00498ef4 (init) + FUN_00499200 (track-swap) for the BGM
 * path. See header for scope notes.
 *
 * Engine vtable offsets used (verified against mingw-w64 dmusici.h):
 *
 *   IDirectMusicPerformance8:
 *     0x14  Stop                  (5)
 *     0xb0  InitAudio             (44)
 *     0xb4  PlaySegmentEx         (45)
 *     0xc4  CreateStandardAudioPath (49)
 *
 *   IDirectMusicLoader8:
 *     0x14  SetSearchDirectory    (5)
 *     0x38  LoadObjectFromFile    (14)
 *
 *   IDirectMusicSegment8:
 *     0x18  SetRepeats            (6)
 *     0x4c  SetParam              (19)   — EN-build extra call; never
 *                                          fires at boot (DAT_0438b170=0)
 *     0x74  Download              (29)
 *
 * Engine quirks faithfully reproduced (or intentionally skipped):
 *
 *   #46: SetSearchDirectory is given the *current working directory*
 *        wide-char. That works fine for our dev workflow where the exe
 *        is launched from inside `vendor/original/`. If the user ever
 *        runs from a different cwd, BGM files won't load — same as
 *        the engine.
 *   #47: All 21 segments are preloaded into RAM (~277 MB total). Lazy
 *        loading would cut boot RAM substantially but the engine
 *        chooses eager-load; we preserve it.
 *
 * COBJMACROS + CINTERFACE → C-style vtable accessors:
 *
 *     IDirectMusicPerformance8_PlaySegmentEx(perf, src, ...)
 *
 * is the same as the C++ form `perf->PlaySegmentEx(src, ...)`.
 */

#include "audio.h"
#include "audio_fade.h"
#include "audio_se_names.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ─── BGM filename table (extracted from .data via tools/analyze/pe.py) ───
 *
 *   tools/analyze/pe.py str $(seq 0x005d1970 0x14 0x005d1ab8) [...]
 *
 * 21 entries. Pure data; lives outside the _WIN32 guard so tests can
 * verify the table directly.
 */
const char *const audio_bgm_filenames[AUDIO_BGM_TRACK_COUNT] = {
    "bgm/retitle2010.wav",   /*  0 — title screen */
    "bgm/town.wav",          /*  1 — town / shop idle */
    "bgm/sougen.wav",        /*  2 — grasslands */
    "bgm/cave.wav",          /*  3 */
    "bgm/forest.wav",        /*  4 */
    "bgm/ruins.wav",         /*  5 */
    "bgm/boss.wav",          /*  6 */
    "bgm/over.wav",          /*  7 — pause modal / game over */
    "bgm/open.wav",          /*  8 — shop open jingle (loops?) */
    "bgm/close.wav",         /*  9 — shop close */
    "bgm/treasure.wav",      /* 10 — one-shot chest-open jingle */
    "bgm/fanfare.wav",       /* 11 — one-shot quest-cleared jingle */
    "bgm/ed.wav",            /* 12 — ending */
    "bgm/clear.wav",         /* 13 — one-shot stage-clear */
    "bgm/night02.wav",       /* 14 */
    "bgm/rival.wav",         /* 15 */
    "bgm/lastboss02.wav",    /* 16 */
    "bgm/lastd01.wav",       /* 17 */
    "bgm/feaver.wav",        /* 18 — "fever" sale music */
    "bgm/staff.wav",         /* 19 — one-shot staff credits */
    "bgm/water.wav",         /* 20 */
};

int audio_is_one_shot_track(int track)
{
    /* Indices 10, 11, 13, 19. Engine source: the
     * `(iVar5 == 0x28 || 0x2c || 0x34 || 0x4c)` test in FUN_00498ef4.
     * iVar5 is the byte offset (track * 4), so the indices are
     * 0x28/4=10, 0x2c/4=11, 0x34/4=13, 0x4c/4=19. */
    return track == 10 || track == 11 || track == 13 || track == 19;
}

const char *audio_bgm_filename(int track)
{
    if (track < 0 || track >= AUDIO_BGM_TRACK_COUNT) return NULL;
    return audio_bgm_filenames[track];
}

/* ─── audio-trace JSONL emitter ─────────────────────────────────────────
 *
 * See audio.h for the schema + rationale. Lives outside the _WIN32
 * guard so tests can exercise it without windows.h. The boot-clock
 * anchor uses timeGetTime() on Win32 (matches the engine's audio
 * clock) and stays at 0 in the test build.
 */

static FILE    *g_audio_trace_fp      = NULL;
static uint32_t g_audio_trace_boot_ms = 0;

/* timeGetTime is in winmm; declared in the _WIN32 backend block below. */
#ifdef _WIN32
static uint32_t audio_trace_platform_now_ms(void);   /* forward */
#else
static uint32_t audio_trace_platform_now_ms(void) { return 0; }
#endif

static uint32_t audio_trace_now_ms(void)
{
    return audio_trace_platform_now_ms() - g_audio_trace_boot_ms;
}

int audio_trace_open(const char *path)
{
    if (!path) return 0;
    if (g_audio_trace_fp) {
        fclose(g_audio_trace_fp);
        g_audio_trace_fp = NULL;
    }
    /* Append mode so re-opens during a run don't truncate prior data;
     * the smoke harness opens a fresh path per run anyway. */
    g_audio_trace_fp = fopen(path, "a");
    if (!g_audio_trace_fp) return 0;
    g_audio_trace_boot_ms = audio_trace_platform_now_ms();
    return 1;
}

void audio_trace_close(void)
{
    if (g_audio_trace_fp) {
        fflush(g_audio_trace_fp);
        fclose(g_audio_trace_fp);
        g_audio_trace_fp = NULL;
    }
}

int audio_trace_is_open(void)
{
    return g_audio_trace_fp != NULL;
}

size_t audio_trace_json_escape(const char *src, char *dst, size_t cap)
{
    if (!dst || cap == 0) return 0;
    size_t o = 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    /* Reserve one byte for the terminator. */
    size_t budget = cap - 1;
    for (size_t i = 0; src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        const char *e = NULL;
        if      (c == '"')  e = "\\\"";
        else if (c == '\\') e = "\\\\";
        else if (c == '\n') e = "\\n";
        else if (c == '\r') e = "\\r";
        else if (c == '\t') e = "\\t";

        if (e) {
            if (o + 2 > budget) break;
            dst[o++] = e[0];
            dst[o++] = e[1];
        } else if (c < 0x20 || c >= 0x7f) {
            /* \uXXXX is 6 bytes. */
            if (o + 6 > budget) break;
            /* Hand-roll instead of snprintf to keep the hot path tiny. */
            static const char hex[] = "0123456789abcdef";
            dst[o++] = '\\';
            dst[o++] = 'u';
            dst[o++] = '0';
            dst[o++] = '0';
            dst[o++] = hex[(c >> 4) & 0xf];
            dst[o++] = hex[c & 0xf];
        } else {
            if (o + 1 > budget) break;
            dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
    return o;
}

void audio_trace_emit_bgm_swap(int track, const char *name)
{
    if (!g_audio_trace_fp) return;
    char esc[512];
    audio_trace_json_escape(name ? name : "", esc, sizeof esc);
    fprintf(g_audio_trace_fp,
            "{\"t_ms\":%u,\"kind\":\"bgm_swap\",\"track\":%d,\"name\":\"%s\"}\n",
            (unsigned)audio_trace_now_ms(), track, esc);
    fflush(g_audio_trace_fp);
}

void audio_trace_emit_se_play(int slot, const char *name)
{
    if (!g_audio_trace_fp) return;
    char esc[512];
    audio_trace_json_escape(name ? name : "", esc, sizeof esc);
    fprintf(g_audio_trace_fp,
            "{\"t_ms\":%u,\"kind\":\"se_play\",\"slot\":%d,\"name\":\"%s\"}\n",
            (unsigned)audio_trace_now_ms(), slot, esc);
    fflush(g_audio_trace_fp);
}

void audio_trace_emit_fade_start(int channel, int slider, int32_t centibel)
{
    if (!g_audio_trace_fp) return;
    fprintf(g_audio_trace_fp,
            "{\"t_ms\":%u,\"kind\":\"fade_start\",\"channel\":%d,"
            "\"slider\":%d,\"centibel\":%d}\n",
            (unsigned)audio_trace_now_ms(), channel, slider, (int)centibel);
    fflush(g_audio_trace_fp);
}

static int audio_play_se_win32(int slot);   /* forward — defined in the _WIN32 block */

int audio_play_se(int slot)
{
    if (slot < 0 || slot >= AUDIO_SE_COUNT) return 0;

    /* Apply SE-A slider via SetVolume on path_se_a (Win32) and emit a
     * `fade_start` trace event. The engine call site is at the top of
     * FUN_00499c63, before the explicit Stop + PlaySegmentEx; we mirror
     * that ordering so analyse-time the fade_start lands immediately
     * before the matching se_play. Pure-C: no-op on non-Win32 except
     * for the trace emission (the apply hook is NULL). */
    audio_fade_apply(AUDIO_FADE_CHANNEL_SE_A);

    /* Trace event fires even when the Win32 backend isn't wired —
     * tests can drive this without windows.h. */
    char name[32];
    snprintf(name, sizeof name, "se_%03d_id%04x",
             slot, audio_se_resource_ids[slot]);
    audio_trace_emit_se_play(slot, name);

#ifdef _WIN32
    return audio_play_se_win32(slot);
#else
    return 1;
#endif
}

int audio_play_se_by_id(uint16_t id)
{
    const int slot = audio_se_slot_for_id(id);
    if (slot < 0) return 0;
    return audio_play_se(slot);
}

/* ─── Win32 / DirectMusic 8 backend ────────────────────────────────── */

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <objbase.h>
#include <dmusici.h>
#include <mmsystem.h>
#include <stdio.h>

#include "music.h"    /* g_music_swap_fn — bridge into music.c */
#include "se_pack.h"  /* runtime SE cache (no embedded proprietary audio) */

static uint32_t audio_trace_platform_now_ms(void)
{
    return (uint32_t)timeGetTime();
}

/* Engine globals (see music.h comment block for the original DAT_*
 * names). Carried here as a single struct for tidiness. */
static struct {
    IDirectMusicPerformance8  *performance;       /* DAT_09643100 */
    IDirectMusicLoader8       *loader;            /* DAT_09643104 */
    IDirectMusicAudioPath     *path_bgm;          /* DAT_09643108 */
    IDirectMusicAudioPath     *path_se_a;         /* DAT_0964310c */
    IDirectMusicAudioPath     *path_se_b;         /* DAT_09643110 — dead in vendor data, created for engine fidelity */
    IDirectMusicSegment8      *segments[AUDIO_BGM_TRACK_COUNT];        /* DAT_09643038[] */
    IDirectMusicSegmentState  *segment_states[AUDIO_BGM_TRACK_COUNT];  /* DAT_09642e24[] */
    IDirectMusicSegment8      *se_segments[AUDIO_SE_COUNT];            /* DAT_09642e7c[] */
    IDirectMusicSegmentState8 *se_states[AUDIO_SE_COUNT];              /* DAT_09642c6c[] (QI-upgraded) */
    int                        se_loaded_count;
    int32_t                    current_track;     /* DAT_005d1960 (mirror) */
    int                        com_initialized;
    int                        all_loaded;        /* DAT_096430fc */
} g_audio;

/* music.c installs a swap function pointer — audio_play_track is the
 * one we hand it. Adapter to keep music.c free of windows.h. */
static void audio_play_track_adapter(int32_t track)
{
    (void)audio_play_track(track);
}

/* audio_fade.c invokes this when audio_fade_apply fires; we route the
 * centibel to the right AudioPath's SetVolume. Mirrors the engine's
 * `(*vt[5])(path, centibel, 0)` call at vtable +0x14 in FUN_00499583
 * (BGM) and FUN_00499c63 (SE). */
static void audio_fade_apply_hook_win32(int channel, int32_t centibel)
{
    IDirectMusicAudioPath *path = NULL;
    switch (channel) {
    case AUDIO_FADE_CHANNEL_BGM:   path = g_audio.path_bgm;  break;
    case AUDIO_FADE_CHANNEL_SE_A:  path = g_audio.path_se_a; break;
    case AUDIO_FADE_CHANNEL_SE_B:  path = g_audio.path_se_b; break;
    default: return;
    }
    if (!path) return;
    IDirectMusicAudioPath_SetVolume(path, (long)centibel, 0);
}

void silent_audio_apply_hook(int channel, int32_t centibel)
{
    (void)centibel;
    IDirectMusicAudioPath *path = NULL;
    switch (channel) {
    case AUDIO_FADE_CHANNEL_BGM:   path = g_audio.path_bgm;  break;
    case AUDIO_FADE_CHANNEL_SE_A:  path = g_audio.path_se_a; break;
    case AUDIO_FADE_CHANNEL_SE_B:  path = g_audio.path_se_b; break;
    default: return;
    }
    if (!path) return;
    IDirectMusicAudioPath_SetVolume(path, (long)AUDIO_FADE_SILENCE_CENTIBEL, 0);
}

/* Wide-char copy of an ASCII string into a fixed-size WCHAR buffer.
 * Used for the cwd path and the BGM filenames going into the Loader.
 * Mirrors the engine's MultiByteToWideChar(CP_ACP) calls. */
static int audio_a_to_w(const char *src, WCHAR *dst, int dst_cap_w)
{
    int n = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, dst_cap_w);
    return n > 0;
}

int audio_init(HWND hwnd)
{
    memset(&g_audio, 0, sizeof g_audio);
    g_audio.current_track = -1;

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
        fprintf(stderr, "audio: CoInitialize failed (hr=0x%08lx)\n", (unsigned long)hr);
        return 0;
    }
    g_audio.com_initialized = 1;

    /* 1. Create the Performance. */
    hr = CoCreateInstance(&CLSID_DirectMusicPerformance, NULL,
                          CLSCTX_INPROC_SERVER,
                          &IID_IDirectMusicPerformance8,
                          (void **)&g_audio.performance);
    if (FAILED(hr) || !g_audio.performance) {
        fprintf(stderr, "audio: CoCreateInstance(Performance) failed (hr=0x%08lx)\n",
                (unsigned long)hr);
        return 0;
    }

    /* 2. InitAudio. Engine args: (NULL, NULL, hwnd, DMUS_APATH_DYNAMIC_STEREO,
     *    64 channels, DMUS_AUDIOF_ALL, NULL). */
    hr = IDirectMusicPerformance8_InitAudio(
        g_audio.performance,
        NULL, NULL, hwnd,
        DMUS_APATH_DYNAMIC_STEREO, 64, DMUS_AUDIOF_ALL,
        NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "audio: InitAudio failed (hr=0x%08lx)\n", (unsigned long)hr);
        return 0;
    }

    /* 3. Create the three AudioPaths the engine carries: BGM, SE-A, SE-B.
     *    Per engine-quirks #46 the SE-B path is dead at runtime because
     *    every SE table entry has channel_flag=0 → SE-A only. We still
     *    create both for fidelity (and so a future modded build with a
     *    populated +4 column would Just Work). */
    hr = IDirectMusicPerformance8_CreateStandardAudioPath(
        g_audio.performance,
        DMUS_APATH_DYNAMIC_STEREO, 64, TRUE,
        &g_audio.path_bgm);
    if (FAILED(hr)) {
        fprintf(stderr, "audio: CreateStandardAudioPath(BGM) failed (hr=0x%08lx)\n",
                (unsigned long)hr);
        return 0;
    }
    hr = IDirectMusicPerformance8_CreateStandardAudioPath(
        g_audio.performance,
        DMUS_APATH_DYNAMIC_STEREO, 64, TRUE,
        &g_audio.path_se_a);
    if (FAILED(hr)) {
        fprintf(stderr, "audio: CreateStandardAudioPath(SE-A) failed (hr=0x%08lx)\n",
                (unsigned long)hr);
        return 0;
    }
    hr = IDirectMusicPerformance8_CreateStandardAudioPath(
        g_audio.performance,
        DMUS_APATH_DYNAMIC_STEREO, 64, TRUE,
        &g_audio.path_se_b);
    if (FAILED(hr)) {
        fprintf(stderr, "audio: CreateStandardAudioPath(SE-B) failed (hr=0x%08lx)\n",
                (unsigned long)hr);
        return 0;
    }

    /* 4. Create the Loader. */
    hr = CoCreateInstance(&CLSID_DirectMusicLoader, NULL,
                          CLSCTX_INPROC_SERVER,
                          &IID_IDirectMusicLoader8,
                          (void **)&g_audio.loader);
    if (FAILED(hr) || !g_audio.loader) {
        fprintf(stderr, "audio: CoCreateInstance(Loader) failed (hr=0x%08lx)\n",
                (unsigned long)hr);
        return 0;
    }

    /* 5. SetSearchDirectory(cwd). Engine passes GUID_DirectMusicAllTypes
     *    so the search dir applies to every object class. */
    char cwd_a[MAX_PATH];
    DWORD cwd_len = GetCurrentDirectoryA(MAX_PATH, cwd_a);
    if (cwd_len == 0 || cwd_len >= MAX_PATH) {
        fprintf(stderr, "audio: GetCurrentDirectoryA failed (len=%lu)\n",
                (unsigned long)cwd_len);
        return 0;
    }
    WCHAR cwd_w[MAX_PATH];
    if (!audio_a_to_w(cwd_a, cwd_w, MAX_PATH)) {
        fprintf(stderr, "audio: MultiByteToWideChar(cwd) failed\n");
        return 0;
    }
    hr = IDirectMusicLoader8_SetSearchDirectory(
        g_audio.loader, &GUID_DirectMusicAllTypes, cwd_w, FALSE);
    if (FAILED(hr)) {
        fprintf(stderr, "audio: SetSearchDirectory failed (hr=0x%08lx)\n",
                (unsigned long)hr);
        return 0;
    }

    /* 6. For each BGM file: LoadObjectFromFile → SetRepeats → Download.
     *    Engine loops 0..0x54 in 4-byte strides; we do the same 21 iters. */
    int loaded = 0;
    for (int i = 0; i < AUDIO_BGM_TRACK_COUNT; i++) {
        const char *fname = audio_bgm_filenames[i];
        WCHAR fname_w[MAX_PATH];
        if (!audio_a_to_w(fname, fname_w, MAX_PATH)) continue;

        hr = IDirectMusicLoader8_LoadObjectFromFile(
            g_audio.loader,
            &CLSID_DirectMusicSegment,
            &IID_IDirectMusicSegment8,
            fname_w,
            (void **)&g_audio.segments[i]);
        if (FAILED(hr) || !g_audio.segments[i]) {
            fprintf(stderr, "audio: LoadObjectFromFile(%s) failed (hr=0x%08lx)\n",
                    fname, (unsigned long)hr);
            /* Engine returns 0 here. We mirror — partial init is
             * unrecoverable: caller treats this as a hard failure. */
            return 0;
        }

        /* Engine: looping tracks get SetRepeats(0xffffffff); one-shot
         * jingles (treasure/fanfare/clear/staff) get 0. */
        DWORD repeats = audio_is_one_shot_track(i) ? 0u : 0xffffffffu;
        hr = IDirectMusicSegment8_SetRepeats(g_audio.segments[i], repeats);
        if (FAILED(hr)) {
            fprintf(stderr, "audio: SetRepeats(%s) failed (hr=0x%08lx)\n",
                    fname, (unsigned long)hr);
            return 0;
        }

        /* Engine: Download(performance) — preloads waves into the
         *    output buffers so PlaySegmentEx doesn't stall on first play. */
        hr = IDirectMusicSegment8_Download(
            g_audio.segments[i], (IUnknown *)g_audio.performance);
        if (FAILED(hr)) {
            fprintf(stderr, "audio: Download(%s) failed (hr=0x%08lx)\n",
                    fname, (unsigned long)hr);
            /* Non-fatal in the engine — Download failure just means
             * dynamic-load fallback. Continue. */
        }
        loaded++;
    }

    /* ── SE resource-load loop ───────────────────────────────────────────
     * Mirrors FUN_00498ef4's second loop, but sources the WAV blobs from
     * the runtime SE cache (se_pack) instead of resources embedded in our
     * own exe — we ship no proprietary audio. se_pack_acquire() extracts
     * the 110 `WAVE` resources from the user's retail recettear.exe on
     * first run and caches them (docs/formats/se-pack.md). Each blob then
     * feeds IDirectMusicLoader::GetObject via a memory-backed
     * DMUS_OBJECTDESC, exactly as the original engine does after its own
     * FindResource/LockResource.
     *
     * If the retail exe can't be found, se_pack_acquire() fails; we skip
     * SE entirely (every slot stays NULL → audio_play_se no-ops), which
     * is the same graceful degradation as a missing resource.
     *
     * Slot 2's ID 0x0135 is in the table but has no `WAVE` resource even
     * in retail — se_pack records it as an empty (size-0) slot and we
     * leave its segment NULL. audio_play_se(2) silently no-ops. */
    int se_loaded = 0;
    int se_missing = 0;
    int se_have = (se_pack_acquire() == 0);
    if (!se_have)
        fprintf(stderr,
                "audio: SE cache unavailable — sound effects disabled\n");
    for (int slot = 0; se_have && slot < AUDIO_SE_COUNT; slot++) {
        uint16_t rid = audio_se_resource_ids[slot];
        const se_blob_t *be = se_pack_blob(slot);
        const void *blob = be ? be->data : NULL;
        uint32_t    size = be ? be->size : 0;
        if (!blob || !size) {
            se_missing++;
            continue;
        }

        /* DMUS_OBJECTDESC: load a Segment from the in-memory WAV blob.
         * dwValidData = DMUS_OBJ_CLASS | DMUS_OBJ_MEMORY (0x402)
         * dwSize must be sizeof(DMUS_OBJECTDESC) = 0x350. */
        DMUS_OBJECTDESC desc;
        memset(&desc, 0, sizeof desc);
        desc.dwSize       = sizeof desc;
        desc.dwValidData  = DMUS_OBJ_CLASS | DMUS_OBJ_MEMORY;
        desc.guidClass    = CLSID_DirectMusicSegment;
        desc.pbMemData    = (BYTE *)blob;
        desc.llMemLength  = (LONGLONG)size;

        hr = IDirectMusicLoader8_GetObject(
            g_audio.loader, &desc,
            &IID_IDirectMusicSegment8,
            (void **)&g_audio.se_segments[slot]);
        if (FAILED(hr) || !g_audio.se_segments[slot]) {
            fprintf(stderr,
                    "audio: SE GetObject(slot=%d id=0x%04x) failed "
                    "(hr=0x%08lx)\n",
                    slot, rid, (unsigned long)hr);
            g_audio.se_segments[slot] = NULL;
            se_missing++;
            continue;
        }

        hr = IDirectMusicSegment8_Download(
            g_audio.se_segments[slot], (IUnknown *)g_audio.performance);
        /* Same engine policy as BGM: Download failure is non-fatal. */
        (void)hr;
        se_loaded++;
    }
    g_audio.se_loaded_count = se_loaded;

    g_audio.all_loaded = 1;

    /* Install the fade-apply hook so audio_fade_apply() lands real
     * IDirectMusicAudioPath::SetVolume calls. Sliders default to 9
     * (full target) — see audio_fade.h header comment for why we
     * don't mirror the engine's BGM=5 default. */
    audio_fade_set_apply_hook(audio_fade_apply_hook_win32);

    /* Bridge into music.c: install our swap callback. From here on
     * every selector swap_call_count++ also triggers a real play. */
    g_music_swap_fn = audio_play_track_adapter;

    fprintf(stderr,
            "audio: init ok — %d BGM segments + %d/%d SE segments preloaded"
            " (%d missing/skipped)\n",
            loaded, se_loaded, AUDIO_SE_COUNT, se_missing);
    return 1;
}

int audio_play_track(int32_t track)
{
    if (!g_audio.all_loaded || !g_audio.performance) return 0;

    /* Engine guard #1: DAT_096430fc != 0 → check covered above. */
    /* Engine guard #2: DAT_005d1960 != param_1 → no-op if same track. */
    if (track == g_audio.current_track) return 1;

    /* Engine STOP path: param_1 == -2 → Stop(NULL,NULL,0,0). The
     * engine also assembles a debug-log path string via FUN_005038ff
     * + sprintf; we don't have a debug logger. */
    if (track == AUDIO_TRACK_STOP || track == -1) {
        HRESULT hr = IDirectMusicPerformance8_Stop(
            g_audio.performance, NULL, NULL, 0, 0);
        g_audio.current_track = -1;
        return SUCCEEDED(hr);
    }

    if (track < 0 || track >= AUDIO_BGM_TRACK_COUNT) {
        fprintf(stderr, "audio: play_track: bad index %d\n", (int)track);
        return 0;
    }
    IDirectMusicSegment8 *seg = g_audio.segments[track];
    if (!seg) {
        fprintf(stderr, "audio: play_track: segment %d not loaded\n", (int)track);
        return 0;
    }

    /* Engine stops the currently-playing segment-state explicitly before
     * the new PlaySegmentEx (via segment-state->Release on the stored
     * DAT_09642e24[idx]). PlaySegmentEx will Stop the previous segment
     * implicitly when the new one is on the same AudioPath, so we just
     * release any prior segment-state to avoid leaks. */
    int32_t prev = g_audio.current_track;
    if (prev >= 0 && prev < AUDIO_BGM_TRACK_COUNT && g_audio.segment_states[prev]) {
        IDirectMusicSegmentState_Release(g_audio.segment_states[prev]);
        g_audio.segment_states[prev] = NULL;
    }

    g_audio.current_track = track;

    /* Engine: FUN_00499200 calls FUN_00499583 (volume-apply) gated on
     * `DAT_09643114 == 0` — i.e. when no per-tick fade animation is
     * currently in progress. We don't yet drive the per-tick animation
     * (DAT_09643114 stays 0), so always apply the current BGM slider
     * here. The music_step fade-band tail will eventually feed a
     * dynamic target into this same hook. */
    audio_fade_apply(AUDIO_FADE_CHANNEL_BGM);

    IDirectMusicSegmentState *new_state = NULL;
    HRESULT hr = IDirectMusicPerformance8_PlaySegmentEx(
        g_audio.performance,
        (IUnknown *)seg,
        NULL,                       /* segment name (unused) */
        NULL,                       /* transition */
        DMUS_SEGF_DEFAULT,
        0,                          /* start time = now */
        &new_state,
        NULL,                       /* from */
        (IUnknown *)g_audio.path_bgm);
    if (FAILED(hr)) {
        fprintf(stderr, "audio: PlaySegmentEx(track=%d) failed (hr=0x%08lx)\n",
                (int)track, (unsigned long)hr);
        return 0;
    }
    g_audio.segment_states[track] = new_state;

    /* Opt-in JSONL trace. No-op if --audio-trace wasn't set. */
    audio_trace_emit_bgm_swap(track, audio_bgm_filenames[track]);
    return 1;
}

/* ── audio_play_se Win32 body — mirrors FUN_00499c63's bare path ─────
 *
 * Engine FUN_00499c63 is gated by the table's +4 channel_flag column,
 * which is all-zero in vendor data (engine-quirks #46). So the only
 * branches that ever fire in practice are:
 *
 *   1. Guard: if (DAT_096430fc == 0 || se_segments[slot] == NULL) return 0
 *   2. Release any prior SegmentState8 stored at DAT_09642c6c[slot]
 *   3. SetVolume on path_se_a per the cos-curve, reading DAT_056e5774
 *      (the SE-A slider). We route this through audio_fade_apply.
 *   4. Stop any prior playback of THIS segment on the performance
 *      (engine emits an extra explicit Stop before the new PlaySegmentEx
 *       to defeat queueing for repeat-same-slot triggers — see Q-D)
 *   5. PlaySegmentEx(seg, NULL, NULL, DMUS_SEGF_QUEUE=0x80, 0, &state,
 *                    NULL, path_se_a)
 *   6. state->QueryInterface(IID_IDirectMusicSegmentState8, &se_states[slot])
 *   7. state->Release()
 */
static int audio_play_se_win32(int slot)
{
    if (!g_audio.all_loaded || !g_audio.performance) return 0;

    IDirectMusicSegment8 *seg = g_audio.se_segments[slot];
    if (!seg) return 0;     /* slot 2 / missing-resource case */

    /* Release the prior SegmentState8 from the last play on this slot.
     * Storing the QI-upgraded type matches the engine — see audio.h
     * and engine-quirks #46 for why we Q.I. instead of holding the
     * raw SegmentState pointer. */
    if (g_audio.se_states[slot]) {
        IDirectMusicSegmentState8_Release(g_audio.se_states[slot]);
        g_audio.se_states[slot] = NULL;
    }

    /* Engine: explicit Stop on this segment before PlaySegmentEx.
     * Defeats DMUS_SEGF_QUEUE's same-path queueing when the player
     * re-triggers the same slot in quick succession.
     * Stop's first segment arg is typed as IDirectMusicSegment* — pass
     * the un-upgraded interface (Segment8 is a Segment by inheritance). */
    IDirectMusicPerformance8_Stop(
        g_audio.performance, (IDirectMusicSegment *)seg, NULL, 0, 0);

    IDirectMusicSegmentState *state = NULL;
    /* dwFlags = DMUS_SEGF_QUEUE (0x80) — engine fidelity. Queues the
     * new segment back-to-back on the same AudioPath; with the
     * explicit Stop just above the queue is always empty so this fires
     * immediately. BGM lives on a separate AudioPath so SE queueing
     * doesn't preempt it. */
    HRESULT hr = IDirectMusicPerformance8_PlaySegmentEx(
        g_audio.performance,
        (IUnknown *)seg,
        NULL,                       /* segment name (unused) */
        NULL,                       /* transition */
        DMUS_SEGF_QUEUE,            /* engine value at FUN_00499c63 +0xb4 call */
        0,                          /* start time = now */
        &state,
        NULL,                       /* from */
        (IUnknown *)g_audio.path_se_a);
    if (FAILED(hr) || !state) {
        fprintf(stderr,
                "audio: PlaySegmentEx(SE slot=%d) failed (hr=0x%08lx)\n",
                slot, (unsigned long)hr);
        return 0;
    }

    /* QI-upgrade SegmentState → SegmentState8 (engine fidelity). The
     * upgraded interface adds GetObjectInPath / SetTrackConfig that
     * we don't use today, but storing the upgraded type matches what
     * the engine writes into DAT_09642c6c[]. Release the un-upgraded
     * pointer once the upgrade lands. */
    IDirectMusicSegmentState_QueryInterface(
        state, &IID_IDirectMusicSegmentState8,
        (void **)&g_audio.se_states[slot]);
    IDirectMusicSegmentState_Release(state);
    return 1;
}

void audio_shutdown(void)
{
    /* Detach the music bridge first so any in-flight tick can't reach
     * back into a half-torn-down backend. Also drop the fade-apply
     * hook so a stray audio_fade_apply caller doesn't reach into a
     * torn-down g_audio. */
    g_music_swap_fn = NULL;
    audio_fade_set_apply_hook(NULL);

    if (g_audio.performance) {
        /* Stop whatever's playing. CloseDown shuts down the performance
         * synth, releases ports, etc. — same as the engine's shutdown
         * (which we haven't ported yet but will need eventually). */
        IDirectMusicPerformance8_Stop(g_audio.performance, NULL, NULL, 0, 0);
        IDirectMusicPerformance8_CloseDown(g_audio.performance);
    }
    for (int i = 0; i < AUDIO_BGM_TRACK_COUNT; i++) {
        if (g_audio.segment_states[i]) {
            IDirectMusicSegmentState_Release(g_audio.segment_states[i]);
            g_audio.segment_states[i] = NULL;
        }
        if (g_audio.segments[i]) {
            IDirectMusicSegment8_Unload(g_audio.segments[i],
                                        (IUnknown *)g_audio.performance);
            IDirectMusicSegment8_Release(g_audio.segments[i]);
            g_audio.segments[i] = NULL;
        }
    }
    for (int i = 0; i < AUDIO_SE_COUNT; i++) {
        if (g_audio.se_states[i]) {
            IDirectMusicSegmentState8_Release(g_audio.se_states[i]);
            g_audio.se_states[i] = NULL;
        }
        if (g_audio.se_segments[i]) {
            IDirectMusicSegment8_Unload(g_audio.se_segments[i],
                                        (IUnknown *)g_audio.performance);
            IDirectMusicSegment8_Release(g_audio.se_segments[i]);
            g_audio.se_segments[i] = NULL;
        }
    }
    if (g_audio.path_se_b) {
        IDirectMusicAudioPath_Release(g_audio.path_se_b);
        g_audio.path_se_b = NULL;
    }
    if (g_audio.path_se_a) {
        IDirectMusicAudioPath_Release(g_audio.path_se_a);
        g_audio.path_se_a = NULL;
    }
    if (g_audio.path_bgm) {
        IDirectMusicAudioPath_Release(g_audio.path_bgm);
        g_audio.path_bgm = NULL;
    }
    if (g_audio.loader) {
        IDirectMusicLoader8_Release(g_audio.loader);
        g_audio.loader = NULL;
    }
    if (g_audio.performance) {
        IDirectMusicPerformance8_Release(g_audio.performance);
        g_audio.performance = NULL;
    }
    if (g_audio.com_initialized) {
        CoUninitialize();
        g_audio.com_initialized = 0;
    }
    g_audio.all_loaded = 0;
    g_audio.current_track = -1;
}

#endif /* _WIN32 */
