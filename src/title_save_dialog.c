/*
 * title_save_dialog.c — see title_save_dialog.h.
 *
 * Engine sources (Ghidra all.c line refs):
 *
 *   FUN_00434d6a @ 0x434d6a (85 B)  L32484
 *   FUN_004356cd @ 0x4356cd (67 B)  L32904
 *   FUN_00435117 @ 0x435117 (615 B) L32676    [STUB]
 *   FUN_0043537e @ 0x43537e (660 B) L32754    [STUB]
 *   FUN_00435747 @ 0x435747 (300 B) L32937    [STUB]
 *
 * Cross-references for the side-table setters/getters (unported,
 * documented for future reference):
 *
 *   FUN_00435612 @ 0x435612 (8 B):  DAT_0438b150 = 0
 *   FUN_0043561a @ 0x43561a (11 B): DAT_0438b150 = 1
 *   FUN_00435625 @ 0x435625 (6 B):  return DAT_0438b150
 *   FUN_00435644 @ 0x435644 (79 B): read shake pos
 *   FUN_00435693 @ 0x435693 (58 B): shake-init (sets DAT_0438ac18=0,
 *                                   DAT_0438b150=1, delta=param,
 *                                   pos=param)
 */

#include "title_save_dialog.h"

#include "audio.h"        /* audio_play_se_by_id — FUN_00499519 analog */
#include "call_trace.h"
#include "sim.h"          /* g_sim_buttons[0].pressed → DAT_073dddd4 */

#ifdef _WIN32
#include <math.h>         /* sinf / fabsf — FUN_00503a44 (sin) + abs */
#include "render_quad.h"  /* render_quad_bind/add/flush — FUN_00404efc/405354 */
#include "sysassets.h"    /* g_sysassets.nowloading_tga — DAT_073cc770 */
#endif

/* ─── module state ────────────────────────────────────────────────── */

/* DAT_0438b148 — save/load dialog counter. Engine ramps 0..8 then
 * 8..0 depending on `closing_mode`. */
static int   g_active_counter = 0;

/* DAT_0438ad28 — 0 = opening (counter rises), 1 = closing (falls). */
static int   g_closing_mode   = 0;

/* DAT_0438ac18 — shake position-interp countdown. */
static int   g_shake_counter  = 0;

/* DAT_0438b154 — monotonic anim counter, incremented every frame
 * the dialog is NOT open. */
static int   g_anim_counter   = 0;

/* DAT_0438abf4 / DAT_0438abf8 — current shake position. */
static float g_shake_pos_x    = 0.0f;
static float g_shake_pos_y    = 0.0f;

/* DAT_0438ac00 / DAT_0438ac04 — per-frame shake delta. */
static float g_shake_delta_x  = 0.0f;
static float g_shake_delta_y  = 0.0f;

/* DAT_0438b150 — cursor-sprite visibility (FUN_0043561a/00435612). */
static int   g_cursor_visible = 0;

/* ─── lifecycle + accessors ───────────────────────────────────────── */

void title_save_dialog_reset(void)
{
    g_active_counter = 0;
    g_closing_mode   = 0;
    g_shake_counter  = 0;
    g_anim_counter   = 0;
    g_shake_pos_x    = 0.0f;
    g_shake_pos_y    = 0.0f;
    g_shake_delta_x  = 0.0f;
    g_shake_delta_y  = 0.0f;
    g_cursor_visible = 0;
}

/* ─── shared menu-cursor control (FUN_0043561a/612/693/710) ────────── */

void title_save_dialog_cursor_set_visible(int on) { g_cursor_visible = on ? 1 : 0; }
int  title_save_dialog_cursor_get_visible(void)   { return g_cursor_visible;        }

/* TAS {phasepin} — zero the shared cursor's bob counter (DAT_0438b154). b154
 * free-runs from boot with no engine reset (its sole writer is the +1 in
 * anim_tick), so its value at any frame = total frames-since-boot the save
 * dialog was closed — which differs between port and retail by the
 * non-deterministic load/intro frame count. Pinning it to 0 at a deterministic
 * post-load anchor makes the hand-cursor bob phase identical across runs and
 * vs retail (engine-quirks §94; mirrors the companion db054 phasepin). */
void title_save_dialog_phasepin(void) { g_anim_counter = 0; }

/* FUN_00435693 — snap to (x,y), zero the slide countdown, show. The
 * engine also writes the deltas = (x,y) here; harmless with ac18==0. */
