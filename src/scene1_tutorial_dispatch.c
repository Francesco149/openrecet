/*
 * scene1_tutorial_dispatch.c — see the header.  The iv1_5/iv1_6 branches of
 * FUN_0044bd0d (all.c:45664-45688).
 */
#include "scene1_tutorial_dispatch.h"

#include "scene1_intro_dialogue.h"   /* _busy / _start_single */
#include "save_work.h"               /* save_work_dwords_at / _active_slot  */

#include <stdint.h>
#include <stddef.h>   /* NULL */

/* Shop-display tutorial flags, bank-byte offsets relative to the working record
 * base DAT_044e3798 (same base + scheme the player controller's PC_SHOP_DISPLAY_*
 * bytes use: DAT_0450f3f8 → 0x2bc60, so each +1 byte is the next DAT_0450f3xx). */
#define TUT_BACKROW_OFF        0x2bc63   /* DAT_0450f3fb — placed in row 0 (cond)   */
#define TUT_BACKROW_DONE_OFF   0x2bc64   /* DAT_0450f3fc — iv1_5 fired (done)        */
#define TUT_ALLFILLED_OFF      0x2bc65   /* DAT_0450f3fd — all stands filled (cond)  */
#define TUT_ALLFILLED_DONE_OFF 0x2bc66   /* DAT_0450f3fe — iv1_6 fired (done)        */
#define TUT_ALLFILLED_DONE2_OFF 0x2bc67  /* DAT_0450f3ff — iv1_6 fired (done, pair)  */
#define TUT_IV1_7_TRIG_OFF     0x2bc68   /* DAT_0450f400 — cs-close sets it → iv1_7  */
#define TUT_IV1_7_DONE_OFF     0x2bc69   /* DAT_0450f401 — iv1_7 fired (done)        */
#define TUT_IV1_7_F406_OFF     0x2bc6e   /* DAT_0450f406 — → iv1_8 trigger (P3)      */

/* The post-first-sale story chain (all.c:45726-45813) — each entry fires when
 * its predecessor's flag pair unlocks and the dialogue runtime is idle, and
 * arms the NEXT via the trailing flag write.  f4XX → bank byte 0x2bc68 +
 * (XX − 0x400). */
#define TUT_IV1_8_TRIG_OFF     0x2bc6a   /* DAT_0450f402 — cs LEAVE sets it (f406→f402) */
#define TUT_IV1_8_DONE_OFF     0x2bc6b   /* DAT_0450f403 — iv1_8 fired (done)        */
#define TUT_F407_OFF           0x2bc6f   /* DAT_0450f407 — set with iv1_8            */
#define TUT_F408_OFF           0x2bc70   /* DAT_0450f408 — cleared at the day advance */
#define TUT_IV2_1_TRIG_OFF     0x2bc71   /* DAT_0450f409 — set with iv1_8            */
#define TUT_IV2_1_DONE_OFF     0x2bc72   /* DAT_0450f40a — iv2_1 fired (done)        */
#define TUT_IV2_2_TRIG_OFF     0x2bc73   /* DAT_0450f40b — set with iv2_1            */
#define TUT_IV2_2_DONE_OFF     0x2bc74   /* DAT_0450f40c — iv2_2 fired (done)        */
#define TUT_IV2_3_TRIG_OFF     0x2bc75   /* DAT_0450f40d — set with iv2_2            */
#define TUT_IV2_3_DONE_OFF     0x2bc76   /* DAT_0450f40e — iv2_3 fired (done)        */
#define TUT_IV2_5_TRIG_OFF     0x2bc78   /* DAT_0450f410 — set at the day advance    */
#define TUT_IV2_5_DONE_OFF     0x2bc79   /* DAT_0450f411 — iv2_5 fired (done)        */
#define TUT_IV2_6_TRIG_OFF     0x2bc7a   /* DAT_0450f412 — set with iv2_5            */
#define TUT_IV2_6_DONE_OFF     0x2bc7b   /* DAT_0450f413 — iv2_6 fired (done)        */
#define TUT_F414_OFF           0x2bc7c   /* DAT_0450f414 — set with iv2_6            */
#define TUT_F3F2_OFF           0x2bc5a   /* DAT_0450f3f2 — set at the day advance    */
#define TUT_F3F7_OFF           0x2bc5f   /* DAT_0450f3f7 — cleared at the day advance */
#define TUT_F3F9_OFF           0x2bc61   /* DAT_0450f3f9 — cleared at the day advance */

