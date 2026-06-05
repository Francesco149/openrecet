/*
 * title_save_dialog.h — five tiny engine functions clustered around
 * the title-scene "save/load dialog" + cursor-shake overlay.  Engine
 * VAs 0x434d6a, 0x4356cd, 0x435117, 0x43537e, 0x435747.
 *
 * The cluster fires once per title-scene frame from FUN_0049a59e
 * (title sim) and FUN_0049c644 (title render).  In normal play none
 * of the dialog/shake-flag gates are set (all BSS-zero), so the body
 * of each function is either a no-op gate or a benign counter update.
 * Our port maps each function to a port symbol, ports the state-only
 * helpers (0x434d6a, 0x4356cd) in full, and stubs the three render
 * helpers (0x435117, 0x43537e, 0x435747) because the D3D draw paths
 * underneath need the texture/font subsystem fully ported first.
 *
 * Engine globals consolidated in the module's static storage:
 *
 *   active_counter   (DAT_0438b148, int): save/load dialog ramp 0..8.
 *                    BSS-zero. FUN_00434d6a is the only writer
 *                    (incrementing while opening, decrementing while
 *                    closing) and the read-gate for FUN_00435117 +
 *                    the second branch of FUN_004356cd.
 *   closing_mode     (DAT_0438ad28, int): 0 → opening (counter ramps
 *                    up to 8), 1 → closing (counter ramps down to 0).
 *                    Set when the user confirms with Z/X while the
 *                    dialog is fully open (counter==8); cleared on
 *                    re-open.
 *   shake_counter    (DAT_0438ac18, int): position-interpolation
 *                    countdown.  FUN_004356cd decrements while > 0.
 *                    Setter at FUN_00435693 (unported).
 *   anim_counter     (DAT_0438b154, int): monotonic frame counter
 *                    incremented by FUN_004356cd whenever the dialog
 *                    is NOT open.  Consumed by the cursor shake-render
 *                    FUN_00435747 — only matters when its visibility
 *                    gate DAT_0438b150 is set, which is BSS-zero in
 *                    normal play.
 *   shake_pos_*      (DAT_0438abf4 / abf8, float pair): cursor-shake
 *                    base position.  Only mutated when shake_counter
 *                    > 0 (= during an active shake animation set up
 *                    by FUN_00435693).
 *   shake_delta_*    (DAT_0438ac00 / ac04, float pair): per-frame
 *                    delta added to shake_pos_* during the active
 *                    window.  BSS-zero by default.
 *
 * What this chip ports:
 *
 *   title_save_dialog_gate_tick     — FUN_00434d6a, 85 B, FULL body.
 *     Returns: 0 if dialog closed, 1 if just-closed-this-tick,
 *     -1 (== 0xffffffff) otherwise.  Engine caller FUN_0049a59e
 *     uses the -1 return to short-circuit the rest of the title sim
 *     while the dialog is open; our scene_title_sim_default ignores
 *     the return for now because the dialog never opens (no setter
 *     port).  When the dialog port lands, wrap the caller side.
 *
 *   title_save_dialog_anim_tick     — FUN_004356cd, 67 B, FULL body.
 *     Two-branch pure-state update: shake-position interpolation +
 *     anim-counter increment-while-dialog-closed.  Always safe to
 *     call; idempotent on BSS-zero defaults.
 *
 *   title_save_dialog_render             — FUN_00435117, 615 B, STUB body.
 *   title_save_dialog_secondary_render   — FUN_0043537e, 660 B, STUB body.
 *   title_save_dialog_cursor_render      — FUN_00435747, 300 B, STUB body.
 *     All three keep the engine VA + the gate-check stripped down to
 *     the early-return path so call-count parity matches retail.  Their
 *     CALL_TRACE_ENTER probes are marked STUB so the diff tool surfaces
 *     them as `≈` for honesty.  Render bodies land when the title-
 *     screen save/load dialog port chip arrives.
 *
 * Setters (FUN_00435612 cursor-visible-off, FUN_0043561a on,
 * FUN_00435625 read, FUN_00435693 shake-init) are exposed as small
 * test/host-side accessors so a future port can wire them without
 * reaching into the module's internals.
 */

