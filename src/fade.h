/*
 * fade.h — scene-fade quad (FUN_004526f5 / 0045281c / 004526ab / 004528b3
 *          counter machinery + FUN_00453e8f alpha-quad render).
 *
 * Drives the black "scene fade-out" you see when the player picks NEW GAME
 * on the title menu: a single 640×480 alpha-blended quad textured with a
 * pure-black 6×6 patch of `bmp/system.bmp` (or a pure-white patch for
 * mode 1). The alpha ramps over `duration - 2` sim ticks.
 *
 * Full RE writeup including the Ghidra mis-decomp of FUN_00453e8f and
 * the recovered alpha formula at `docs/findings/title-fade-out.md`.
 *
 * Two-layer split (matches render_quad / scene_title pattern):
 *   - Pure-C counter machinery + alpha math: compiles on the Linux
 *     unit-test build.
 *   - Win32 alpha-quad renderer (loads system.bmp lazily, emits via
 *     render_quad_add): #ifdef _WIN32 at the bottom.
 *
 * Globals mirrored (all engine-faithful names in comments):
 *   counter  — DAT_0438bf78
 *   phase    — DAT_0438bf7c (1 = fade-IN-progress / +α; -1 = fade-OUT
 *                            of the previous fade / α decreases; 0 = idle)
 *   mode     — DAT_0438bf80 (0 → src (9,1)-(15,7) = black,
 *                            1 → src (1,1)-(7,7) = white,
 *                            2 → special "no duration" mode used by other
 *                                scenes — ends at counter == 0x1f)
 *   duration — DAT_005c5934 (target counter; e.g. 0x11 for the
 *                            title→NEW-GAME flow)
 *
 * "Phase" labels here use the engine's polarity (1 vs -1), not the
 * audio_fade module's (1 vs 2). Don't confuse them — they're separate
 * subsystems that happen to both use the word "phase".
 *
 * The vestigial 100-particle pre-roll inside FUN_004526f5 is intentionally
 * omitted — no consumer reads those globals. See findings doc §"Vestigial
 * state".
 */
#ifndef OPENRECET_FADE_H
#define OPENRECET_FADE_H

#include <stdint.h>

/* ─── pure-C state + machinery ───────────────────────────────────────── */

/* Engine-faithful globals (g_ prefix to match other ports). */
extern int32_t g_fade_counter;    /* DAT_0438bf78 */
extern int32_t g_fade_phase;      /* DAT_0438bf7c */
extern int32_t g_fade_mode;       /* DAT_0438bf80 */
extern int32_t g_fade_duration;   /* DAT_005c5934 */

/* FUN_004526f5 — start a phase-1 fade. Sets counter=1, phase=1, mode,
 * duration. The engine also pre-rolls two 100-element float-vec tables
 * here; we skip that — no consumer reads them (see findings doc). */
void fade_phase1_start(int32_t mode, int32_t duration);

/* FUN_0045281c — start a phase-(-1) fade (fade-IN at the end of a scene
 * load completion). Sets counter=0, phase=-1, mode, duration. */
void fade_phase_out_start(int32_t mode, int32_t duration);

/* FUN_004526ab — per-sim-tick counter advance. Called from sim_step_a
 * tail. Idle (phase==0) is a no-op. Phase 1 clamps the counter at
 * duration+1. Phase -1 resets phase + counter to 0 at counter > duration. */
void fade_tick(void);

/* FUN_004528b3 — "is the phase-1 fade done?" — returns 1 when phase==1
 * AND (mode==2 ? counter==0x1f : counter==duration). 0 otherwise. */
int  fade_is_done(void);

/* Reset to BSS-zero. Tests + lifecycle. */
void fade_reset(void);

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

/* Lazy load — the renderer calls this on first frame after construction.
 * Idempotent. Returns 1 once `bmp/system.bmp` is loaded into the static
 * slot, 0 if the load failed (renderer will silently skip until the next
 * attempt). */
int  fade_load_system_texture(IDirect3DDevice8 *dev);

/* Release the system.bmp slot. Idempotent. Called from main.c shutdown. */
void fade_unload_system_texture(void);

/* FUN_00453e8f — emit the alpha quad if there's a fade running.
 * No-op when `g_fade_counter == 0`. Caller is responsible for being
 * inside BeginScene + having set up render_quad state.
 *
 * Renders ONE quad covering (0,0)-(640,480) with diffuse =
 * (alpha << 24) | 0xffffff and the per-mode source patch from
 * system.bmp; the resulting blend with SRCALPHA/INVSRCALPHA fades the
 * back-buffer toward black (mode 0) or white (mode 1). */
void fade_render(IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_FADE_H */