void title_save_dialog_cursor_snap(float x, float y)
{
    g_shake_counter  = 0;
    g_cursor_visible = 1;
    g_shake_delta_x  = x;
    g_shake_delta_y  = y;
    g_shake_pos_x    = x;
    g_shake_pos_y    = y;
}

/* FUN_00435710 — arm a 6-frame ease toward (x,y); anim_tick walks
 * shake_pos there one delta per frame. Does NOT touch visibility. */
void title_save_dialog_cursor_slide(float x, float y)
{
    g_shake_counter = 6;
    g_shake_delta_x = (x - g_shake_pos_x) / 6.0f;
    g_shake_delta_y = (y - g_shake_pos_y) / 6.0f;
}

int   title_save_dialog_get_active_counter(void) { return g_active_counter; }
void  title_save_dialog_set_active_counter(int v){ g_active_counter = v;    }

int   title_save_dialog_get_closing_mode(void)   { return g_closing_mode;   }
void  title_save_dialog_set_closing_mode(int v)  { g_closing_mode = v ? 1 : 0; }

int   title_save_dialog_get_shake_counter(void)  { return g_shake_counter;  }
void  title_save_dialog_set_shake_counter(int v) { g_shake_counter = v;     }

int   title_save_dialog_get_anim_counter(void)   { return g_anim_counter;   }
void  title_save_dialog_set_anim_counter(int v)  { g_anim_counter = v;      }

float title_save_dialog_get_shake_pos_x(void)    { return g_shake_pos_x;    }
float title_save_dialog_get_shake_pos_y(void)    { return g_shake_pos_y;    }

void  title_save_dialog_set_shake_delta(float dx, float dy)
{
    g_shake_delta_x = dx;
    g_shake_delta_y = dy;
}

/* ─── FUN_00434d6a (FULL body) ────────────────────────────────────── */
/*
 * Decompiled engine body (verbatim):
 *
 *   if (DAT_0438b148 < 1)              return 0;
 *   if (DAT_0438ad28 == 0) {
 *       if (DAT_0438b148 < 8)          DAT_0438b148++;
 *   } else {
 *       DAT_0438b148--;
 *       if (DAT_0438b148 < 1)          return 1;
 *   }
 *   if (DAT_0438b148 == 8 &&
 *       (DAT_073dddd4 & 0x30) != 0) {
 *       DAT_0438ad28 = 1;
 *       FUN_00499519(0x143);
 *   }
 *   return -1;
 *
 * DAT_073dddd4 is the just-pressed mask (engine `pressed`, our
 * `g_sim_buttons[0].pressed`).  Bits 0x10 = Z (button A) and 0x20 = X
 * (button B) — either confirms the dialog and flips into closing mode.
 *
 * FUN_00499519(0x143) plays SE id 0x143 = "menu confirm".  Mapped to
 * our `audio_play_se_by_id(0x143)`.
 */
int title_save_dialog_gate_tick(void)
{
    /* E.2 probe — FUN_00434d6a @ 0x434d6a. Full body parity. */
    CALL_TRACE_ENTER(0x434d6au);

    if (g_active_counter < 1) {
        return 0;
    }
    if (g_closing_mode == 0) {
        if (g_active_counter < 8) {
            g_active_counter++;
        }
    } else {
        g_active_counter--;
        if (g_active_counter < 1) {
            return 1;
        }
    }
    if (g_active_counter == 8 &&
        (g_sim_buttons[0].pressed & 0x30) != 0) {
        g_closing_mode = 1;
        audio_play_se_by_id(0x143);
    }
    return -1;
}

/* ─── FUN_004356cd (FULL body) ────────────────────────────────────── */
/*
 * Decompiled engine body (verbatim):
 *
 *   if (0 < DAT_0438ac18) {
 *       _DAT_0438abf4 = _DAT_0438ac00 + _DAT_0438abf4;
 *       DAT_0438ac18--;
 *       _DAT_0438abf8 = _DAT_0438ac04 + _DAT_0438abf8;
 *   }
 *   if (DAT_0438b148 == 0) {
 *       _DAT_0438b154++;
 *   }
 *
 * Two unconditional branches sharing no state; both safe to run with
 * BSS-zero defaults (first branch falls through, second increments
 * a counter that's only read by the cursor-shake render, which is
 * gated off in normal play).
 */
