/*
 * audio.h — DirectMusic 8 BGM backend (init + track-swap slice).
 *
 * Ports FUN_00498ef4 ("init daoudio ok") + FUN_00499200 (track-swap)
 * for the BGM (background music) path only. SE (sound effects),
 * resource-loaded WAVs, and the sin-based volume animation tail
 * (FUN_00499583 / FUN_00499c63 / FUN_00451874) land in later commits.
 *
 * Engine layout:
 *
 *   - One IDirectMusicPerformance8       (DAT_09643100)
 *   - One IDirectMusicLoader8            (DAT_09643104)
 *   - Three IDirectMusicAudioPath8       (DAT_09643108 = BGM, _10c/_110 = SE)
 *   - 21 IDirectMusicSegment8 slots      (DAT_09643038[0..20])
 *   - 21 IDirectMusicSegmentState8 slots (DAT_09642e24[0..20]) for the
 *     currently-playing segment-state (so the engine can call
 *     Performance::Stop on it cleanly when swapping tracks).
 *
 * This first commit only ports the BGM half (one AudioPath, the 21
 * segments, and the segment-state slots). The selector in music.c calls
 * audio_play_track via a function-pointer the audio module installs
 * into music.c at init time (see audio_install_music_bridge).
 *
 * Engine sources:
 *   FUN_00498ef4 @ 0x498ef4 — full init                         (736 B)
 *   FUN_00499200 @ 0x499200 — swap (stop current, load, play)   (219 B)
 *
 * The 21 BGM filenames come from the .data pointer table at 0x005d190c
 * — extracted via `tools/analyze/pe.py str` and hardcoded here as
 * `audio_bgm_filenames[]`. Track indices are stable across builds
 * because the engine indexes them in selector switches.
 */
#ifndef OPENRECET_AUDIO_H
#define OPENRECET_AUDIO_H

#include <stdint.h>

/* ─── BGM track table ─────────────────────────────────────────────────────
 * 21 entries. Filenames are CWD-relative (engine calls
 * GetCurrentDirectoryA + Loader::SetSearchDirectory before loading).
 *
 * Pure C — included in the test build. */
#define AUDIO_BGM_TRACK_COUNT 21

extern const char *const audio_bgm_filenames[AUDIO_BGM_TRACK_COUNT];

/* True for tracks the engine plays once (jingles): treasure, fanfare,
 * clear, staff. All other tracks loop infinitely. Source: the (iVar5 ==
 * 0x28 || 0x2c || 0x34 || 0x4c) guard in FUN_00498ef4. iVar5 is the
 * byte offset in the pointer table → /4 = the track index. */
int audio_is_one_shot_track(int track);

/* Returns the filename for a track in [0, AUDIO_BGM_TRACK_COUNT) or
 * NULL otherwise. */
const char *audio_bgm_filename(int track);

/* Sentinel passed from music.c when the selector wants the BGM to stop
 * outright (state 0 at frame_counter == 0x1ba7). Matches the engine's
 * FUN_00499200(param_1 == -2) → Performance::Stop branch. */
#define AUDIO_TRACK_STOP   (-2)

/* ─── Audio trace log (opt-in JSONL) ─────────────────────────────────────
 *
 * Off by default. When --audio-trace <path> is set on the CLI, every
 * BGM swap (and later SE trigger + fade event) emits one JSON line.
 * Schema:
 *
 *   {"t_ms":<uint>,"frame":<int>,"kind":"bgm_swap",   "track":<int>,  "name":<str>}
 *   {"t_ms":<uint>,"frame":<int>,"kind":"se_play",    "slot":<int>,   "name":<str>}
 *   {"t_ms":<uint>,"frame":<int>,"kind":"fade_start", "channel":<int>,"slider":<int>,
 *                                                     "centibel":<int>}
 *
 * t_ms is timeGetTime()-since-boot (matches the engine's clock).
 *
 * frame is the engine frame index (g_tick.frame_count) the event fired
 * on, stamped by audio_trace_set_frame() once per tick from the main
 * loop — the SAME counter d3d_trace/call-trace and the retail Frida
 * capture key on, so tools/audio_diff.py can align port↔retail sound
 * triggers frame-for-frame. -1 until the first audio_trace_set_frame()
 * (e.g. the test build, where no main loop drives it).
 * name is JSON-escaped per audio_trace_json_escape() — \", \\, \n,
 * \r, \t mapped explicitly; other non-printable / non-ASCII bytes
 * become \uXXXX. Filenames in our table are pure ASCII so the
 * escape stays small in practice.
 *
 * Pure C — present in both Win32 and test builds. timeGetTime() is
 * #ifdef-guarded; tests get t_ms=0.
 */