/* Day-block dwords (bank dword index; the fb7x/fb8x per-save stats block). */
#define TUT_DAY_DWORD          (0x2c3ec / 4)   /* DAT_0450fb84 — day counter (HUD shows +1) */
#define TUT_SHOPTIME_DWORD     (0x2c3f0 / 4)   /* DAT_0450fb88 — shop-time/activity counter */

/* iv2_5→iv2_6 idle-beat length: retail gates the cascade behind
 * `DAT_0438b924 < 0xbe` (all.c:45489), and b924 counts one per free-roam frame from
 * the iv2_5 arm (b924=0) — so iv2_6 fires the frame b924 reaches 0xbe.  Measured:
 * b924 0@15470 → 189@15659 (retail drive 161316Z), iv2_6 load @15659. */
#define IV2_BEAT_FRAMES 0xbe   /* 190 */

/* The transient "Recette looks up at Tear" beat (retail DAT_0438b928 / DAT_0438b924).
 * Armed at the iv2_5 fire, counted once per free-roam dispatch tick (= retail's
 * master-tick b924++, which only runs in the default/free-roam arm — during a
 * dialogue the sim takes the event arm and neither this tick nor b924 advances). */
static int g_iv2_beat_active = 0;   /* DAT_0438b928 == 1 */
static int g_iv2_beat_ctr    = 0;   /* DAT_0438b924       */

/* The opening-scene selector the dispatcher writes (DAT_005c7a2c = 1). */
#define TUT_SCENE 1
#define TUT_SCENE_IV2 2
#define TUT_SUB_IV1_5 5
#define TUT_SUB_IV1_6 6
#define TUT_SUB_IV1_7 7
#define TUT_SUB_IV1_8 8

