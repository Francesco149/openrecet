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

#endif /* OPENRECET_AUDIO_H */