/* Opens the trace file in append mode and stashes the FILE* + a
 * boot-time anchor. Returns 1 on success, 0 if path is NULL/fopen
 * fails. Idempotent: closing a stale FILE* before reopening. */
int  audio_trace_open(const char *path);

/* Flushes + closes the trace file. Safe to call when no trace is
 * open. */
void audio_trace_close(void);

/* Stamp the engine frame index carried by every subsequent trace event.
 * Called once per tick from the main loop (next to d3d_trace_begin_frame)
 * with g_tick.frame_count. Until first called, events carry frame=-1. */
void audio_trace_set_frame(int frame);

/* Emits one JSONL line if a trace is open. No-op otherwise. */
void audio_trace_emit_bgm_swap(int track, const char *name);

/* Same shape, kind="se_play". `slot` is the 0..109 SE table index;
 * `name` is a short identifier (e.g. "se_012_id0148.wav"). */
void audio_trace_emit_se_play(int slot, const char *name);

/* Same shape, kind="fade_start". `channel` is the AUDIO_FADE_CHANNEL_*
 * index (see audio_fade.h), `slider` the current [0,9] slider value,
 * `centibel` the SetVolume value that was applied. Emitted by
 * audio_fade_apply() on every BGM swap and every SE play. */
void audio_trace_emit_fade_start(int channel, int slider, int32_t centibel);

/* Test hook: returns whether the trace is currently open. */
int  audio_trace_is_open(void);

/* JSON-escape `src` into `dst` (cap bytes total, NUL-terminated on
 * return). Returns the number of bytes written excluding the NUL.
 * Truncates cleanly if `dst` is too small — never overruns. */
#include <stddef.h>
size_t audio_trace_json_escape(const char *src, char *dst, size_t cap);

#ifdef _WIN32
#include <windows.h>

/* Init the DirectMusic backend, preload all 21 BGM segments, and
 * register the swap bridge with music.c. Returns 1 on full success,
 * 0 on any failure (CoCreateInstance / InitAudio / LoadObjectFromFile).
 * On 0, audio_shutdown still does the right thing for whatever was
 * created.
 *
 * Must be called after CreateWindowEx (uses hwnd for InitAudio) and
 * after the working directory has been pointed at the data tree
 * (Loader::SetSearchDirectory uses GetCurrentDirectoryA). */
int  audio_init(HWND hwnd);

/* Tear down the backend. Stops any playing segment, releases the
 * AudioPath, all 21 segments and their states, the Loader, the
 * Performance, and calls CoUninitialize. Safe to call after a failed
 * audio_init — releases whatever was created. */
void audio_shutdown(void);

/* Play `track` on the BGM AudioPath. If `track == current_track`, this
 * is a no-op (matches FUN_00499200's `DAT_005d1960 != param_1` guard).
 * If `track == AUDIO_TRACK_STOP` (-2), stops the current segment.
 *
 * Returns 1 on Performance::PlaySegmentEx S_OK (or no-op short-circuit),
 * 0 on any failure path. */
int  audio_play_track(int32_t track);

#endif /* _WIN32 */