void scene1_tutorial_dispatch_tick(void)
{
    /* Gate: no dialogue armed/loading/active (retail DAT_0438b1c8 == 0).  Uses
     * _busy() rather than _active()||_loading() because the latter has a 1-frame
     * hole at the D_TUT_LOAD→D_TUT lazy-load seam (loading already false, active
     * not yet true) through which iv1_6 would fire and clobber iv1_5.  Blocks
     * during the prologue too; serialises iv1_5 → iv1_6 (iv1_6 only once iv1_5 has
     * fully completed and g_state returns to D_IDLE). */
    if (scene1_intro_dialogue_busy())
        return;

    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return;
    uint8_t *bb = (uint8_t *)bank;

    /* iv1_5 (all.c:45666): the window-counter placement dialogue.  Priority over
     * iv1_6 (if/else-if), so when a single placement sets BOTH flags this fires
     * first; iv1_6 then fires once this one completes and the gate reopens. */
    if (bb[TUT_BACKROW_DONE_OFF] == 0 && bb[TUT_BACKROW_OFF] == 1) {
        scene1_intro_dialogue_start_single(TUT_SCENE, TUT_SUB_IV1_5);
        bb[TUT_BACKROW_DONE_OFF] = 1;        /* DAT_0450f3fc = 1 */
        return;
    }

    /* iv1_6 (all.c:45677): the "all wares displayed" dialogue. */
    if (bb[TUT_ALLFILLED_DONE_OFF] == 0 && bb[TUT_ALLFILLED_OFF] == 1) {
        scene1_intro_dialogue_start_single(TUT_SCENE, TUT_SUB_IV1_6);
        bb[TUT_ALLFILLED_DONE_OFF]  = 1;     /* DAT_0450f3fe = 1 */
        bb[TUT_ALLFILLED_DONE2_OFF] = 1;     /* DAT_0450f3ff = 1 */
        return;
    }

    /* iv1_7 (all.c:45715): the post-tutorial wrap-up dialogue ("And that is,
     * essentially, how it goes…").  In retail an INDEPENDENT `if` after the
     * iv1_5/iv1_6 block (not the else-if chain), gated the same way (b1c8==0 →
     * _busy()).  Fires once the SELL tutorial closes: the cs leave/dissolve
     * (FUN_00462403 all.c:60389, mirrored in customer_service.c) sets f400=1 when
     * f404 (sell-active) clears.  f400 has EXACTLY ONE writer (that close), so it
     * is 0 at the cad868 LOAD (RE §12, probe-confirmed 3014/3014 free-roam rows
     * f400==0) → NO premature fire during the load — the frame-231 hang the first
     * P2 attempt saw was an env confound, not this branch.  Sets f401 (done) +
     * f406 (→ iv1_8 "now sit at the counter", the P3 first-customer entry). */
    if (bb[TUT_IV1_7_DONE_OFF] == 0 && bb[TUT_IV1_7_TRIG_OFF] == 1) {
        scene1_intro_dialogue_start_single(TUT_SCENE, TUT_SUB_IV1_7);
        bb[TUT_IV1_7_DONE_OFF] = 1;          /* DAT_0450f401 = 1 */
        bb[TUT_IV1_7_F406_OFF] = 1;          /* DAT_0450f406 = 1 */
        return;
    }

    /* The iv2_5→iv2_6 idle beat.  Mirrors FUN_0044bd0d's `if (b928==1 &&
     * b924 < 0xbe) bVar1 = false; ... if (!bVar1) return;` (all.c:45489/45507):
     * while the beat holds, the whole scenario cascade below is withheld, so
     * iv2_6 (the DAY2 load) can't fire.  b924 increments in the master tick every
     * free-roam frame; this dispatch tick runs exactly on those frames (a dialogue
     * routes the sim to the event arm, which skips this tick), so counting here
     * matches retail's cadence.  Increment-then-test (retail bumps b924 in the
     * master tick, then FUN_0044bd0d tests the bumped value the same frame): the
     * beat holds for b924 = 1..189 and releases the frame b924 hits 0xbe. */
    if (g_iv2_beat_active) {
        g_iv2_beat_ctr++;
        if (g_iv2_beat_ctr < IV2_BEAT_FRAMES)
            return;
        g_iv2_beat_active = 0;   /* beat elapsed — fall through, fire iv2_6 */
    }

    /* ── The post-first-sale story chain (all.c:45726-45813, RE §21.31) ──────
     * iv1_8 → iv2_1 → iv2_2 → iv2_3 (the DAY ADVANCE) → iv2_5 → iv2_6, the
     * day-1-evening → day-2 cutscene series the day2 trace replays.  Retail's
     * cascade is if (f403==0) {iv1_8} else if … — mirrored exactly.  Each
     * retail entry also runs FUN_00452d07(N) (the dialogue load worker —
     * start_single models the bracket; N selects worker assets, not modeled)
     * and, where noted, FUN_00452809 (the screen-blackout arm —
     * PORT-DEBT(blackout-tut-dispatch), scene1_intro_dialogue_blackout_active
     * exists unwired). */
    if (bb[TUT_IV1_8_DONE_OFF] == 0) {
        if (bb[TUT_IV1_8_TRIG_OFF] == 1) {   /* f402: set at the cs LEAVE */
            scene1_intro_dialogue_start_single(TUT_SCENE, TUT_SUB_IV1_8);
            bb[TUT_IV1_8_DONE_OFF] = 1;      /* DAT_0450f403 = 1 */
            bb[TUT_IV2_1_TRIG_OFF] = 1;      /* DAT_0450f409 = 1 */
            bb[TUT_F407_OFF]       = 1;      /* DAT_0450f407 = 1 */
        }
    } else if (bb[TUT_IV2_1_DONE_OFF] == 0) {
        if (bb[TUT_IV2_1_TRIG_OFF] == 1) {
            scene1_intro_dialogue_start_single(TUT_SCENE_IV2, 1);
            bb[TUT_IV2_1_DONE_OFF] = 1;      /* DAT_0450f40a = 1 */
            bb[TUT_IV2_2_TRIG_OFF] = 1;      /* DAT_0450f40b = 1 */
        }
    } else if (bb[TUT_IV2_2_DONE_OFF] == 0) {
        if (bb[TUT_IV2_2_TRIG_OFF] == 1) {
            scene1_intro_dialogue_start_single(TUT_SCENE_IV2, 2);
            bb[TUT_IV2_2_DONE_OFF] = 1;      /* DAT_0450f40c = 1 */
            bb[TUT_IV2_3_TRIG_OFF] = 1;      /* DAT_0450f40d = 1 */
        }
    } else if (bb[TUT_IV2_3_DONE_OFF] == 0) {
        if (bb[TUT_IV2_3_TRIG_OFF] == 1) {
            /* iv2_3 = the DAY ADVANCE (all.c:45765-45783): day++ + the
             * day-block resets that reopen the shop for day 2. */
            scene1_intro_dialogue_start_single(TUT_SCENE_IV2, 3);
            bb[TUT_IV2_3_DONE_OFF] = 1;      /* DAT_0450f40e = 1 */
            bank[TUT_DAY_DWORD] += 1;        /* DAT_0450fb84 += 1 (day counter) */
            bank[TUT_SHOPTIME_DWORD] = 0;    /* DAT_0450fb88 = 0 */
            bb[TUT_F3F9_OFF] = 0;            /* DAT_0450f3f9 = 0 */
            bb[TUT_F408_OFF] = 0;            /* DAT_0450f408 = 0 */
            bb[TUT_F3F2_OFF] = 1;            /* DAT_0450f3f2 = 1 */
            bb[TUT_F3F7_OFF] = 0;            /* DAT_0450f3f7 = 0 */
            bb[TUT_IV1_7_TRIG_OFF] = 0;      /* DAT_0450f400 = 0 */
            /* _DAT_0438b7d4 = 0 — the smoothed shop-time follower
             * (sim-step-a L304, unported cosmetic timer). */
            bb[TUT_IV2_5_TRIG_OFF] = 1;      /* DAT_0450f410 = 1 */
        }
    } else if (bb[TUT_IV2_5_DONE_OFF] == 0) {
        if (bb[TUT_IV2_5_TRIG_OFF] == 1) {
            scene1_intro_dialogue_start_single(TUT_SCENE_IV2, 5);
            bb[TUT_IV2_5_DONE_OFF] = 1;      /* DAT_0450f411 = 1 */
            bb[TUT_IV2_6_TRIG_OFF] = 1;      /* DAT_0450f412 = 1 */
            /* Arm the "Recette looks up at Tear" beat (retail 45798-99:
             * DAT_0438b928=1, DAT_0438b924=0).  The gate at the top of the iv2
             * cascade then withholds iv2_6 (the DAY2 load) for IV2_BEAT_FRAMES of
             * free-roam, and the pose driver holds the pose across it — closing
             * the 189f DAY2-entry drift (Residual B) + the port-only pose blip
             * (Residual A).  FUN_00452809 (blackout) still PORT-DEBT(blackout-
             * tut-dispatch); FUN_004852fb's day-8/15-gated flag set is inert here
             * (day 1→2 → the fb84==8 gate never binds; f470 stays 0, verified). */
            g_iv2_beat_active = 1;           /* DAT_0438b928 = 1 */
            g_iv2_beat_ctr    = 0;           /* DAT_0438b924 = 0 */
        }
    } else if (bb[TUT_IV2_6_DONE_OFF] == 0 && bb[TUT_IV2_6_TRIG_OFF] == 1) {
        scene1_intro_dialogue_start_single(TUT_SCENE_IV2, 6);
        bb[TUT_IV2_6_DONE_OFF] = 1;          /* DAT_0450f413 = 1 */
        bb[TUT_F414_OFF]       = 1;          /* DAT_0450f414 = 1 */
    }
}

int scene1_tutorial_dispatch_iv2_beat_active(void)
{
    return g_iv2_beat_active;
}

int scene1_tutorial_dispatch_iv2_beat_ctr(void)
{
    return g_iv2_beat_ctr;
}

void scene1_tutorial_dispatch_reset(void)
{
    g_iv2_beat_active = 0;
    g_iv2_beat_ctr    = 0;
}