#ifndef OPENRECET_TITLE_SAVE_DIALOG_H
#define OPENRECET_TITLE_SAVE_DIALOG_H

/* Reset all module state to BSS-zero defaults. Idempotent. */
void title_save_dialog_reset(void);

/* ─── pure-state ticks (FULL body parity) ─────────────────────────── */

/* FUN_00434d6a — per-frame counter management for the save/load
 * dialog open/close ramp.  See header for return semantics. */
int title_save_dialog_gate_tick(void);

/* FUN_004356cd — per-frame shake-position interpolation +
 * anim-counter increment-while-closed. */
void title_save_dialog_anim_tick(void);

/* ─── render helpers (STUB body — gate-check only) ─────────────────── */

/* FUN_00435117 — save/load dialog frame render.  Stub: only the
 * early-return gate `if (counter == 0) return;` is preserved. */
void title_save_dialog_render(void);

/* FUN_0043537e — secondary dialog frame render.  Stub: only the
 * early-return gate `if (DAT_0438af34 == 0) return;` is preserved. */
void title_save_dialog_secondary_render(void);

/* FUN_00435747 — cursor sprite shake-render. Now a real body: draws the
 * 40×40 hand from nowloading.tga (DAT_073cc770, src (192,0)-(232,40)) at
 * (shake_pos_x - bob, shake_pos_y - 20), bob = |sin(anim_counter·0.1)|·8,
 * gated on the cursor-visible flag. D3D draw is _WIN32-only; host build
 * emits the probe + returns. */
struct IDirect3DDevice8;
void title_save_dialog_cursor_render(struct IDirect3DDevice8 *dev);

/* ─── shared menu-cursor control (DAT_0438b150 + the abf4/abf8 pos +
 * ac00/ac04 slide deltas + ac18 slide countdown). The hand cursor is a
 * single shared sprite the engine reuses across the options panel, the
 * save/load dialog, and the Yes/No choice box — see FUN_0043561a/612
 * (visible on/off), FUN_00435693 (snap), FUN_00435710 (6-frame slide). */

/* FUN_0043561a (on) / FUN_00435612 (off) — DAT_0438b150 visibility. */
void title_save_dialog_cursor_set_visible(int on);
int  title_save_dialog_cursor_get_visible(void);

/* FUN_00435693 — snap the cursor to (x,y) and show it. */
void title_save_dialog_cursor_snap(float x, float y);

/* FUN_00435710 — start a 6-frame ease toward (x,y). Visibility unchanged. */
void title_save_dialog_cursor_slide(float x, float y);

/* TAS {phasepin} — zero the bob counter (DAT_0438b154) so the shared hand
 * cursor's bob is phase-pinned across the non-deterministic load. Called from
 * segtrace_phasepin_cb alongside the companion db054 reset. */
void title_save_dialog_phasepin(void);

/* ─── test/host-side accessors ────────────────────────────────────── */

/* DAT_0438b148 — save/load dialog counter (0..8). */
int  title_save_dialog_get_active_counter(void);
void title_save_dialog_set_active_counter(int v);

/* DAT_0438ad28 — opening/closing mode flag. */
int  title_save_dialog_get_closing_mode(void);
void title_save_dialog_set_closing_mode(int v);

/* DAT_0438ac18 — shake position-interp countdown. */
int  title_save_dialog_get_shake_counter(void);
void title_save_dialog_set_shake_counter(int v);

/* DAT_0438b154 — monotonic anim counter. */
int  title_save_dialog_get_anim_counter(void);
void title_save_dialog_set_anim_counter(int v);

/* DAT_0438abf4/abf8 — current shake position. */
float title_save_dialog_get_shake_pos_x(void);
float title_save_dialog_get_shake_pos_y(void);

/* DAT_0438ac00/ac04 — per-frame shake deltas. */
void  title_save_dialog_set_shake_delta(float dx, float dy);

#endif /* OPENRECET_TITLE_SAVE_DIALOG_H */
