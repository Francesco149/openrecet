/*
 * choice_box.c — see choice_box.h. Faithful port of the engine's generic
 * choice box: FUN_00434def (open), FUN_00434ed2 (poll), FUN_00434dd6 /
 * FUN_00434dbf (query / reset), and the FUN_0043537e + FUN_00435747 draw.
 *
 * The state machine is pure C (host-tested). Only choice_box_draw() is
 * Win32-gated. SE playback (FUN_00499519) is a side effect that doesn't touch
 * the return values, so it is _WIN32-only (host tests don't need audio) — the
 * same "audio is deferred at the host layer" convention the dialogue uses.
 */
#include "choice_box.h"

#include <string.h>

#ifdef _WIN32
#include "audio.h"                  /* audio_play_se_by_id (FUN_00499519) */
#define CB_SE(id) audio_play_se_by_id((uint16_t)(id))
#else
#define CB_SE(id) ((void)0)
#endif

/* ── engine state globals (by-address names in comments) ── */
static int   cb_active = 0;   /* DAT_0438af34 — 0 closed; 1..4 open anim/interactive */
static int   cb_close  = 0;   /* DAT_0438ac14 — close-anim counter (0 = not closing)  */
static int   cb_result = 0;   /* DAT_0438af30 — committed kind: 1=opt0, 2=opt1, 3=cancel */
static int   cb_sel    = 0;   /* DAT_0438ac24 — current selection                     */
static int   cb_mode   = 0;   /* DAT_0438b144 — param_2 (1 = B cancels)               */
static int   cb_rows   = 1;   /* DAT_0438ac08 — prompt text row count                 */
static float cb_b14c   = 0.0f;/* _DAT_0438b14c — 2-row vertical shift (8.0 if 2 rows)  */
static int   cb_bob    = 0;   /* cursor bob phase (_DAT_0438b154 family)              */

/* DAT_0438af3b prompt buffer: the engine lays the text out at &DAT_0438af3b+1
 * (= &DAT_0438af3c), one 0x100-stride row per '<'-delimited line. We keep the
 * same 4-row × 0x100 layout so the draw can walk row 0 / row 1 verbatim. */
static char  cb_text[4 * 0x100];

/* FUN_00434def — open the box and lay the prompt text out. */
void choice_box_open(const char *text, int mode, int sel)
{
    int rows = 0;        /* iVar1 — '<' count                       */
    int col  = 0;        /* iVar4 — column within the current row   */
    int src  = 0;        /* iVar2 — read index into `text`          */
    int base = 0;        /* iVar3 — current row's 0x100 byte base   */

    memset(cb_text, 0, sizeof(cb_text));
    while (text[src] != '\0') {
        if (text[src] == '<') {           /* row break: skip "<tag" header */
            col   = 0;
            rows += 1;
            base += 0x100;
            src  += 4;
        }
        col += 1;
        /* store at base+col (so row r begins at offset r*0x100 + 1) */
        if (base + col < (int)sizeof(cb_text))
            cb_text[base + col] = text[src];
        src += 1;
        if (src == 0x100)
            break;
    }

    cb_close = 0;                 /* DAT_0438ac14 = 0          */
    cb_mode  = mode;              /* DAT_0438b144 = param_2    */
    cb_b14c  = 0.0f;              /* _DAT_0438b14c = 0.0       */
    cb_rows  = rows + 1;          /* DAT_0438ac08 = iVar1 + 1  */
    cb_active = 1;                /* DAT_0438af34 = 1          */
    cb_sel   = sel;               /* DAT_0438ac24 = param_3    */
    if (cb_rows == 2)             /* 2 rows → shift options down */
        cb_b14c = 8.0f;
    cb_bob   = 0;
}

/* FUN_00434ed2 — per-frame nav / confirm / close. `edge` is DAT_073dddd4. */
int choice_box_poll(uint16_t edge, int reset_pos)
{
    (void)reset_pos;   /* the engine only slides the cursor offscreen on close;
                          cosmetic, and our cursor snaps — see choice_box_draw. */

    if (cb_active < 1)
        return CB_INACTIVE;

    cb_bob++;

    /* ── closing animation (DAT_0438ac14 != 0) ── */
    if (cb_close != 0) {
        if (cb_result == 3) {              /* B-cancel: one-frame-per-step close */
            cb_active--;
            if (cb_active != 0)
                return CB_BUSY;
            return CB_OPT1;                /* cancel resolves as option 1 */
        }
        cb_close++;
        if (cb_close < 8)
            return CB_BUSY;
        cb_active--;
        if (cb_active != 0)
            return CB_BUSY;
        return cb_result;                 /* 1 (opt0) or 2 (opt1) */
    }

    /* ── opening animation (DAT_0438ac14 == 0) ── */
    cb_active++;
    if (cb_active < 5)
        return CB_BUSY;                   /* still growing in */
    cb_active = 4;                        /* cap → interactive */

    /* ── input (DAT_073dddd4) ── */
    if (!((edge & CB_BTN_B) && cb_mode == 1)) {
        if (!(edge & CB_BTN_A)) {                 /* no confirm → navigation */
            if ((edge & CB_BTN_RIGHT) && cb_sel == 0) {
                CB_SE(0x146);
                cb_sel ^= 1;                       /* Yes → No */
            }
            if (!(edge & CB_BTN_LEFT))
                return CB_BUSY;
            if (cb_sel != 1)
                return CB_BUSY;
            CB_SE(0x146);
            cb_sel ^= 1;                            /* No → Yes */
            return CB_BUSY;
        }
        /* A pressed → commit */
        if (cb_sel == 0) {
            cb_result = 1;
            CB_SE(0x143);
        } else {
            cb_result = 2;
            CB_SE(0x13d);
        }
    } else {                                       /* B in mode 1 → cancel */
        cb_result = 3;
        CB_SE(0x13d);
    }
    cb_close = 1;                                  /* start the close anim */
    return CB_BUSY;
}

