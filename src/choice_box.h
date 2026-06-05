/*
 * choice_box.h — the engine's generic yes/no (multi-option) CHOICE BOX.
 *
 * A reusable engine subsystem: a gold scroll banner with a question and a
 * row of selectable options (Yes / No for the binary case), a bobbing
 * hand cursor, and an open/confirm/close animation. The opening-prologue
 * ESC "Do you want to skip this event?" prompt is the first consumer
 * (src/skip_event.c drives it off the FUN_0046c2cb gate); the same box
 * backs save/shop/event confirmations across the engine.
 *
 * Engine correspondence (by-address):
 *   FUN_00434def  choice_box_open()    — open + lay the prompt text out
 *   FUN_00434ed2  choice_box_poll()    — per-frame nav / confirm / close anim
 *   FUN_00434dd6  choice_box_blocking()— "a box (or msg) is up" query
 *   FUN_00434dbf  choice_box_reset()   — hard reset (scene teardown)
 *   FUN_0043537e  choice_box_draw()    — banner + prompt + options  (Win32)
 *   FUN_00435747  title_save_dialog_cursor_render() — the SHARED hand cursor
 *                 (choice_box_open/poll drive its snap/slide/visible state via
 *                 title_save_dialog_cursor_*, exactly as the engine reuses the
 *                 one cursor across the options panel + save dialog).  (Win32)
 *
 * The state machine is pure C and host-tested (test_choice_box.c); only the
 * D3D draw is Win32-gated.
 */
#ifndef OPENRECET_CHOICE_BOX_H
#define OPENRECET_CHOICE_BOX_H

#include <stdint.h>

/* The engine button-edge bits the poll reads from DAT_073dddd4
 * (src/input.c input_binding_mask[] — same 14-bit layout). */
#define CB_BTN_RIGHT  0x0001   /* move to the next option   (Yes → No)  */
#define CB_BTN_LEFT   0x0002   /* move to the previous one  (No → Yes)  */
#define CB_BTN_A      0x0010   /* confirm the selection                 */
#define CB_BTN_B      0x0020   /* cancel  (only when mode == 1)         */

/* choice_box_poll() return codes — mirror FUN_00434ed2's int return. */
enum {
    CB_INACTIVE  =  0,   /* no box up                                    */
    CB_BUSY      = -1,   /* open/close anim or a nav step this frame     */
    CB_OPT0      =  1,   /* option 0 committed (Yes) — close anim done   */
    CB_OPT1      =  2,   /* option 1 committed (No / B-cancel) — done    */
};

/* Open the box. `text` is the prompt ('<' splits it into stacked rows, as
 * FUN_00434def does); `mode` = FUN_00434def's param_2 (1 = B cancels to
 * option 1); `sel` = the initial selection (0 = first option). */
void choice_box_open(const char *text, int mode, int sel);

/* Advance one frame with the button-edge mask `edge` (held & ~prev).
 * Returns CB_INACTIVE / CB_BUSY while still up, or CB_OPT0 / CB_OPT1 on the
 * frame the close animation finishes (the box auto-closes — _active() reads
 * 0 afterwards). `reset_pos` mirrors FUN_00434ed2's param_1 (slide the
 * cursor offscreen on close); pass 1. */
int choice_box_poll(uint16_t edge, int reset_pos);

/* Is a box currently up (open, interactive, or closing)? DAT_0438af34 > 0. */
int choice_box_active(void);

/* Hard reset — FUN_00434dbf. Closes the box with no result (scene teardown). */
void choice_box_reset(void);

/* ── render-state getters (choice_box_draw + host tests) ── */
int         choice_box_selection(void);   /* DAT_0438ac24 — 0..n-1            */
int         choice_box_anim(void);        /* DAT_0438af34 — open/close 0..4   */
int         choice_box_options(void);     /* DAT_0438ac08 — option/row count  */
const char *choice_box_text(void);        /* the laid-out prompt buffer       */

#ifdef _WIN32
struct IDirect3DDevice8;
/* Draw the box (banner + prompt + options + bobbing cursor). Caller is inside
 * BeginScene + render_quad_state_setup, exactly like scene1_dialogue_draw. */
void choice_box_draw(struct IDirect3DDevice8 *dev);
#endif

#endif /* OPENRECET_CHOICE_BOX_H */