void title_save_dialog_anim_tick(void)
{
    /* E.2 probe — FUN_004356cd @ 0x4356cd. Full body parity. */
    CALL_TRACE_ENTER(0x4356cdu);

    if (g_shake_counter > 0) {
        g_shake_pos_x   += g_shake_delta_x;
        g_shake_counter -= 1;
        g_shake_pos_y   += g_shake_delta_y;
    }
    if (g_active_counter == 0) {
        g_anim_counter++;
    }
}

/* ─── FUN_00435117 (STUB body) ────────────────────────────────────── */
/*
 * Engine body (deferred):
 *
 *   if (DAT_0438b148 > 0) {
 *       // bind dim texture; draw full-screen alpha-fade quad
 *       // bind frame texture; draw scaled dialog frame
 *       // draw text strings keyed off DAT_0438ac0c (save/load/delete)
 *   }
 *
 * Stub keeps only the early-return.  In normal play g_active_counter
 * stays at 0 forever, so the gate fires.  Body lands when the save/
 * load dialog port chip arrives. */
void title_save_dialog_render(void)
{
    /* E.2 probe — FUN_00435117 @ 0x435117. Body deferred. */
    CALL_TRACE_ENTER_STUB(0x435117u);

    if (g_active_counter <= 0) {
        return;
    }

    /* Engine body deferred — see header. */
}

/* ─── FUN_0043537e (STUB body) ────────────────────────────────────── */
/*
 * Engine body (deferred):
 *
 *   if (DAT_0438af34 > 0) {
 *       // bind frame texture; draw scaled "secondary" dialog frame
 *       // (probably the options/settings panel)
 *       // draw two text strings with blend animation keyed off
 *       // DAT_0438ac14 and DAT_0438af30 (mode 1/2)
 *   }
 *
 * DAT_0438af34 stays BSS-zero in our port; an internal accessor
 * isn't exposed because nothing in our port writes it yet. */
void title_save_dialog_secondary_render(void)
{
    /* E.2 probe — FUN_0043537e @ 0x43537e. Body deferred. */
    CALL_TRACE_ENTER_STUB(0x43537eu);

    /* Gate global DAT_0438af34 is not in this module's storage yet —
     * no writer is ported, so the gate is BSS-zero by construction
     * and the body would always short-circuit.  Match that with an
     * unconditional return; if a future port adds a writer, expose
     * the gate accessor + flip this to a real check. */
    return;
}

/* ─── FUN_00435747 (STUB body) ────────────────────────────────────── */
/*
 * Engine body (deferred):
 *
 *   shake = sin(DAT_0438b154 * 0.1) * 8
 *   if (shake < 0) shake = -shake;
 *   bind cursor texture
 *   if (DAT_0438b150 != 0) {
 *       draw cursor quad at (DAT_0438abf4 - shake, DAT_0438abf8 - 20)
 *   }
 *
 * The visibility gate DAT_0438b150 is BSS-zero in our port — only
 * setters FUN_00435612/1a touch it, both unported.  Same deal as
 * the secondary render. */
void title_save_dialog_cursor_render(struct IDirect3DDevice8 *dev)
{
    /* FUN_00435747 @ 0x435747 — shared menu hand-cursor draw. */
    CALL_TRACE_ENTER(0x435747u);

#ifdef _WIN32
    if (g_cursor_visible == 0) {
        return;
    }
    sprite_t *cur = &g_sysassets.nowloading_tga;
    if (cur->tex == NULL) {
        return;
    }

    /* bob = |sin(anim_counter · 0.1)| · 8 — engine takes the ABS of the
     * sin so the hand wobbles LEFT-only (0..8 toward the row) at period
     * π (FUN_00435747 L32955-32965). Same shape choice_box.c uses. */
    const float bob = fabsf(sinf((float)g_anim_counter * 0.1f)) * 8.0f;

    /* 40×40 hand glyph at src (192,0)-(232,40) of nowloading.tga, drawn
     * at (shake_pos_x - bob, shake_pos_y - 20). */
    const float dst[4] = { g_shake_pos_x - bob, g_shake_pos_y - 20.0f,
                           40.0f, 40.0f };
    const float src[4] = { 192.0f, 0.0f, 232.0f, 40.0f };
    render_quad_bind(dev, cur);
    render_quad_add(dst, src, cur->width, cur->height, 0xffffffffu);
    render_quad_flush(dev);
#else
    (void)dev;
#endif
}