int choice_box_active(void)
{
    return cb_active > 0;
}

void choice_box_reset(void)
{
    cb_active = 0;
    cb_sel    = 0;
    cb_close  = 0;
    cb_result = 0;
}

int         choice_box_selection(void) { return cb_sel; }
int         choice_box_anim(void)      { return cb_active; }
int         choice_box_options(void)   { return cb_rows; }
const char *choice_box_text(void)      { return cb_text + 1; }   /* &DAT_0438af3c */

#ifdef _WIN32
#include "render_quad.h"
#include "font_draw.h"
#include "sysassets.h"
#include <math.h>

/* FUN_0043537e + FUN_00435747 — the box render.
 *   bg   = savewindow.tga (DAT_073d8dc0), 512×128, grows from screen centre
 *   text = centred prompt (FUN_0047d14c) + "Yes"/"No" (FUN_0047ca05)
 *   cursor = a 40×40 hand from nowloading.tga (DAT_073cc770) at the selection,
 *            bobbing horizontally (FUN_00435747's sin wobble). */
void choice_box_draw(struct IDirect3DDevice8 *dev)
{
    if (cb_active < 1)
        return;

    float s = (float)cb_active / 4.0f;             /* open scale, 0.25 → 1.0 */

    /* backdrop banner — savewindow.tga, src (0,0)-(512,128), col 0xff7f7f7f.
     * The engine (FUN_0043537e L31) sets COLOROP = D3DTOP_MODULATE2X (8) for the
     * banner+text, so the 0x7f7f7f (~half) vertex colour reads as full
     * brightness (texel·0.5·2); it resets to MODULATE (4) at L77. Without the
     * 2X the banner is drawn at ~50% under the inherited MODULATE → the prompt
     * looks dim. Mirror the engine: 2X for the banner, MODULATE for everything
     * after (the text/cursor below are full-white, so MODULATE is correct). */
    sprite_t *bg = &g_sysassets.savewindow_tga;
    if (bg->tex != NULL) {
        const float dst[4] = { 320.0f - s * 256.0f, 224.0f - s * 64.0f,
                               s * 512.0f, s * 128.0f };
        const float src[4] = { 0.0f, 0.0f, 512.0f, 128.0f };
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                              D3DTOP_MODULATE2X);
        render_quad_bind(dev, bg);
        render_quad_add(dst, src, bg->width, bg->height, 0xff7f7f7fu);
        render_quad_flush(dev);
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                              D3DTOP_MODULATE);
    }

    /* text + options only once fully open (matches the engine alpha ramp:
     * the prompt/options fade in over the open anim — drawn at cap). */
    if (cb_active >= 4) {
        /* prompt text, centred. FUN_0043537e L62-71: a ONE-row prompt (ac08==1
         * — the skip prompt, which has no '<' delimiters) draws at y=192
         * (0x43400000); a TWO-row prompt draws row 0 at y=184 (0x43380000) and
         * row 1 at y=208 (0x43500000). The port previously always used 184, so
         * the 1-row skip prompt sat 8px too high. */
        if (cb_rows == 1) {
            font_draw_text_centered(dev, 320.0f, 192.0f, cb_text + 1,
                                    0xffffffffu, 1.0f);
        } else {
            font_draw_text_centered(dev, 320.0f, 184.0f, cb_text + 1,
                                    0xffffffffu, 1.0f);
            font_draw_text_centered(dev, 320.0f, 208.0f, cb_text + 1 + 0x100,
                                    0xffffffffu, 1.0f);
        }

        /* "Yes" / "No" — BOTH full brightness. The engine (FUN_0043537e
         * L73-76) draws both at 0x7f7f7f under MODULATE2X = full white; the
         * selection is shown ONLY by the hand cursor, NOT by dimming the
         * unselected option. (The lone 0x7f fade there is a commit-time close
         * anim — deferred.) FUN_0047ca05 at x 252/376. */
        font_draw_text(dev, 252.0f, cb_b14c + 232.0f, "Yes", 0xffffffffu, 1.0f);
        font_draw_text(dev, 376.0f, cb_b14c + 232.0f, "No",  0xffffffffu, 1.0f);

        /* hand cursor — nowloading.tga, 40×40 region (192,0)-(232,40), at the
         * selected option (x = sel*0x7c + 212), bobbing toward it. The engine
         * (FUN_00435747) takes the ABS of the sin: bob = |sin(phase·0.1)|·8, so
         * the hand wobbles LEFT-only (0..8 toward the option) at the |sin|
         * frequency (period π). A plain sin is half that frequency (looked too
         * slow) and swings +8 to the RIGHT of the option (overshoot) — both the
         * user-reported symptoms. */
        sprite_t *cur = &g_sysassets.nowloading_tga;
        if (cur->tex != NULL) {
            float bob = fabsf(sinf((float)cb_bob * 0.1f)) * 8.0f;
            float cx  = (float)(cb_sel * 0x7c) + 212.0f - bob;
            float cy  = cb_b14c + 244.0f - 20.0f;
            const float dst[4] = { cx, cy, 40.0f, 40.0f };
            const float src[4] = { 192.0f, 0.0f, 232.0f, 40.0f };
            render_quad_bind(dev, cur);
            render_quad_add(dst, src, cur->width, cur->height, 0xffffffffu);
            render_quad_flush(dev);
        }
    }
}
#endif /* _WIN32 */