/* SE trigger — currently a trace-only shell. The full FUN_00499c63
 * port (volume-blend with the fade curve via audio_fade_compute,
 * PlaySegmentEx onto the SE AudioPath with DMUS_SEGF_SECONDARY=0x80,
 * QueryInterface-upgrade of the returned SegmentState to
 * SegmentState8) lands in a follow-up commit. Per engine-quirks #46
 * every SE in vendor data routes to path A (DAT_0964310c); path B
 * and the cross-slot voice-stealing scan are dead-code branches
 * driven by the all-zero +4 column of the SE ID table.
 *
 * For now this function:
 *   - bounds-checks `slot` against AUDIO_SE_COUNT
 *   - emits an audio_trace `se_play` event if --audio-trace is on
 *   - returns 1 always (no actual playback yet)
 *
 * Driven by `--play-se <slot[,slot,...]>` on the CLI for smoke tests
 * (main.c arms a SetTimer that walks the slot list at a configurable
 * interval; see --play-se-after-ms / --play-se-interval-ms).
 *
 * Pure C — no _WIN32 guard — so the trace event is testable directly. */
int audio_play_se(int slot);

/* Resource-ID variant of `audio_play_se`. The engine's menu/game code
 * names SEs by resource ID (e.g. 0x143 = confirm, 0x146 = cursor tick)
 * via FUN_00499519; we mirror that for call sites that hard-code IDs.
 * Does NOT play immediately: it FLAGS the slot (engine FUN_004994f3,
 * DAT_0964308c[slot] = 1); the per-frame `audio_se_flush` pump plays each
 * flagged SE once — so same-frame repeats of one SE collapse to a single
 * play, exactly like retail (§21.31.7).  Returns 1 (engine behavior)
 * whether or not the id is in the table. */
int audio_play_se_by_id(uint16_t id);

/* Flush the SE-A request flags — the FUN_0049966a per-frame pump head.
 * Walks the 110 flags, plays each flagged SE once (audio_play_se) and
 * clears.  Called once per ticked frame from music_step_default (sim_b). */
void audio_se_flush(void);

/* Filename-loaded SE / voice clip — mirror of FUN_0049933c.
 *
 * The opening cutscene (and other event scripts) play voice lines and
 * one-off SEs by *path* rather than by the resource-baked SE table —
 * the `.ivt` `se:<bin>` command names a loose RIFF/WAVE file relative to
 * the game dir, e.g. "bin/se/01ti/event/tea_mataku.bin" (Tear's voice).
 * The engine carries a SINGLE filename-SE slot: each call Unloads the
 * previous segment before loading the new one (so only one voice line
 * plays at a time), and routes playback to SE AudioPath B with the SE-B
 * slider volume — distinct from the resource SEs on path A.
 *
 * `path` is the game-relative path from the script (NUL-terminated,
 * ASCII). Resolved by IDirectMusicLoader against the search directory
 * set at audio_init (the cwd / game dir), same as the BGM .wma loads.
 * Returns 1 on a successful PlaySegmentEx, 0 otherwise (backend not
 * ready, file not found, load/play failure). The .bin files are retail
 * assets — read at runtime from the install, never redistributed.
 *
 * Pure-C shell (emits the audio_trace + SE-B fade) with the DirectMusic
 * body under _WIN32; a no-op returning 1 on the test build. */
int audio_play_se_file(const char *path);

#ifdef _WIN32
/* Drop-in replacement for the default audio_fade apply hook that
 * clamps every SetVolume call to -10000 centibel (silence). Engine's
 * audio code (PlaySegmentEx, fade animations, segment-state queueing)
 * runs untouched — only the master attenuation forwarded to the
 * IDirectMusicAudioPath is pinned to silence. Used by `--silent-audio`
 * to make scenarios capture-only without the BGM cluttering whatever
 * else is playing on the host. Mirrors the retail-side
 * `installSilentAudioFromPath` hook in tools/frida/openrecet-agent.js.
 *
 * Install via `audio_fade_set_apply_hook(silent_audio_apply_hook)`
 * AFTER audio_init (which installs the default audible hook). */
void silent_audio_apply_hook(int channel, int32_t centibel);
#endif

#endif /* OPENRECET_AUDIO_H */
