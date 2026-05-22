/*
 * nowloading.h — engine's "Now Loading…" overlay (FUN_00453147 at 0x453147).
 *
 * Per-frame UI overlay drawn AFTER the scene render and AFTER the
 * cross-fade alpha quad (FUN_00453e8f). Composed of two layers:
 *
 *   - A static 128×64 panel at screen position (512, 400) — the actual
 *     "Now Loading…" text bitmap. Sampled from bmp/nowloading.tga's
 *     UV rect (0, 0, 0.25, 0.5) over a (64,0)-(192,64) source pixel
 *     window into the 256×64 texture.
 *
 *   - A rotating 64×64 spinner centred at (496, 440), sampled from
 *     UV rect (0.000781, 0.003125, 0.25, 1.0). Rotation accumulates
 *     at 0.3 rad/tick (engine `_DAT_06a4998c`).
 *
 * Gate (engine DAT_06a49958 / DAT_06a49960):
 *
 *   - When NEITHER is set, the overlay is invisible. A separate alpha
 *     counter (engine `_DAT_06a49988`) is decremented by 32/tick toward
 *     0. The engine uses that counter for other UI elements that fade
 *     out alongside the loading overlay; the overlay itself is gate-
 *     driven, not alpha-driven.
 *
 *   - When EITHER is set, the overlay draws. DAT_06a49958 is the
 *     worker-thread "still loading" flag set by FUN_0049de18 (the
 *     scene-asset worker spawn) and cleared when assets finish.
 *     DAT_06a49960 is a secondary gate set by several other load
 *     paths (FUN_0049de24 and friends).
 *
 * Because the engine's worker thread isn't ported yet, openrecet
 * exposes `nowloading_set_active(int)` so the post-fade init code can
 * fake the gate for the LOADING→INGAME transition window. The
 * placeholder INGAME scene draws underneath until the asset worker
 * thread (and real scene-1 sim/render) land.
 */

#ifndef OPENRECET_NOWLOADING_H
#define OPENRECET_NOWLOADING_H

#include <stdint.h>

/* Test/inspection accessors — also used internally on the Win32 path.
 * The render function (Win32-only, below) drives the state forward;
 * these expose it for the unit-test build. */
int   nowloading_get_alpha_counter(void); /* engine _DAT_06a49988 (0..255) */
float nowloading_get_rotation(void);      /* engine _DAT_06a4998c (radians) */
int   nowloading_is_active(void);         /* OR of the two gates */

/* Pure-C tick — advances the alpha counter / rotation per the active
 * gate, no D3D. This is what FUN_00453147 does at L17-22 (alpha
 * decay) and L40 (rotation advance) folded into one. Used by the
 * Win32 render path AND by tests.
 *
 * Returns the previous gate state (1 = was active, 0 = was inactive)
 * so tests can assert single-tick semantics. */
int  nowloading_tick(void);

/* Set/clear the "Now Loading…" active gate. Mirrors writes to
 * DAT_06a49958. Until the engine's asset worker thread (FUN_0049de18)
 * lands, openrecet sets this manually from scene_post_fade_init. */
void nowloading_set_active(int active);

/* Reset module state — alpha=0, rotation=0, gate=0. Idempotent. */
void nowloading_reset(void);

#ifdef _WIN32

struct IDirect3DDevice8;

/* Per-frame render of the overlay. Mirrors FUN_00453147 end-to-end:
 *   if gate==0:  nowloading_tick();              // alpha decay
 *   else:        nowloading_tick();              // advance rotation
 *                set blend + filter render state
 *                bind nowloading.tga texture
 *                render_quad_add(static panel)
 *                render_quad_flush
 *                render_quad_draw_rotated(spinner)
 *
 * Engine call site: FUN_004547ab L203, AFTER FUN_00453e8f (cross-
 * fade alpha quad). We hook the same way from main.c::render_dispatch.
 *
 * If the sysassets nowloading_tga slot is empty (sprite tex==NULL),
 * the overlay is skipped — same defensive behaviour as the engine's
 * SetTexture pointer write (D3D8 tolerates NULL).
 *
 * Note: this fuses tick + render the same way the engine does. There
 * is no separate `nowloading_tick_step` to call elsewhere. */
void nowloading_render(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_NOWLOADING_H */
