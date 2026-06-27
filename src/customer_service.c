/*
 * customer_service.c — see customer_service.h.
 *
 * The cc08==4 in-shop customer-service / selling mode.  Transcribed from the
 * unpacked disassembly + docs/decompiled (the integer bookkeeping is faithful in
 * the decompile; the haggle FP lives in customer_haggle.c).  The TUTORIAL haggle
 * is the kind-2 machine FUN_004658ab (NOT the kind-4 FUN_00463cfb — see the RE
 * doc §3.5 correction); this file ports the entry (FUN_0045edaa) first.
 */

#include <stdio.h>            /* snprintf — the <I>/<Y> sale-line macro formats */

#include "customer_service.h"
#include "customer_haggle.h"
#include "rng.h"
#include "save_work.h"
#include "save_bank.h"
#include "scene_buy.h"        /* g_scene_buy_current_page == engine DAT_0730b56c */
#include "worker_load.h"      /* worker_load_spawn_d3e == FUN_00452d3e */
#include "tables_kyaku.h"     /* g_kyaku — the customer tuning fields */
#include "customer_dialogue.h" /* kyaku_dialogue_get — per-kyaku fN.txt lines (L1c) */
#include "dialogue_macros.h"  /* dlg_macro_set — the <I>/<Y> sale-line macros */
#include "tables_item.h"      /* g_item / tables_item_find_slot_by_id (FUN_004681f6) */
#include "tables_tuto.h"      /* g_tuto — the scripted-sell script (FUN_00461c00 consumer) */
#include "customer_haggle.h"  /* haggle_offer_up (FUN_00460161) */
#include "scene1_shop_display.h"  /* SHOP_DISPLAY_TIER_SELECTOR (0xb378) */
#include "scene1_camera.h"        /* scene1_camera_cs_counter_cam (cc08==4 counter cam) */
#include "scene1_player_ctrl.h"   /* player_ctrl_cc08_enter_freeroam (b520 leave → cc08=1) */
#include "scene1_shop_walker.h"   /* in-shop browsing-customer NPCs (FUN_0046f8ba/892) */
#include "scene1_particles_tick.h" /* g_scene1_player_pos (DAT_056da1d8) — the leave hop-down reposition */
#include "choice_box.h"           /* the ESC "Cancelling tutorial?" prompt (FUN_0045e6a5) */
#include "fade.h"                 /* fade_phase1_start/_is_done/_phase_out_start (b520 dissolve) */

/* ── per-frame input masks (the engine's DAT_073dddd0/d4/d6 button quad) ──────
 * The cc08==4 driver reads three masks: cur (DAT_073dddd0, this frame's raw
 * accumulated buttons), pressed (DAT_073dddd4, rose this frame: 0x10=Z/A,
 * 0x20=X/B, 0x0c=up/down, 0x40=details), held (DAT_073dddd6, held-with-repeat:
 * bits 1/2/4/8 = L/R/U/D for the digit editor).  Passed into the tick each frame
 * so the logic is host-testable with scripted inputs. */
static uint32_t s_in_cur;       /* DAT_073dddd0 */
static uint32_t s_in_pressed;   /* DAT_073dddd4 */
static uint32_t s_in_held;      /* DAT_073dddd6 */

/* ── save-bank byte offsets of the customer-service flags (rel. DAT_044e3798) ──
 * DAT_0450f400 = displays-suppressed (PC_SHOP_DISPLAY_SUPPRESS_BYTE_OFF 0x2bc68),
 * so the sell flags sit just after it. */
#define CS_F404_SELL_ACTIVE_BYTE_OFF 0x2bc6c   /* DAT_0450f404 — sell-active flag */
#define CS_F406_TUTORIAL_BYTE_OFF    0x2bc6e   /* DAT_0450f406 — forced/tutorial sale */
/* the b520-leave (customer-service exit) flag clears (offsets vs DAT_044e3798;
 * verified by objdump of FUN_00462403 @ 0x462ae0-0x462b0f). */
#define CS_F3FF_BYTE_OFF             0x2bc67   /* DAT_0450f3ff */
#define CS_F400_DISPLAY_SUPPRESS_OFF 0x2bc68   /* DAT_0450f400 — shop-display suppress */
#define CS_F402_BYTE_OFF             0x2bc6a   /* DAT_0450f402 */
#define CS_F405_BYTE_OFF             0x2bc6d   /* DAT_0450f405 */

/* The display grid is 15 rows × 20 cols (= 300 dwords) at SAVE_BANK_FIELD_DISPLAY_GRID. */
#define CS_DISPLAY_GRID_ROWS 15
#define CS_DISPLAY_GRID_COLS 20

/* ── the engine customer-service state block (DAT_0730aXXX / DAT_0730bXXX) ─────
 * Modeled as named module statics (house style).  The customer index b56c is the
 * shared engine global g_scene_buy_current_page (scene_buy.c), reused here. */

/* Customer queue — DAT_0730aca0[], stride 6 dwords/entry: {kyaku, item_slot,
 * kind, _, _, _}.  30 entries (span aca0..af78). */
#define CS_QUEUE_ENTRIES 30
#define CS_QUEUE_STRIDE  6
static int32_t s_queue[CS_QUEUE_ENTRIES * CS_QUEUE_STRIDE]; /* DAT_0730aca0 */
static int32_t s_queue_count;                               /* DAT_0730ac98 */

/* Customer-roster permutation — DAT_0730b1a8[50], filled 0,1,2,…  (the
 * eligible-sort scratch; tutorial leaves it as the identity). */
#define CS_ROSTER_PERM_N 50
static int32_t s_roster_perm[CS_ROSTER_PERM_N];             /* DAT_0730b1a8 */

/* Eligible-customer list — DAT_06a5d450[50], terminated by a negative.  Tutorial:
 * {13, -2, -1, …}. */
#define CS_ELIGIBLE_N 50
static int32_t s_eligible[CS_ELIGIBLE_N];                   /* DAT_06a5d450 */

/* Item-pick array — DAT_0730b274[], stride 3 dwords/entry: {id, col, row}.
 * NB the master tick reuses [0].col/[0].row (DAT_0730b278/b27c) as the 2
 * per-customer arrival timers — same storage, temporally separate. */
#define CS_ITEMPICK_N 6
static int32_t s_item_pick[CS_ITEMPICK_N * 3];             /* DAT_0730b274 */

/* per-on-screen-customer active flags — DAT_06a5ea70[2] (the 2 dwords just
 * before the kyaku records at 0x6a5ea90). */
static int32_t s_cust_active[2];                           /* DAT_06a5ea70/74 */

/* ── the b5xx scalar state (the subset the entry + machine touch) ──────────── */
static int32_t s_b318;   /* DAT_0730b318 — queue head/scratch */
static int32_t s_b51c;   /* DAT_0730b51c — debug all-customers latch */
static int32_t s_b520;   /* DAT_0730b520 — leave/dissolve phase */
static int32_t s_b524;   /* DAT_0730b524 — idle/intro counter */
static int32_t s_b528;   /* DAT_0730b528 — queue advance scratch */
static int32_t s_b52c;   /* DAT_0730b52c — closing countdown */
static int32_t s_b530;   /* DAT_0730b530 — sold-pause counter */
static int32_t s_b534;   /* DAT_0730b534 — SELL sub-state */
static int32_t s_b538;   /* DAT_0730b538 — "too expensive" pushback latch */
static int32_t s_b53c;   /* DAT_0730b53c — short flash timer */
static int32_t s_b558;   /* DAT_0730b558 — greeting Z-gate */
static int32_t s_b564;   /* DAT_0730b564 — bubble-emit enable */
static int32_t s_b568;   /* DAT_0730b568 */
static int32_t s_b58c;   /* DAT_0730b58c — per-round input delay */
static int32_t s_b590;   /* DAT_0730b590 — patience */
static int32_t s_b594;   /* DAT_0730b594 */
static int32_t s_b598;   /* DAT_0730b598 */
static int32_t s_b59c;   /* DAT_0730b59c — reaction phase */
static int32_t s_b5a0;   /* DAT_0730b5a0 — arrival-anim counter */
static int32_t s_b5ac;   /* DAT_0730b5ac */
static int32_t s_b5a8;   /* DAT_0730b5a8 — transaction-type selector (-1 init) */
static int32_t s_b5b0;   /* DAT_0730b5b0 */
static int32_t s_b5b4;   /* DAT_0730b5b4 — leave/dissolve frame counter */
static int32_t s_b5b8;   /* DAT_0730b5b8 */
static int32_t s_b5bc;   /* DAT_0730b5bc */
static int32_t s_b5c0;   /* DAT_0730b5c0 */
static int32_t s_b5c4;   /* DAT_0730b5c4 */
static int32_t s_b5c8;   /* DAT_0730b5c8 — item-list scroll */
static int32_t s_b5cc;   /* DAT_0730b5cc — sub-menu open */
static int32_t s_b5d0;   /* DAT_0730b5d0 — pose state */
static int32_t s_b5d4;   /* DAT_0730b5d4 — pose timer */
static int32_t s_b5d8;   /* DAT_0730b5d8 — resolved want-list line index (render) */
static int32_t s_b5dc;   /* DAT_0730b5dc — button-row count (render) */
static int32_t s_b5e4;   /* DAT_0730b5e4 — ESC "Cancelling tutorial?" skip-armed flag */
/* The cc08==4 ESC-skip choice box's b150 hold.  Retail's DAT_0438b150 is set by
 * the cancel-prompt choice_box_open and is NOT cleared by the "Yes" (b5e4→0);
 * it persists through the leave/dissolve → wrap-up → the first customer's cc08
 * re-entry, and is finally cleared by the live machine's FUN_00435612 when the
 * greeting advances to b534==2 (sell-greeting).  rec-20260622: b150 1→0 exactly
 * at the b534 1→2 edge (retail frames 15163→15164).  The port "split" b150 so
 * b5e4 alone closed PAUSE_OPEN at the Yes — this latch reproduces the full hold
 * so a raw-retail ESC-skip→first-customer recording replays past the skip. */
static int32_t s_skip_modal;

/* ── master-tick + scripted-machine scalar state (Chip 2) ─────────────────────
 * The subset of the DAT_0730bXXX block the master tick (FUN_00462403) + the
 * scripted-sell machine (FUN_00461c00) drive beyond Chip 1's entry set. */
static int32_t s_b540;   /* DAT_0730b540 — digit cursor / Yes-No toggle */
static int32_t s_b544;   /* DAT_0730b544 — per-state sub-frame timer (==1 first frame) */
static int32_t s_b548;   /* DAT_0730b548 — text-reveal budget (climbs 1 display-char/frame) */
static int32_t s_b54c;   /* DAT_0730b54c — per-line voice/file id [0] */
static int32_t s_b550;   /* DAT_0730b550 — per-line scratch [1] */
static int32_t s_b55c;   /* DAT_0730b55c — line-done / Z-advance gate */
static char        s_line_buf[0x100];   /* DAT_0730b31c — the active (pre-<C>) line text */
static char        s_line_tail[0x100];  /* DAT_0730b41c — the post-<C> continuation */
static const char *s_b270;              /* DAT_0730b270 — the active line pointer */
static int32_t s_b560;   /* DAT_0730b560 — price-digit cursor (FUN_0045ff11) */
static int32_t s_b570;   /* DAT_0730b570 — the item slot being transacted */
static int32_t s_b5a4;   /* DAT_0730b5a4 — offered-item handle (id = b5a4>>6) */
static int32_t s_b600;   /* DAT_0730b600 — script step phase (0 = first frame) */
static int32_t s_b604;   /* DAT_0730b604 — script program counter */
static int32_t s_b608;   /* DAT_0730b608 — script sub-state */
/* haggle working state (the FUN_00460161 binding; driven in Chip 2b). */
static int32_t s_b574;   /* DAT_0730b574 — customer's current offer */
static int32_t s_b57c;   /* DAT_0730b57c — working price (seeded = base) */
static int32_t s_b580;   /* DAT_0730b580 — haggle floor */
static int32_t s_b584;   /* DAT_0730b584 — haggle round (0 = first offer) */
static int32_t s_b588;   /* DAT_0730b588 — accept-test reference */
/* load-worker phase (DAT_0438b1cc): 2 = the cc08==4 asset-load worker (d3e) is
 * running ⇒ the master tick is inert until the load callback clears it. */
static int32_t s_b1cc;   /* DAT_0438b1cc */

/* {csloadpin:N} — trace-harness pin for the cc08==4 d3e asset-load bracket: hold
 * b1cc==2 for exactly N frames on BOTH targets (the Frida agent extends retail's
 * real worker-thread bracket to the same N) so the 目玉 sparkle — which fires
 * THROUGHOUT the b1cc==2 window (player_ctrl_display_sparkle_emit, %8-gated, NOT
 * gated on the load) — consumes the SAME rng count on both sides.  The engine
 * load is a CreateThread race with no min-duration gate (nowloading.c), so its
 * duration is non-deterministic run-to-run (port ~15-18f, retail ~7f); pinning
 * normalizes it for trace comparison, exactly like {phasepin}/{tutloadpin}.
 * 0 = unset (ship behaviour: clear on the async worker).  See RE §20. */
static int32_t s_csload_pin  = 0;   /* pinned bracket length (0 = unset) */
static int32_t s_csload_hold = 0;   /* frames elapsed in the current b1cc==2 bracket */

/* shared price scalars (DAT_005c6bXX). */
static int32_t s_price_fileidx;  /* DAT_005c6bb0 — active dialogue-file index */
static int32_t s_price_bb4;      /* DAT_005c6bb4 — committed/prev asking price (-1 = none) */
static int32_t s_price_ask;      /* DAT_005c6bb8 — player's asking price */
static int32_t s_price_runsum;   /* DAT_005c6bbc — running base-price sum / item base */
static int32_t s_price_base;     /* DAT_005c6bc0 — base/reference price (= runsum*count) */
static int32_t s_price_bc4;      /* DAT_005c6bc4 — item count (base = runsum*count) */
static int32_t s_price_bc8;      /* DAT_005c6bc8 — alt item handle (-1 = none) */
static int32_t s_price_cursor;   /* DAT_005c6bcc — item-pick cursor */

/* customer-service-active global (DAT_0438b7b0) — set 1 on session init. */
static int32_t s_cs_active;      /* DAT_0438b7b0 */

/* ── FUN_0046f8ba — build the in-shop browsing-customer roster + cap ──────────
 * For each eligible-list entry, matches the kyaku id against the DAT_005c7ce0
 * (char_id,key) sprite-type table and appends the matched table index to the
 * roster (DAT_073a7f30), setting DAT_005c7dd0 = the spawn cap.  The cc08==4
 * per-frame pump (scene1_customer_npc_pump, FUN_0047019f) then spawns + wanders
 * that many chibi customers across the shop floor.  Also resets the NPC array
 * + the spawn/frame counters (engine FUN_0046f892) so the new session starts
 * with all slots free.  No RNG (the pump consumes it). */
static void cs_load_eligible_portraits(const int32_t *eligible)
{
    scene1_customer_npc_reset();              /* FUN_0046f892 — free slots */
    scene1_customer_npc_roster_build(eligible); /* FUN_0046f8ba — cap + roster */
}

/* ── customer_service_session_init — FUN_0045edaa (TUTORIAL path) ───────────── */
void customer_service_session_init(void)
{
    const uint8_t *bank = (const uint8_t *)save_work_dwords_at(save_work_active_slot());

    /* Free the in-shop chibi-NPC slots for EVERY session-init path.  Retail resets
     * the NPC array (FUN_0046f892) at the HOUSE scene load (all.c:34885) + the cs
     * leave (60337) — both of which precede every cc08==4 entry — so the slots are
     * all-free (ACTIVE==-1) before any pump (FUN_0047019f) tick.  The port does NOT
     * port the house-load reset, and only the forced-sale path reset them (via
     * cs_load_eligible_portraits); the scripted-tutorial (sell_active) and the
     * general first-customer (roster-scan, PORT-DEBT(cs-roster-scan)) paths left the
     * slots zero-init (ACTIVE==0, read as "active"), so the now-unconditional pump
     * ticked up to 30 GHOST slots → spurious LCG draws retail never makes.  Reset
     * here, before the per-session state, so all paths start with retail's free
     * slots + a zeroed spawn cadence (the forced-sale path resets again in
     * cs_load_eligible_portraits before building its roster — harmless). */
    scene1_customer_npc_reset();    /* FUN_0046f892 effect (house-load + leave mirror) */

    /* ── shared prologue (all.c:57374-57413): zero the per-session state ── */
    s_b520 = 0;
    /* FUN_0047f1ce() — rebuild the unlocked-party-model list (DAT_0741bed8);
     *   0 in the tutorial (no party yet).  PORT-DEBT(cs-party-list): not modeled
     *   here (the pause-menu Status stub already treats DAT_0741bed8 as 0). */
    /* FUN_004681d3() — DAT_0734b96c = 0 (display-menu reset); inert pre-render. */
    s_b5b4 = 0;
    s_cs_active = 1;                 /* DAT_0438b7b0 = 1 */
    s_b5a8 = -1;                     /* DAT_0730b5a8 = -1 */
    s_b524 = 0;
    s_b528 = 0;
    s_b52c = 0;
    s_b530 = 0;
    s_b534 = 0;
    s_b53c = 0;
    s_b564 = 0;
    /* DAT_0730b544 (sub-frame timer) lives in the master-tick chip; zero it via
     * the block reset below. */
    s_cust_active[0] = 0;            /* DAT_06a5ea70 */
    s_cust_active[1] = 0;            /* DAT_06a5ea74 */
    s_b558 = 0;
    s_b5ac = 0;
    s_b568 = 0;
    s_b58c = 0;
    s_b590 = 0;
    s_b594 = 0;
    s_b598 = 0;
    s_b59c = 0;
    s_b5a0 = 0;
    s_price_cursor = 1;              /* DAT_005c6bcc = 1 */
    s_b5c8 = 0;
    s_b5cc = 0;
    s_b5d0 = 0;
    s_b5d4 = 0;
    s_item_pick[1] = 0;             /* DAT_0730b278 = 0 (item-pick[0].col / timer0) */
    s_item_pick[2] = 0;             /* DAT_0730b27c = 0 (item-pick[0].row / timer1) */
    s_b5b0 = 0;
    s_b5b8 = 0;
    s_b5bc = 0;
    s_b5c0 = 0;
    s_b5c4 = 0;

    /* queue init (all.c:57415-57422): 30 entries, {kyaku=-1, item_slot=entry, [2]=-1}. */
    for (int e = 0; e < CS_QUEUE_ENTRIES; e++) {
        s_queue[e * CS_QUEUE_STRIDE + 0] = -1;   /* kyaku */
        s_queue[e * CS_QUEUE_STRIDE + 1] = e;    /* item_slot (default = entry idx) */
        s_queue[e * CS_QUEUE_STRIDE + 2] = -1;   /* kind */
    }
    s_b318 = 0;

    /* item-pick[1..5] init (all.c:57424-57430): stride 3, all -1. */
    for (int i = 1; i < CS_ITEMPICK_N; i++) {
        s_item_pick[i * 3 + 0] = -1;
        s_item_pick[i * 3 + 1] = -1;
        s_item_pick[i * 3 + 2] = -1;
    }

    /* count items on display (all.c:57431-57444): the 15×20 display grid. */
    int displayed = 0;
    if (bank != NULL) {
        const int32_t *grid =
            (const int32_t *)(bank) + SAVE_BANK_FIELD_DISPLAY_GRID;
        for (int i = 0; i < CS_DISPLAY_GRID_ROWS * CS_DISPLAY_GRID_COLS; i++)
            if (grid[i] != -1)
                displayed++;
    }
    /* shop-tier bonus (all.c:57445-57454). */
    int tier = (bank != NULL)
             ? (int)((const int32_t *)bank)[SHOP_DISPLAY_TIER_SELECTOR] : 0;
    if (tier == 1) displayed += 3;
    if (tier == 2) displayed += 4;
    if (tier == 3) displayed += 5;

    /* customer-count RNG draw (all.c:57455-57461) — ONE LCG draw, BEFORE the
     * path branch, so it is consumed on EVERY path (load-bearing for RNG parity).
     * The resulting count is unused on the tutorial path (the forced sale queues
     * exactly one customer), but the draw must still happen. */
    uint32_t r = rng_next15();
    int customer_count = (int)(r & 1u) + 1 + displayed / 4;
    if (customer_count > 5)
        customer_count = 5;
    (void)customer_count;

    int tutorial = (bank != NULL) && bank[CS_F406_TUTORIAL_BYTE_OFF] != 0;
    int sell_active = (bank != NULL) && bank[CS_F404_SELL_ACTIVE_BYTE_OFF] != 0;

    s_b51c = 0;

    /* eligible-list init (all.c:57464-57468): 50 entries = -1. */
    for (int i = 0; i < CS_ELIGIBLE_N; i++)
        s_eligible[i] = -1;

    /* (all.c:57469-57471) if tutorial AND sell-active: clear sell-active. */
    if (tutorial && sell_active)
        sell_active = 0;   /* engine clears the bank byte; mirrored below conceptually */

    if (!sell_active) {
        if (!tutorial) {
            /* PORT-DEBT(cs-roster-scan): the full eligible-roster build
             * (all.c:57474-58212 — scan all 50 kyaku by activity-time / attr /
             * story-flags, sort, fill the queue + the buysell-debug override).
             * Not exercised by the tutorial (the forced sale is the else branch);
             * port it with the general sell loop. */
        } else {
            /* ── TUTORIAL forced sale (all.c:58218-58231): one customer = kyaku 13 ── */
            s_queue_count += 1;                 /* DAT_0730ac98++ */
            s_queue[0 * CS_QUEUE_STRIDE + 1] = 0;   /* queue[0].item_slot = 0 */
            s_queue[0 * CS_QUEUE_STRIDE + 0] = 0xd; /* queue[0].kyaku = 13 (Woman) */
            s_queue[0 * CS_QUEUE_STRIDE + 2] = 0;   /* queue[0].kind = 0 */
            for (int i = 0; i < CS_ROSTER_PERM_N; i++)  /* b1a8[i] = i */
                s_roster_perm[i] = i;
            s_eligible[0] = 0xd;                /* DAT_06a5d450 = 13 */
            s_eligible[1] = -2;                 /* DAT_06a5d454 = -2 (terminator) */
            cs_load_eligible_portraits(s_eligible);  /* FUN_0046f8ba */
        }
    } else {
        /* ── SELL-ACTIVE / scripted-sell path (all.c:58234-58248) ──────────────
         * The player-initiated sell (Z at the counter sets f404) — and the
         * customer-service TUTORIAL runs here too (it's a SCRIPTED sell driven by
         * FUN_00461c00 reading g_tuto, NOT the live kind-machine FUN_004658ab —
         * RE doc §3.7).  Latches b51c=1 (the master tick's b534==1 arm then
         * dispatches the scripted machine every frame), and seeds a 3-deep queue
         * of placeholder customers (kyaku=1, kind=0).  The on-screen customer +
         * the haggle tuning come from the script + the offered item, not kyaku 1
         * (matches the BIT-EXACT capture: b56c=1, b5a8=2). */
        s_b51c = 1;                          /* DAT_0730b51c = 1 */
        for (int e = 0; e < 3; e++) {        /* aca0..ace8 = queue[0..2] */
            s_queue[e * CS_QUEUE_STRIDE + 0] = 1;   /* kyaku = 1 */
            s_queue[e * CS_QUEUE_STRIDE + 2] = 0;   /* kind  = 0 (item_slot left = e) */
        }
        s_queue_count = 3;                   /* DAT_0730ac98 = 3 */
        for (int i = 0; i < CS_ROSTER_PERM_N; i++)  /* b1a8[i] = i */
            s_roster_perm[i] = i;
    }

    /* FUN_00452d3e(0) (all.c:58250): spawn the customer-service asset-load worker
     * (DAT_0438b1cc = 2 = "loading" — the master tick is inert until it clears). */
    s_b1cc = 2;
    s_csload_hold = 0;                       /* {csloadpin} bracket start */
    worker_load_spawn_d3e(0);
}

/* Load-worker completion (DAT_0438b1cc 2 → 1): the cc08==4 asset-load worker's
 * callback calls this when the customer-service assets finish loading.  The
 * engine's d3e worker BODY (LAB_00452ae8/b13) writes DAT_0438b1cc = 1 — NOT 0 —
 * which is the state BOTH the master tick (runs while != 2) AND the render
 * (FUN_0046602e draws while == 1) read.  The wiring (scene1_player_ctrl) calls
 * this once the d3e worker is no longer pending; host tests call it directly. */
void customer_service_notify_loaded(void) { s_b1cc = 1; }

/* Trace-harness `{csloadpin:N}`: pin the cc08==4 d3e load bracket to N frames
 * (N <= 0 clears the pin → ship behaviour).  Set once at segtrace load (main.c);
 * harness-only, never written by game logic. */
void customer_service_set_load_pin(int n) { s_csload_pin = (n > 0) ? n : 0; }

/* Advance the {csloadpin} bracket counter — called once per cc08==4 frame by the
 * load-gate bridge while b1cc==2.  Returns whether the pinned minimum has elapsed
 * (always 1 when no pin is set, so the gate then clears purely on the async
 * worker, exactly as shipped).  The async-done check is ANDed by the caller, and
 * N is chosen > the port's worst-case async load, so the bracket clears at frame
 * N with the d3e assets already in (the sparkle reads the SAVE grid, not the d3e
 * assets, so its rng is deterministic regardless of asset readiness). */
int customer_service_load_pin_elapsed(void)
{
    if (s_csload_pin <= 0) return 1;
    if (s_csload_hold < s_csload_pin) s_csload_hold++;
    return s_csload_hold >= s_csload_pin;
}

/* ── accessors ─────────────────────────────────────────────────────────────── */
int32_t customer_service_b534(void)        { return s_b534; }
int32_t customer_service_player_ask(void)  { return s_price_ask; }
int32_t customer_service_offer(void)       { return s_b574; }
int32_t customer_service_base_price(void)  { return s_price_base; }
int32_t customer_service_b5a8(void)        { return s_b5a8; }
int32_t customer_service_b56c(void)        { return g_scene_buy_current_page; }
/* DAT_0450f404[slot] — the SELL-ACTIVE flag (1 = a player-initiated sell at the
 * counter; 0 = the autonomous first customer / no sell).  FUN_0048a833 forces
 * its branch selector local_c != 0 on f404 != 0 (by-address 0x48a98b), which
 * selects the at-counter companion arm (co_at_counter_tick); the first customer
 * is f404 == 0 ⇒ the engine runs the free-roam spring-follow (FUN_0048a4d1)
 * instead — see scene1_companion_ctrl_tick. */
int32_t customer_service_f404(void)
{
    const uint8_t *bank =
        (const uint8_t *)save_work_dwords_at(save_work_active_slot());
    return (bank != NULL) && bank[CS_F404_SELL_ACTIVE_BYTE_OFF] != 0;
}
int32_t customer_service_arrival_anim(void){ return s_b5a0; }
int32_t customer_service_round(void)       { return s_b584; }
int32_t customer_service_b520(void)        { return s_b520; }
int32_t customer_service_b524(void)        { return s_b524; }
int32_t customer_service_b544(void)        { return s_b544; }
int32_t customer_service_b590(void)        { return s_b590; }
int32_t customer_service_b1cc(void)        { return s_b1cc; }

/* The cc08==4 d3e asset-load overlay gate (engine DAT_06a49960 = "gate2", raised
 * by FUN_00452d3e alongside b1cc=2 and cleared by the worker reap FUN_00452917).
 * Retail keeps loading_active up THROUGH this load (LOADING_END/HOUSE_FREEROAM
 * fire when it completes, ~off23), so the load-anchored {rngseed} pins land at
 * the same entry-relative offset as the port (RE §21.9).  The port modelled the
 * d3e load (b1cc=2) but never raised the overlay ⇒ its LOADING_END fired at off0
 * (1-frame reload only) and the pins skewed.  b1cc==2 mirrors gate2's lifetime
 * (set at the d3e spawn, cleared when the load — csloadpin-held — completes). */
int customer_service_d3e_loading(void)     { return s_b1cc == 2; }

/* Frame-start snapshot of the d3e-load gate, set by the cc08==4 arm
 * (scene1_player_ctrl_tick) BEFORE its inline notify_loaded clears b1cc, so a
 * consumer that runs LATER in the frame (the companion ctrl in scene1_sim) gates
 * on the load state as it was at frame start — exactly like the master tick's
 * b1cc_pre.  Keeps the companion idle for the WHOLE load incl. the release frame
 * ⇒ its arrival starts the frame AFTER the load clears, matching retail (RE
 * §21.10); without it the companion sees the already-cleared b1cc and arrives 1f
 * early. */
static int s_cs_load_at_frame_start;
void customer_service_note_frame_load(int loading) { s_cs_load_at_frame_start = loading; }
int  customer_service_load_at_frame_start(void)    { return s_cs_load_at_frame_start; }

int32_t customer_service_active(void)      { return s_cs_active; }
int32_t customer_service_b51c(void)        { return s_b51c; }
int32_t customer_service_b608(void)        { return s_b608; }
int32_t customer_service_b604(void)        { return s_b604; }   /* the script PC (g_tuto index) */
int32_t customer_service_fileidx(void)     { return s_price_fileidx; }
/* The BARGAIN price-confirm choice is open — retail sets DAT_0438b150 (the
 * shared modal-cursor flag, → the PAUSE_OPEN anchor) here via choice_box_open;
 * the port split b150 so the haggle never set the flag the anchor reads.  This
 * lets the anchor OR it in so PAUSE_OPEN fires at the BARGAIN like retail
 * (RE §9.6/§11) — the prerequisite for replaying any haggle trace on the port.
 * TWO machines reach the same cs_input_poll (FUN_004622d9) Yes/No state:
 *   • the SCRIPTED tutorial machine (b51c!=0) at b608==4, and
 *   • the LIVE first-customer machine (b51c==0) at b534==0xf (the haggle
 *     decision; FUN_004658ab's poll).  Retail opens the SAME b150 choice box
 *     for both, so rounds 4-5 of the recording (the live practice sale with
 *     Tear) only fire PAUSE_OPEN once the live decision is included here. */
int32_t customer_service_bargain_active(void)
{
    return (s_b51c != 0 && s_b608  == 4)        /* scripted price-confirm   */
        || (s_b51c == 0 && s_b534  == 0xf);     /* live haggle decision     */
}
void    customer_service_set_script_file(int32_t idx) { s_price_fileidx = idx; }
int32_t customer_service_b5e4(void)        { return s_b5e4; }
/* The cc08==4 ESC-skip choice box's b150 hold (see s_skip_modal).  → PAUSE_OPEN
 * at the cancel-prompt arm, held through the leave/wrap-up/first-customer
 * arrival, → PAUSE_CLOSE at the live greeting b534==2 (retail's b150). */
int32_t customer_service_skip_modal_active(void) { return s_skip_modal; }

/* ── FUN_0045e6a5 — the cc08==4 ESC "Cancelling tutorial?" skip gate ──────────
 * Called from the in-game ESC dispatch (esc_dispatch.c) when cc08==4.  During
 * the SCRIPTED haggle tutorial (b51c==1) — and only when not already leaving
 * (b520==0) or armed (b5e4==0) — open the "Cancelling tutorial. Are you sure?"
 * choice box and latch b5e4=1; the master tick's b5e4 poll then drives Yes→leave
 * / No→resume.  This is a SEPARATE mechanism from the prologue/guild skip_event
 * (which is the b1c8-dialogue "Do you want to skip this event?" path); the live
 * customer machine (b51c==0) is NOT skippable, matching retail.  Returns 1 if it
 * armed the prompt (ESC consumed), 0 to fall through to the pause menu. */
int customer_service_esc_skip_arm(void)
{
    if (s_b520 == 0 && s_b51c == 1 && s_b5e4 == 0) {
        s_b5e4 = 1;
        s_skip_modal = 1;   /* DAT_0438b150 = 1 (choice_box_open); held past Yes */
        choice_box_open("Cancelling tutorial. Are you sure?", /*mode=*/1, /*sel=*/0);
        return 1;
    }
    return 0;
}

/* Once-per-frame render-state snapshot (FUN_0046602e + FUN_00466b7b inputs). */
void customer_service_get_render_state(struct cs_render_state *o)
{
    if (!o) return;
    o->b1cc = s_b1cc;          o->cs_active = s_cs_active;
    o->b52c = s_b52c;          o->b530 = s_b530;       o->b53c = s_b53c;
    o->b540 = s_b540;          o->b548 = s_b548;       o->b55c = s_b55c;
    o->b558 = s_b558;
    o->b54c = s_b54c;          o->b550 = s_b550;       o->b56c = g_scene_buy_current_page;
    o->b560 = s_b560;          o->b564 = s_b564;
    o->b58c = s_b58c;          o->b590 = s_b590;       o->b598 = s_b598;
    o->b59c = s_b59c;
    o->b5a0 = s_b5a0;          o->b5a4 = s_b5a4;       o->b5a8 = s_b5a8;
    o->b5b4 = s_b5b4;          o->b5bc = s_b5bc;       o->b5c0 = s_b5c0;
    o->b5c8 = s_b5c8;
    o->b5d0 = s_b5d0;          o->b5d4 = s_b5d4;       o->b5d8 = s_b5d8;
    o->b5dc = s_b5dc;          o->b51c = s_b51c;
    {   /* slot-1 name plate: *(int *)(&DAT_06a5ea90 + b56c*0x2c670) = the active
         * kyaku record's name_index (the first field of the engine record). */
        int ni = g_scene_buy_current_page;            /* b56c */
        o->cust_name_index = (ni >= 0 && ni < KYAKU_COUNT)
                           ? g_kyaku.records[ni].name_index : 0;
    }
    o->cust_active[0] = s_cust_active[0];
    o->cust_active[1] = s_cust_active[1];
    o->pose_timer[0]  = s_item_pick[1];  /* DAT_0730b278 */
    o->pose_timer[1]  = s_item_pick[2];  /* DAT_0730b27c */
    for (int i = 0; i < CS_ITEMPICK_N * 3; i++) o->item_pick[i] = s_item_pick[i];
    o->price_ask = s_price_ask;          o->price_base = s_price_base;
    o->price_count = s_price_bc4;        o->price_fileidx = s_price_fileidx;
    o->price_bc8 = s_price_bc8;          o->price_cursor = s_price_cursor;
    o->line = s_b270;
}

int32_t customer_service_queue_kyaku(int entry)
{
    if (entry < 0 || entry >= CS_QUEUE_ENTRIES) return -1;
    return s_queue[entry * CS_QUEUE_STRIDE + 0];
}
int32_t customer_service_queue_item_slot(int entry)
{
    if (entry < 0 || entry >= CS_QUEUE_ENTRIES) return -1;
    return s_queue[entry * CS_QUEUE_STRIDE + 1];
}
int32_t customer_service_queue_kind(int entry)
{
    if (entry < 0 || entry >= CS_QUEUE_ENTRIES) return -1;
    return s_queue[entry * CS_QUEUE_STRIDE + 2];
}
int32_t customer_service_queue_count(void)  { return s_queue_count; }
int32_t customer_service_eligible(int i)
{
    if (i < 0 || i >= CS_ELIGIBLE_N) return -1;
    return s_eligible[i];
}

/* ── FUN_00461303 — the customer-service kind selector ───────────────────────
 * Binds the active customer (b56c/b570 from the queue) + the offered-item handle
 * + the b5a8==2 dispatch, branching on the sale-flag bytes:
 *   f404 != 0 (scripted player sell, all.c:59312-59317) → b5a4 = 0xc0 (the
 *     script's offered item, id 3 = Steel Sword, base 3000).
 *   f406 != 0 (FORCED first customer, all.c:59320-59348) → b5a4 = 0x3ea00 (id
 *     4008 = Walnut Bread, base 100): scan the 20-slot showcase row
 *     (SAVE_BANK_FIELD_DISPLAY_GRID row 0) for the 0x3ea00 handle (low-6 masked)
 *     and set b564 = 1 iff its slot is one of the 7 special slots
 *     {1,2,3,4,11,12,13} (DAT_005c6be0).  b564 gates a 2-rng particle emit in the
 *     master tick (all.c:60240, FUN_00471089×2), so it is RNG-load-bearing.
 *   else (general live customer, all.c:59350+, rng-DRAWN item) →
 *     PORT-DEBT(cs-kind-select-general); not reached by the current f404/f406
 *     traces — fall back to the scripted 0xc0 so an item still resolves. */
static int cs_kind_select(void)
{
    const uint8_t *bank =
        (const uint8_t *)save_work_dwords_at(save_work_active_slot());
    int e = s_roster_perm[s_b318];

    g_scene_buy_current_page = s_queue[e * CS_QUEUE_STRIDE + 0]; /* b56c = queue[*].kyaku */
    s_b570                   = s_queue[e * CS_QUEUE_STRIDE + 1]; /* b570 = queue[*].item_slot */
    s_b5a8                   = 2;

    int f404 = (bank != NULL) && bank[CS_F404_SELL_ACTIVE_BYTE_OFF] != 0;
    int f406 = (bank != NULL) && bank[CS_F406_TUTORIAL_BYTE_OFF]    != 0;

    if (f404) {                                                 /* all.c:59312-59317 */
        s_b5a4 = 0xc0;
        return 1;
    }
    if (f406) {                                                 /* all.c:59320-59348 */
        s_b5a4 = 0x3ea00;                                       /* Walnut Bread, id 4008 */
        s_b564 = 0;
        const int32_t *grid = (const int32_t *)bank + SAVE_BANK_FIELD_DISPLAY_GRID;
        int found = -1;
        for (int i = 0; i < 0x14; i++) {                        /* the showcase row (20 cells) */
            uint32_t h = (uint32_t)grid[i];
            if (h != 0xffffffffu && (h & 0xffffffc0u) == 0x3ea00u) { found = i; break; }
        }
        if (found >= 0) {
            static const int special[7] = { 1, 2, 3, 4, 11, 12, 13 }; /* DAT_005c6be0 */
            for (int k = 0; k < 7; k++)
                if (found == special[k]) { s_b564 = 1; break; }
        }
        return 1;
    }
    s_b5a4 = 0xc0;   /* PORT-DEBT(cs-kind-select-general) fallback */
    return 1;
}

/* ── greeting base/ask (FUN_00462403 @ 0x46343d-0x463503) ────────────────────
 * Run the frame the idle promotes b534 0→1.  base = item.price·count;
 * ask = ftol((float)item.price) when b5a8==2 (the sell path), else
 * ftol((float)item.price·(float)count).  item = the offered handle (b5a4>>6),
 * resolved via FUN_004681f6 → g_item.records[slot].price (= DAT_095d37d4[slot*0xb3]). */
static void cs_greeting_base_ask(void)
{
    int slot = tables_item_find_slot_by_id(&g_item, s_b5a4 >> 6);
    int32_t item_base = (slot >= 0) ? g_item.records[slot].price : 0;
    s_price_runsum = item_base;                          /* bbc */
    s_price_base   = item_base * s_price_bc4;             /* bc0 = bbc·count */
    if (s_b5a8 == 2)
        s_price_ask = (int32_t)(float)item_base;          /* ftol((float)base) */
    else
        s_price_ask = (int32_t)((float)item_base * (float)s_price_bc4);
    s_b584 = 0;
}

/* ── queue advance (FUN_00462403 b524==0x3c, the f404 path) ──────────────────
 * The sell-active dispatch (all.c:60886-61000): bump the per-customer counter and
 * run the kind selector (b5b0∉{1,2} ⇒ FUN_00461303 → b5a8=2).  b520 leave only
 * arms once the queue is exhausted (inert in the captured window: b528≤1<count+1).
 * PORT-DEBT(cs-queue-advance-fileidx): b5b0==1 → FUN_00461792, ==2 → FUN_00460fa7. */
static void cs_queue_advance(void)
{
    s_price_bb4 = -1;                       /* DAT_005c6bb4 = 0xffffffff */
    s_b528 += 1;
    if (s_queue_count + 1 <= s_b528)        /* f406==0 on this path */
        s_b520 = 1;
    if (s_b318 < s_queue_count) {
        s_price_bc4 = 1;                    /* bc4 = count = 1 */
        s_b564 = 0;
        if (s_price_fileidx != 2 && s_price_fileidx != 1)
            cs_kind_select();               /* FUN_00461303 → b5a8=2, b56c=1, b5a4=0xc0 */
    }
    s_price_bc8 = -1;                       /* DAT_005c6bc8 = 0xffffffff */
    s_b54c = 0;
    s_b550 = 0;
    /* FUN_00452d3e(1) (all.c:60998-61000) — the SECOND d3e load (occ3): a queued
     * customer (b56c>0) with no leave in progress (b520==0) ⇒ spawn the
     * queued-customer asset-load worker.  param=1 (disasm 0x463435 `push 0x1`,
     * NOT session_init's `push 0x0`) picks the b13 thread proc (FUN_0047333b, the
     * per-page buy-phase loader), distinct from session_init's param-0 (ae8) load.
     * Sets DAT_0438b1cc=2 → the master tick goes inert until the worker clears it
     * (the cc08==4 arm bridges g_worker_sec_state_1cc → notify_loaded → b1cc=1).
     * Retail spawns this ~b524==0x3c; the port had only the session_init spawn, so
     * its haggle-window frame count / RNG was ~1-2f shifted vs retail (offer tilt
     * 1536 vs retail-free-run 1548).  Porting it matches the load structure.  RE §8.3. */
    if (s_b520 == 0 && g_scene_buy_current_page > 0) {
        s_b1cc = 2;                          /* DAT_0438b1cc = 2 (master-tick inert gate) */
        s_csload_hold = 0;                   /* {csloadpin} bracket start (occ3) */
        worker_load_spawn_d3e(1);            /* FUN_00452d3e(1) */
    }
}

/* ── idle (FUN_00462403 b534==0, all.c:60670-61027) ─────────────────────────
 * The pre-greeting idle: a frame counter (b524) gates the walk-setup (0x14),
 * story-event probe (0x32, inert — eligible list all -1), queue advance (0x3c),
 * and finally the greeting trigger (b524>0x77 AND b52c>=0x20 → b534=1 + base/ask). */
static void cs_idle_tick(void)
{
    s_b524 += 1;
    /* b524==10: FUN_004733cc() — PORT-DEBT(cs-idle-asset-refresh). */
    if (s_b524 == 0x14) {
        /* (b528&1)==0 || f404 → skip the walk setup (the sell path has f404 set)
         * AND skip the rest of the idle this frame (engine `goto LAB_0046350e`).
         * PORT-DEBT(cs-walk-setup): FUN_00461068 (autonomous customer walk). */
        if (s_b530 > 0)
            s_b530 -= 1;
        return;
    }
    /* b524==0x32 story-event probe — inert (eligible list all -1 on the sell
     * path); PORT-DEBT(cs-story-probe): the FUN_0044ba2c story triggers. */
    if (s_b524 == 0x3c)
        cs_queue_advance();
    if (0x77 < s_b524) {
        s_b52c += 1;
        s_b530 += 1;
        if (s_b52c < 0x20)
            return;
        s_b544 = 0;
        s_b534 = 1;
        cs_greeting_base_ask();
        return;
    }
    if (s_b530 > 0)                          /* LAB_0046350e */
        s_b530 -= 1;
}

/* ── FUN_004623bc — GOTO: scan the script for id==target, set the PC (b604) ────
 * Walks the file's records from 0; on the first id==target sets b604 to that
 * index; a sentinel (opcode==-1) before a match leaves b604 unchanged. */
static void cs_goto(int target_id)
{
    struct tuto_record *rec = &g_tuto[s_price_fileidx * TUTO_CONSUMER_STRIDE];
    int idx = s_b604;                        /* iVar2 = current PC (default) */
    if (rec[0].opcode != -1) {
        int i = 0;
        for (;;) {
            idx = i;                         /* iVar2 = iVar5 (set before the test) */
            if (rec[i].id == target_id)
                break;
            i++;
            if (rec[i].opcode == -1)         /* sentinel before match → unchanged */
                return;
        }
    }
    s_b604 = idx;
}

/* ── FUN_0045ff11 — count the asking price's decimal digits → b560 ──────────── */
static void cs_digit_count(void)
{
    int n = 0, p = 10;
    do {
        if (s_price_ask < p) { s_b560 = n; return; }
        p *= 10;
        n += 1;
    } while (n != 7);
}

/* ── FUN_0045ff31 — edit the asking-price digit at the cursor (L/R + U/D) ─────
 * Held bits (DAT_073dddd6): 2=R, 1=L move the cursor (mod 7); 4=U, 8=D ±1 the
 * digit.  Result clamped ≥1.  PORT-DEBT(cs-ask-gold-cap): the b5a8==0 buy path's
 * gold ceiling (all.c:58323) never applies on the sell path (b5a8==2). */
static void cs_digit_edit(void)
{
    if (s_in_held & 2) s_b560 = (s_b560 + 8) % 7;   /* R (SE 0x146) */
    if (s_in_held & 1) s_b560 = (s_b560 + 6) % 7;   /* L */
    int mul = 1, val = s_price_ask;
    for (int i = s_b560; i != 0; i -= 1) { val /= 10; mul *= 10; }
    val %= 10;
    s_price_ask -= mul * val;
    if (s_in_held & 4) val += 1;                    /* U */
    if (s_in_held & 8) val -= 1;                    /* D */
    s_price_ask += mul * val;
    if (s_price_ask < 1)
        s_price_ask = 1;
}

/* ── FUN_004622d9 — the price-confirm input poll (Yes/No + patience) ──────────
 * Returns 1 = commit (patience spent), 2 = cancel, 0 = continue.  PORT-DEBT
 * (cs-poll-fx): the cursor placement (FUN_00435710) + SE (FUN_00499519). */
static int cs_input_poll(void)
{
    int ret = 0;
    if (s_b58c < 5)
        s_b58c += 1;
    if (s_b590 < 1) {
        if (s_in_pressed & 0x10) {              /* Z */
            if (s_b540 == 0) s_b590 = 1;        /* Yes → start the commit countdown */
            else ret = 2;                       /* No → cancel */
        } else if (s_in_pressed & 0x20) {       /* X → cancel */
            ret = 2;
        } else if (s_in_pressed & 0xc) {        /* up/down → toggle Yes/No */
            s_b540 ^= 1;
        } else {
            return 0;
        }
    } else {
        s_b590 += 1;
        if (0xe < s_b590) { s_b590 = 0xf; ret = 1; }
    }
    return ret;
}

/* ── FUN_00460161 binding — the customer raises its offer (haggle UP) ─────────
 * Binds the live DAT_0730bXXX / price scalars + the active customer's kyaku
 * tuning (record b56c = g_kyaku.records[b56c]) to the pure haggle_offer_up math
 * (src/customer_haggle.c).  trend = FUN_004361b2(b5a4) is PORT-DEBT → neutral 0
 * (no price tilt, no rng draw); is_tutorial = the f406 override (clears here). */
static void cs_offer_up(void)
{
    int idx = g_scene_buy_current_page;                  /* b56c */
    if (idx < 0 || idx >= KYAKU_COUNT) idx = 0;           /* defensive */
    const kyaku_record_t *kr = &g_kyaku.records[idx];
    haggle_customer_t c = {
        .initial     = kr->initial,
        .random      = kr->random,
        .gullibility = kr->gullibility,
        .rise1       = kr->rise1,
        .rise2       = kr->rise2,
        .budget_low  = kr->budget_low,
        .budget_high = kr->budget_high,
    };
    haggle_state_t st = {
        .round = s_b584, .offer = s_b574, .work_price = s_b57c,
        .floor = s_b580, .accept_ref = s_b588,
    };
    const uint8_t *bank = (const uint8_t *)save_work_dwords_at(save_work_active_slot());
    int is_tut = (bank != NULL) && bank[CS_F406_TUTORIAL_BYTE_OFF] != 0;
    haggle_offer_up(&st, &c, s_price_base, s_price_ask, 0, is_tut);
    s_b584 = st.round; s_b574 = st.offer; s_b57c = st.work_price;
    s_b580 = st.floor; s_b588 = st.accept_ref;
}

/* the dialogue-line loader (FUN_0046098f), defined below the master tick but
 * called from the scripted machine's dialogue op. */
static void cs_dialogue_line_setup(const char *text, int idx, int32_t chr_arg);

/* ── FUN_00461c00 — the SCRIPTED-sell machine (the customer-service tutorial) ──
 * The per-frame interpreter dispatched from the master tick's b534==1 arm while
 * b51c==1.  The PC (b604) walks g_tuto[b5b0*200 + pc]; opcodes interleave Tear's
 * dialogue (CHR0/CHR1) with the price UI: price-set (op 2 → base/ask from the
 * offered item), PRID/PRIA (op 3/4 → the digit editor + the customer's offer via
 * FUN_00460161), and conditional GOTOs (op 5/6/0xc/0xd/0xe → cs_goto on ask/base
 * ratio thresholds).  Transcribed from docs/decompiled/by-address/461c00.c with
 * its literal label structure (the b600==0 dispatch falls through into the
 * b600!=0 continuation handlers via the LAB_* gotos).  Externals that are render/
 * audio/item-menu are PORT-DEBT no-ops (inert for the state trajectory). */
static void cs_scripted_tick(void)
{
    int recbase = s_price_fileidx * TUTO_CONSUMER_STRIDE;
    struct tuto_record *r = &g_tuto[recbase + s_b604];
    int op;
    int uVar10 = 0;            /* GOTO target id */

    if (r->opcode == -1) {     /* end of script → closing */
        s_cust_active[0] = 0;
        s_cust_active[1] = 0;
        s_b534 = 0xc;
        s_b55c = 0;
        return;
    }
    /* PORT-DEBT(cs-details-overlay): FUN_004681e6 detail-card open query (0 in
     * steady state) + the Button-3 item-detail draw (all.c:59767-59783, render). */

    if (s_b600 == 0) {
        op = r->opcode;
        s_b600 = 1;
        if (op != 10) {
            if (op == 0xb) {                       /* sword-select result */
                /* PORT-DEBT(cs-sword-select): FUN_00469a9f + the like-attr GOTO. */
                cs_goto(r->args[1]);
                s_b600 = 0;
                s_b608 = -1;
                goto lab_tail;
            }
            if (op == 9) {                         /* TOUT — NPC exits */
                s_b608 = 5;
                goto lab_tout_dec;
            }
            if (op == 8) {                         /* TAGN — hide target window */
                s_b5a0 = 0;
                goto lab_advance;
            }
            if (op == 2) {                         /* price-set: base/ask from item 2 */
                s_b5a0 = 1;
                s_b5a4 = 0x80;
                {
                    int slot = tables_item_find_slot_by_id(&g_item, 2);
                    int32_t pr = (slot >= 0) ? g_item.records[slot].price : 0;
                    s_price_runsum = pr;            /* bbc */
                    s_price_base   = pr;            /* bc0 = bbc */
                    int slot2 = tables_item_find_slot_by_id(&g_item, s_b5a4 >> 6);
                    int32_t pr2 = (slot2 >= 0) ? g_item.records[slot2].price : 0;
                    s_price_ask = (int32_t)(float)pr2;   /* ftol((float)price) */
                }
                s_price_bb4 = -1;
                s_b584 = 0;
                goto lab_advance;
            }
            if (op == 6) { uVar10 = r->args[0]; goto lab_goto; }   /* GOTO */
            if (op == 0x14) {                      /* SET_INITIAL */
                s_b604 += 1;
                s_b608 = -1;
                s_b600 = 0;
                s_price_bb4 = s_price_ask;
                goto lab_tail;
            }
            if (op == 0xc) {                       /* PRICE compare */
                if (s_price_ask <= s_price_runsum) { uVar10 = r->args[1]; goto lab_goto; }
                uVar10 = r->args[0];
                goto lab_goto;
            }
            if (op == 5) {                         /* BUN0 / value compare */
                float fa = (float)s_price_ask, fb = (float)s_price_runsum;
                if (s_price_fileidx == 1) {
                    if (fa <= fb * 0.2f) { uVar10 = r->args[0]; goto lab_goto; }
                    if (fa < fb * 0.7f)  { uVar10 = r->args[1]; goto lab_goto; }
                    if (fa < fb * 0.9f)  { uVar10 = r->args[2]; goto lab_goto; }
                    if (fb <= fa) {
                        if (s_price_ask == s_price_runsum) { uVar10 = r->args[4]; goto lab_goto; }
                        if ((double)s_price_runsum * 1.5 <= (double)s_price_ask)
                            { uVar10 = r->args[6]; goto lab_goto; }
                        uVar10 = r->args[5];
                        goto lab_goto;
                    }
                } else {
                    if (fa < fb * 0.5f) { uVar10 = r->args[0]; goto lab_goto; }
                    if (fa < fb * 0.7f) { uVar10 = r->args[1]; goto lab_goto; }
                    if (fa < fb)        { uVar10 = r->args[2]; goto lab_goto; }
                    if (s_price_ask != s_price_runsum) {
                        if ((double)s_price_runsum * 1.3 <= (double)s_price_ask) {
                            if ((double)s_price_runsum * 2.0 <= (double)s_price_ask)
                                { uVar10 = r->args[6]; goto lab_goto; }
                            uVar10 = r->args[5];
                            goto lab_goto;
                        }
                        uVar10 = r->args[4];
                        goto lab_goto;
                    }
                }
                uVar10 = r->args[3];
                goto lab_goto;
            }
            if (op == 0xd) {                       /* DISCOUNT compare */
                if (s_price_bb4 < s_price_ask) { uVar10 = r->args[6]; goto lab_goto; }
                if (s_price_ask != s_price_bb4) {
                    float fa = (float)s_price_ask, fb = (float)s_price_runsum;
                    if (fb * 0.7f <= fa) {
                        if (fb <= fa) {
                            if (fa < fb * 1.3f) { uVar10 = r->args[2]; goto lab_goto; }
                            uVar10 = r->args[3];
                            goto lab_goto;
                        }
                        uVar10 = r->args[1];
                        goto lab_goto;
                    }
                    uVar10 = r->args[0];
                    goto lab_goto;
                }
                if ((float)s_price_ask < (float)s_price_runsum * 1.3f)
                    { uVar10 = r->args[4]; goto lab_goto; }
                uVar10 = r->args[5];
                goto lab_goto;
            }
            if (op == 0xe) {                       /* MARKUP compare */
                if (s_price_ask < s_price_bb4) { uVar10 = r->args[0]; goto lab_goto; }
                if (s_price_ask == s_price_bb4) {
                    if ((float)s_price_runsum * 0.5f <= (float)s_price_ask)
                        { uVar10 = r->args[2]; goto lab_goto; }
                    uVar10 = r->args[1];
                    goto lab_goto;
                }
                if ((float)s_price_ask < (float)s_price_runsum * 0.9f)
                    { uVar10 = r->args[3]; goto lab_goto; }
                uVar10 = r->args[4];
                goto lab_goto;
            }
            if (op == 3) {                         /* PRID — price-window show */
                cs_digit_count();
                if (s_b59c == 0) s_b59c = 1;
                s_cust_active[1] = 0;
                s_cust_active[0] = 0;
                s_b600 = 0;
                s_b608 = 1;
                goto lab_prid_wait;
            }
            if (op != 4) {                         /* dialogue (CHR0=0 / CHR1=1) */
                s_b55c = 0;
                /* FUN_0046098f: load the line text, split at <C>, reset b548.  The
                 * b55c-gated advance below is driven by cs_dialogue_reveal_tick. */
                cs_dialogue_line_setup(r->text, op, r->chr_arg);
                if (op == 0 || op == 1) {          /* speaker active flag toggle */
                    s_cust_active[op ^ 1] = 0;
                    s_cust_active[op] = 1;
                }
                s_b608 = 0;
                goto lab_b608_dispatch;
            }
            /* op == 4: PRIA — price-input wait */
            if (s_b59c == 0) { cs_digit_count(); s_b59c = 1; }
            s_b608 = 3;
            s_cust_active[0] = 0;
            goto lab_b608_3;
        }
        /* op == 10: ITEM menu (PORT-DEBT) */
        s_b608 = 6;
        s_price_fileidx = 2;
        s_cust_active[1] = 0;
        s_cust_active[0] = 0;
        goto lab_item_menu;
    }

    s_b600 += 1;
lab_b608_dispatch:
    if (s_b608 == 3) {
lab_b608_3:
        s_cust_active[1] = 0;
        /* PORT-DEBT(cs-cursor): FUN_00435612 cursor reset (render). */
        if (s_b59c == 0) s_b59c = 1;
        if ((s_in_pressed & 0x10) == 0) {
            cs_digit_edit();
        } else {
            s_b608 = 4;
            s_b540 = 0;
            cs_offer_up();                         /* FUN_00460161 — the customer offer */
            s_b590 = 0;
            s_b540 = 0;
            /* PORT-DEBT(cs-offer-fx): SE 0x143 + FUN_00435693 cursor placement. */
        }
    } else {
        if (s_b608 != 4) {
            if (s_b608 == 5) {
                if (s_b600 < 0x20) goto lab_tout_dec;
                s_b52c += 1;
                if (s_b52c < 0x20) goto lab_tail;
            } else {
                if (s_b608 == 6) goto lab_item_menu;
                if (s_b608 != 1) {
                    if (s_b608 != 0) goto lab_check_ret;
                    /* b608 == 0: dialogue — advance on Z-edge or X-held once revealed. */
                    if (s_b55c == 0 ||
                        ((s_in_pressed & 0x10) == 0 && (s_in_cur & 0x20) == 0))
                        goto lab_tail;
                    s_b55c = 0;
                    goto lab_advance_pc;
                }
lab_prid_wait:                                     /* b608 == 1 (PRID) */
                if (s_in_pressed == 0) goto lab_tail;
                s_b55c = 0;
            }
            s_b604 += 1;                            /* b608 ∈ {5,1} fallthrough advance */
            s_b600 = 0;
            goto lab_tail;
        }
        /* b608 == 4: PRIA price-confirm poll (FUN_004622d9). */
        {
            int poll = cs_input_poll();
            if (poll == 1) {
                if (s_price_bb4 == -1)
                    s_price_bb4 = s_price_ask;
                /* PORT-DEBT(cs-cursor): FUN_00435612. */
                s_b59c = 0;                          /* LAB_00462253 */
                goto lab_advance_pc;                 /* → LAB_004622b7 */
            }
            if (poll == 2)
                s_b608 = 3;
        }
    }
lab_check_ret:                                       /* LAB_004622bd */
    if (s_b608 == 4)
        return;
    goto lab_tail;

lab_advance_pc:                                      /* LAB_004622b7: b600=0; b604++ */
    s_b600 = 0;
    s_b604 += 1;
    goto lab_check_ret;

lab_advance:                                        /* LAB_00461dda: op 2/8 → cont */
    s_b604 += 1;
    s_b600 = 0;
    goto lab_b608_dispatch;

lab_goto:                                           /* LAB_00462065 */
    cs_goto(uVar10);
    s_b608 = -1;
    s_b600 = 0;
    goto lab_tail;

lab_tout_dec:                                       /* LAB_00462200 */
    s_b52c -= 1;
    goto lab_tail;

lab_item_menu:                                      /* LAB_00462225 (PORT-DEBT) */
    /* PORT-DEBT(cs-item-menu): FUN_00469414 item-window pick + FUN_004682d0; the
     * buy-from-customer item-select flow, not the sell tutorial's price path. */
    goto lab_tail;

lab_tail:                                           /* LAB_004622c6 */
    if (s_b58c > 0)
        s_b58c -= 1;
    return;
}

/* ── FUN_0046098f — load a dialogue line into the active buffer ───────────────
 * The dialogue op (CHR0/CHR1) calls this with the line text (g_tuto[pc].text),
 * the speaker index (0/1 → the b54c/b550 panel-sprite slot), and the per-line
 * voice/sprite id (chr_arg).  Copies the text into s_line_buf, SPLITS at a "<C>"
 * tag (sets b558=1 = a mid-line pause and stashes the post-<C> tail in
 * s_line_tail), and zeroes the reveal budget b548.  b270 points at the visible
 * text (the source when there is no <C>, else the truncated s_line_buf). */
/* the <C> split shared by FUN_0046098f (cs_dialogue_line_setup, the scripted op)
 * and FUN_00460a1a (cs_pick_line, the live picker) — both end identically (the
 * picker tail @0x460a77): copy `text` into s_line_buf; on a "<C>" tag truncate
 * the visible half there, set b558=1, and stash the post-<C> remainder in
 * s_line_tail.  b270 points at the source when there is no <C>, else at the
 * truncated s_line_buf.  Zeroes the reveal budget b548. */
static void cs_split_line(const char *text)
{
    s_b558 = 0;
    s_b548 = 0;
    s_b270 = text;
    int i = 0;
    for (;;) {
        char c = text[i];
        if (c == '\0')                       /* no <C> → b270 stays = source */
            return;
        s_line_buf[i] = c;
        if (c == '<' && text[i + 1] == 'C')
            break;
        i++;
        if (i == 0x100)
            return;
    }
    s_line_buf[i] = '\0';                     /* truncate the visible text at '<' */
    s_b558 = 1;
    if (i != 0x100) {                         /* copy the tail after "<C>" */
        int j = 0;
        for (;;) {
            char c = text[i + 3];             /* skip the 3 chars "<C>" */
            s_line_tail[j] = c;
            if (c == '\0')
                break;
            j++;
            i++;
            if (i == 0x100)
                break;
        }
    }
    s_b270 = s_line_buf;
}

static void cs_dialogue_line_setup(const char *text, int idx, int32_t chr_arg)
{
    if (idx == 0) s_b54c = chr_arg;          /* (&DAT_0730b54c)[idx] = chr_arg */
    else          s_b550 = chr_arg;
    cs_split_line(text);
}

/* ── FUN_00465db4 reveal-complete (the b55c half) — all.c:62828-62836 ─────────
 * The engine sets DAT_0730b55c=1 as a SIDE EFFECT of the speech-bubble text
 * RENDER (FUN_00466b7b @63744 calls FUN_00465db4 with budget = DAT_0730b548):
 * it walks the active line consuming b548 display-chars (an SJIS 2-byte char =
 * a lead byte that costs no budget + a trail that costs 1; "<BR>" = a free row
 * break) and, when the line's '\0' is reached within the budget AND b544>0,
 * sets b55c=1 — the gate the scripted machine's dialogue advance reads.  We run
 * the COUNT here (no draw) at the master-tick HEAD, using b548 as it stood at
 * the end of the prior frame (== the value retail's prior-frame render saw), so
 * the b534==1 dispatch later this frame reads the same b55c retail does.
 * Only SETS b55c (never clears — the dispatch clears it when it loads a line).
 * PORT-DEBT(cs-reveal-in-render): fold this into the Chip 3 FUN_0046602e/66b7b
 * render port so it is the same call site/glyph-walk, not a state-side replica. */
static void cs_dialogue_reveal_tick(void)
{
    if (s_cust_active[0] == 0 && s_cust_active[1] == 0)   /* no active speaker */
        return;
    if (s_b270 == NULL)
        return;
    int budget = s_b548;
    const char *p = s_b270;
    int in_lead = 0;
    for (int guard = 0; guard < 0x200; guard++) {
        char c = *p;
        if (c == '\0') {                     /* whole line revealed */
            if (s_b544 > 0)
                s_b55c = 1;
            return;
        }
        if (c == '<' && p[1] == 'B' && p[2] == 'R') {   /* "<BR>" row break — free */
            p += 4;
            continue;
        }
        if (c < 0 && in_lead == 0) {
            in_lead = 1;                     /* SJIS lead byte — costs no budget */
        } else {
            in_lead = 0;
            budget -= 1;
            if (budget < 1)                  /* budget exhausted before the end */
                return;
        }
        p += 1;
    }
}

/* ── FUN_00460a1a — pick + load a customer dialogue line ─────────────────────
 * The live machine's line loader.  `rec_index` selects the SPEAKING record's
 * dialogue buffer: 0 = Recette (&DAT_06a5ea90, the slot-0 shopkeeper lines),
 * g_scene_buy_current_page (=b56c) = the customer (slot-1).  `type` is the msgNN
 * line type; `slot` (0/1) is the b54c/b550 sprite slot.  For a REAL customer
 * (f404==0) it draws ONE rng (rand % count[type]) to pick a variant — a
 * load-bearing LCG step; for the forced/tutorial sale it uses variant 0.  Reads
 * the text/sprite from the per-kyaku dialogue buffer (customer_dialogue.c,
 * engine record +0x6e70/0x51d8/0x6df8) and runs the <C> split.
 * PORT-DEBT(cs-dlg-override): the DAT_073dddb8 scripted-override variant table
 * (data/buysell.txt) is not modeled — we always take the rng branch, matching
 * the verified-1:1 consumption in this flow (the override is inactive here).
 * PORT-DEBT(cs-voice): the voice id (+0x5b38) is parsed but playback (FUN_0049933c
 * on record+voice*0x100+0x1444) is audio, not wired. */
static void cs_pick_line(int rec_index, int type, int slot)
{
    const kyaku_dialogue_t *dlg = kyaku_dialogue_get(rec_index);
    const uint8_t *bank =
        (const uint8_t *)save_work_dwords_at(save_work_active_slot());
    int f404 = (bank != NULL) && bank[CS_F404_SELL_ACTIVE_BYTE_OFF] != 0;

    int variant;
    if (!f404) {
        uint32_t r = (uint32_t)rng_next15();   /* thunk_FUN_005041f6 */
        int count = (dlg != NULL && (unsigned)type < (unsigned)KYAKU_DLG_TYPES)
                  ? dlg->count[type] : 0;
        /* engine: rand % count[type] (would div-0 on an absent type — vendor
         * data always has the requested type; we clamp to variant 0 for safety
         * while STILL consuming the rng so the LCG stays 1:1 with retail). */
        variant = (count > 0) ? (int)(r % (uint32_t)count) : 0;
    } else {
        variant = 0;                           /* forced sale → variant 0 */
    }

    int s = variant + type * KYAKU_DLG_VARIANTS;
    int32_t sprite = 0;
    const char *text = "...";                  /* fallback if the script is absent */
    if (dlg != NULL && (unsigned)s < (unsigned)KYAKU_DLG_SLOTS) {
        sprite = dlg->sprite[s];
        text   = dlg->text[s];                 /* voice = dlg->voice[s] (PORT-DEBT) */
    }
    if (slot == 0) s_b54c = sprite; else s_b550 = sprite;   /* (&b54c)[slot] */
    cs_split_line(text);                        /* sets b558/b548/b270 (+ <C>) */
}

/* ── FUN_004607f3 — set the <I> dialogue macro to the transacted item's name ──
 * `handle` = the offered item handle (b5a4): id = handle>>6, count = handle&0xf.
 * count 0 → just the singular name ("Steel Sword"); else "name N".  Read by
 * font_draw_text_box (dlg_macro_expand) when a line contains "<I>".
 * PORT-DEBT(cs-item-macro-kinds): the engine's b534==0x1e debug-string branch +
 * the b5a8==4 (synth/order) name source are not modeled — the f404 sell path
 * (b5a8==2) takes the item-name branch ported here. */
static void cs_set_item_macro(int handle)
{
    char tmp[DLG_MACRO_BUFSZ];
    int slot = tables_item_find_slot_by_id(&g_item, handle >> 6);   /* FUN_004681f6 */
    const char *name = (slot >= 0) ? g_item.records[slot].singular : "";
    int count = handle & 0xf;
    if (count == 0)
        snprintf(tmp, sizeof tmp, "%s", name);              /* DAT_005c6d7c "%s" */
    else
        snprintf(tmp, sizeof tmp, "%s %d", name, count);    /* DAT_005c6d74 "%s %d" */
    dlg_macro_set(DLG_MAC_I, tmp);
}

/* ── FUN_00460672 — grade the ask vs the customer's fair value (b588) ─────────
 * Returns 1 if the ask is within ±0.5% of b588 (great deal → +5 like), 2 within
 * −5%/+5% (ok → +2), else 0 (+1).  Only tunes the like-count; the accept/reject
 * is decided by offer-vs-ask in the machine.  Constants objdump-transcribed
 * (the Ghidra decompile dropped the x87 multiplies): 0x519e08=1.005f,
 * 0x519e00=0.995, 0x5198ac=1.05f, 0x519df8=0.95. */
static int cs_accept_eval(void)
{
    int iVar1 = (int)((float)s_b588 * 1.005f);    /* @0x519e08 */
    int iVar2 = (int)((double)s_b588 * 0.995);    /* @0x519e00 */
    int iVar3 = (int)((float)s_b588 * 1.05f);     /* @0x5198ac */
    int iVar4 = (int)((double)s_b588 * 0.95);     /* @0x519df8 */
    if (s_b588 < 0x6e)
        iVar2 = iVar1;
    int ask = s_price_ask;                         /* DAT_005c6bb8 */
    if (iVar1 < ask || ask < iVar2) {
        if (iVar3 < ask || ask < iVar4)
            return 0;
        return 2;
    }
    return 1;
}

/* ── FUN_00460f16 — pick the "too expensive" pushback line variant (2/3/4) ──── */
static int cs_pushback_line(void)
{
    int ret = 2;
    /* PORT-DEBT(cs-shop-stock): the per-item sold-streak high-short
     * (DAT_045109aa + b570*4) is not modeled → read 0 (→ variant 2). */
    int sold = 0;
    if (sold < 5) { if (sold > 0) ret = 3; }
    else          ret = 4;
    const uint8_t *bank =
        (const uint8_t *)save_work_dwords_at(save_work_active_slot());
    if (bank != NULL && bank[CS_F404_SELL_ACTIVE_BYTE_OFF] != 0)
        ret = 3;
    return ret;
}

/* ── FUN_00460e50 — the "you sell a lot of this" sold-streak flash trigger ──── */
static int cs_sold_streak(void)
{
    /* PORT-DEBT(cs-shop-stock): reads/writes the per-item sold-streak shorts
     * (DAT_06a5d564 / DAT_045109a8 high-short) — not modeled; no flash. */
    return 0;
}

/* ── FUN_004658ab — the LIVE kind-2 sell machine (the first real customer) ────
 * Dispatched from the master tick's b5a8==2 arm for b534 ∈ {2,6,0xf,7,8,9}.
 * State graph (RE §3.5): 2 greeting → 6 reaction/price-edit → 0xf decision →
 * 7 accept / 8 pushback / 9 reject → (master tick: 0xa thanks → 0xc close →
 * 0x14/0x15 queue-advance → idle).  Transcribed by-address from 4658ab.c.
 * PORT-DEBT(cs-live-sale-fx): the accept side-effects (gold/stock/catalog —
 * FUN_00460d52/b3a/606fc/00083/00f59/0002a/00b93, all gated f404==0), the
 * details overlay (FUN_004681e6/db/68286), the cursor/SE externals — none move
 * the b534 state, so they are inert no-ops for the un-softlock trajectory. */
static void cs_live_machine(void)
{
    s_b544 += 1;

    /* FUN_004681e6 detail-card query → 0 in steady state (PORT-DEBT). */

    if (s_b534 == 2) {                          /* greeting */
        if (s_b544 == 1) {
            cs_pick_line(g_scene_buy_current_page, 1, 1);  /* FUN_00460a1a(rec,1,1) */
            s_cust_active[1] = -1;              /* DAT_06a5ea74 = -1 */
            s_b5a0 = 1;                         /* arrival anim start */
            s_skip_modal = 0;                  /* close the held ESC-skip b150 HERE — the
                                                * first b534==2 frame, 1f after the b534=1→2
                                                * edge — matching retail's deferred b150 clear
                                                * so PAUSE_CLOSE lands on the b534==2 frame
                                                * (not 1f early); RE §21.11.2. */
            /* FUN_00435612 cursor (PORT-DEBT). */
        }
        if (s_b55c != 0 && (s_in_pressed & 0x10) != 0) {   /* line up + Z */
            s_b55c = 0;
            s_cust_active[1] = 0;
            s_b534 = 6;
            cs_offer_up();                      /* FUN_00460161 — the customer offer */
            s_b544 = 0;
            cs_digit_count();                   /* FUN_0045ff11 */
        }
        goto lab_tail;
    }

    if (s_b534 != 0xf) {
        if (s_b534 == 6) {                      /* reaction / price-edit */
            s_cust_active[1] = 0;
            if (s_b59c == 0) s_b59c = 1;
            if (s_b544 == 1)
                cs_pick_line(0, 9, 0);          /* FUN_00460a1a(&ea90,9,0) */
            /* FUN_00435612 cursor (PORT-DEBT). */
            if ((s_in_pressed & 0x10) == 0) {
                cs_digit_edit();                /* FUN_0045ff31 — adjust the ask */
            } else {
                s_cust_active[1] = 0;
                s_b534 = 0xf;                   /* → decision */
                s_b590 = 0;
                s_b540 = 0;
                /* SE 0x143 + FUN_00435693 cursor (PORT-DEBT). */
            }
            goto lab_tail;
        }
        if (s_b534 == 7) {                      /* accept → sale */
            if (s_b544 == 1) {
                cs_pick_line(g_scene_buy_current_page, 5, 1);  /* FUN_00460a1a(rec,5,1) */
                s_cust_active[1] = -1;
            }
            if (s_b55c == 0 || (s_in_pressed & 0x10) == 0)
                goto lab_tail;
            s_b55c = 0;
            s_cust_active[1] = 0;
            /* PORT-DEBT(cs-live-sale-fx): f404==0 → gold += ask (DAT_044e37a4),
             * SE 0x14d, FUN_00460d52/b3a/606fc/00083/00f59/0002a/00b93 (the
             * payout + catalog + inventory).  Gated f404==0; inert for the
             * forced/tutorial sale.  The STATE still advances to 10. */
            s_b534 = 10;
        } else {
            if (s_b534 == 8) {                  /* pushback ("too much") */
                if (s_b544 == 1) {
                    int line = cs_pushback_line();
                    s_b5c4 = 0;
                    const uint8_t *bank = (const uint8_t *)
                        save_work_dwords_at(save_work_active_slot());
                    int f404 = bank && bank[CS_F404_SELL_ACTIVE_BYTE_OFF];
                    int f406 = bank && bank[CS_F406_TUTORIAL_BYTE_OFF];
                    int uVar6;
                    if (!f404 || s_b538 != 1) {
                        if (s_b584 == line && !f406) uVar6 = 10;
                        else                         uVar6 = 4;
                    } else {
                        uVar6 = 0xe;
                    }
                    cs_pick_line(g_scene_buy_current_page, uVar6, 1);  /* FUN_00460a1a(rec,uVar6,1) */
                    s_cust_active[1] = -1;
                }
                if (s_b55c != 0 && (s_in_pressed & 0x10) != 0) {
                    s_b55c = 0;
                    s_cust_active[1] = 0;
                    int line = cs_pushback_line();
                    const uint8_t *bank = (const uint8_t *)
                        save_work_dwords_at(save_work_active_slot());
                    int f406 = bank && bank[CS_F406_TUTORIAL_BYTE_OFF];
                    if (s_b584 == line && !f406) {   /* patience spent → leave */
                        s_b534 = 0xb;
                        s_b544 = 0;
                        s_b5a0 = 0;
                        return;
                    }
                    s_b534 = 6;                 /* haggle again (customer re-offers) */
                    s_b544 = 0;
                    cs_offer_up();              /* FUN_00460161 */
                }
                goto lab_tail;
            }
            if (s_b534 != 9)
                goto lab_tail;
            /* b534 == 9: final reject */
            if (s_b544 == 1) {
                cs_pick_line(g_scene_buy_current_page, 6, 1);  /* FUN_00460a1a(rec,6,1) */
                s_cust_active[1] = -1;
            }
            if (s_b55c == 0 || (s_in_pressed & 0x10) == 0)
                goto lab_tail;
            s_b55c = 0;
            s_cust_active[1] = 0;
            s_b534 = 0xb;
        }
        s_b544 = 0;
        s_b5a0 = 0;
        goto lab_tail;
    }

    /* b534 == 0xf — the HAGGLE DECISION (FUN_004622d9 poll). */
    {
        int poll = cs_input_poll();            /* FUN_004622d9 */
        if (poll != 1) {
            if (poll == 2) s_b534 = 6;         /* cancel → back to reaction */
            goto lab_tail;
        }
        /* poll == 1 — commit the named price. */
        const uint8_t *bank =
            (const uint8_t *)save_work_dwords_at(save_work_active_slot());
        int f404 = bank && bank[CS_F404_SELL_ACTIVE_BYTE_OFF];
        int f406 = bank && bank[CS_F406_TUTORIAL_BYTE_OFF];
        s_b538 = 0;
        /* PORT-DEBT(cs-shop-stock): the b584==3 patience-spent stock penalty
         * (f404==0) + the reject stock-loss + the like-count tuning all touch the
         * unmodeled per-item shorts; gated f404==0 (inert for the tutorial). */
        if (s_b574 < s_price_ask) {            /* offer < ask → too expensive */
            if (s_price_ask < s_b580 || f406) {
                s_b534 = 8;                    /* pushback (haggle floor / tutorial) */
            } else {
                s_b534 = 9;                    /* reject (− stock, PORT-DEBT) */
            }
        } else {                               /* offer >= ask → can accept */
            int can = ((double)s_price_base * 0.8 < (double)s_price_ask) || !f404;
            if (can) {
                (void)cs_accept_eval();        /* FUN_00460672 — like-count (PORT-DEBT) */
                if (cs_sold_streak())          /* FUN_00460e50 → flash */
                    s_b53c = 1;
                s_b534 = 7;                    /* ACCEPT */
            } else {
                s_b538 = 1;
                s_b534 = 8;                    /* pushback */
            }
        }
        s_b59c = 0;
        s_b544 = 0;
        /* FUN_00435612 cursor (PORT-DEBT). */
    }

lab_tail:
    /* PORT-DEBT(cs-details-overlay): b5a0 && (pressed&0x40) → the item-detail
     * card (FUN_004681db/68286 + SE 0x2c6). */
    return;
}

/* ── customer_service_master_tick — FUN_00462403 ─────────────────────────────
 * Run every frame while cc08==4 (once the asset-load worker has cleared b1cc).
 * Owns the per-frame timers + the arrival-anim ramp, then the b534 state switch.
 * For the scripted tutorial sell (b51c==1) the b534==1 arm dispatches the scripted
 * machine (FUN_00461c00) every frame; b534 stays 1 (the kind-machine states are
 * never entered — RE doc §3.7).  `pressed`/`held` = the engine masks DAT_073dddd4
 * /DAT_073dddd6.  PORT-DEBT tags mark the off-window branches (leave/closing/fx). */
void customer_service_master_tick(uint32_t cur, uint32_t pressed, uint32_t held)
{
    s_in_cur     = cur;
    s_in_pressed = pressed;
    s_in_held    = held;

    if (s_b1cc == 2)                         /* asset-load worker running → inert */
        return;

    /* the ESC "Cancelling tutorial?" skip poll (all.c:60168-60186).  Armed by
     * customer_service_esc_skip_arm (FUN_0045e6a5); poll the choice box: Yes
     * (CB_OPT0) → start the leave (b520=1, force b5b4≥0xf0 so the dissolve fires
     * this cycle), No (CB_OPT1) → clear b5e4 and resume the tutorial.  `pressed`
     * (DAT_073dddd4) is the edge retail's FUN_00434ed2 reads internally. */
    if (s_b5e4 == 1) {
        int r = choice_box_poll((uint16_t)s_in_pressed, 1);   /* FUN_00434ed2 */
        if (r != CB_OPT0) {                  /* not Yes */
            if (r == CB_OPT1) {              /* No → cancel the skip, resume */
                s_b5e4 = 0;
                s_skip_modal = 0;            /* No closes the choice box (b150=0) */
            }
            return;
        }
        if (s_b5b4 > 0xef) {                 /* Yes, dissolve counter already past */
            s_b520 = 1;
            s_b5e4 = 0;
            return;
        }
        s_b520 = 1;                          /* Yes → leave, seed the dissolve clock */
        s_b5b4 = 0xf0;
        s_b5e4 = 0;
        return;
    }

    /* text-reveal-complete (b55c): in retail this is a side effect of the prior
     * frame's speech-bubble render; replicate it at the tick HEAD off the b548 the
     * prior frame left, so the b534==1 dispatch below reads retail's b55c. */
    cs_dialogue_reveal_tick();

    s_b5b4 += 1;
    if (s_b53c > 0) { s_b53c += 1; if (0x4f < s_b53c) s_b53c = 0; }
    /* FUN_0048a833() — PORT-DEBT(cs-misc-tick): per-frame housekeeping, no cc08 state. */
    if (s_b5d0 == 0) s_b5d4 = 0;
    else if (s_b5d4 < 0xf) s_b5d4 += 1;

    /* on-screen-customer pose timers + the dialogue REVEAL budget (all.c:60198-
     * 60234).  b278/b27c (= s_item_pick[1]/[2]) ramp each speaker's pose in
     * (0→0xf) while active / out (→0) when not; the reveal budget b548 climbs
     * 1/frame per ACTIVE speaker — but the whole block is gated on `settled`
     * (bVar12): while an INACTIVE speaker is still posing OUT (b278>0) the budget
     * is held, which is the inter-line gap retail shows between dialogue lines.
     * PORT-DEBT(cs-target-window-spawn): the (settled && b5b0!=0) →
     * FUN_00469351/FUN_00468338 target-window asset spawn (render-side; b5b0==0
     * on the tutorial sell path, so inert). */
    {
        int settled = 1;                         /* bVar12 (init true, 60167) */
        for (int i = 0; i < 2; i++) {
            if (s_cust_active[i] == 0 && s_item_pick[1 + i] > 0) {
                settled = 0;
                s_item_pick[1 + i] -= 1;         /* b278[i] pose-out ramp down */
            }
        }
        int b59c_prev = s_b59c;
        if (s_b59c == 0 && s_b598 > 0) { s_b598 -= 1; settled = 0; }
        if (settled) {
            for (int i = 0; i < 2; i++) {
                if (s_cust_active[i] != 0) {
                    if (s_item_pick[1 + i] < 0xf)
                        s_item_pick[1 + i] += 1;  /* b278[i] pose-in ramp up */
                    s_b548 += 1;                  /* reveal budget +1/active speaker */
                }
            }
            if (b59c_prev > 0 && s_b598 < 0xf) s_b598 += 1;
        }
    }

    if (s_b51c == 0 && s_b534 != 0xf && s_b58c > 0)
        s_b58c -= 1;

    /* arrival-anim ramp (all.c:60238-60255): once the script starts it (b5a0>0),
     * climb to 0x3c.  The '!' sparkle emit + arrival SE are gated on b564 (==0
     * here) ⇒ inert; PORT-DEBT(cs-arrival-fx) for the bubble particle + the SE. */
    if (s_b5a0 > 0 && s_b5a0 < 0x3c)
        s_b5a0 += 1;
    /* PORT-DEBT(cs-payout-anim): the b5c0 sale-payout coin anim (post-sale). */

    /* the cc08==4 COUNTER camera (all.c:60280-60314).  Pin the lookat to the
     * fixed per-shop-tier counter target + orbit the eye, flag stage_class=1 so
     * the camera function (scene1_camera_pose_compute) uses it instead of the
     * player-follow.  (DAT_0438cc38/3c/40 — the old "cs-bubble-pos" note here —
     * is the camera EYE, not a speech-bubble position.)  Shop tier from the save
     * bank, like session_init.  v3-verified vs retail: tier-0 lookat (-3.0, 0.0),
     * eye (-3.0, 14.0). */
    {
        const uint8_t *bank =
            (const uint8_t *)save_work_dwords_at(save_work_active_slot());
        int tier = (bank != NULL)
                 ? (int)((const int32_t *)bank)[SHOP_DISPLAY_TIER_SELECTOR] : 0;
        scene1_camera_cs_counter_cam(tier);
    }

    /* <C>-pause continue (all.c:60318-60324): a <C>-tagged line is fully revealed
     * (b558==1, b55c) and Z pressed → switch the active pointer to the post-<C>
     * tail (b41c) and restart the reveal (b548=0). */
    if (s_b558 == 1 && s_b55c != 0 && (s_in_pressed & 0x10)) {
        s_b270 = s_line_tail;
        s_b548 = 0;
        s_b558 = 0;
        s_b55c = 0;
        return;
    }

    if (s_b520 != 0) {
        /* leave/dissolve → free-roam (all.c:60325-60396).  Reached by the ESC
         * skip (b5e4 Yes) and the live customer's f406 close.  Phase 1 = kick the
         * tile-dissolve once the dissolve clock (b5b4) is past 0xf0; phase 2 =
         * wait for it, then restore free-roam (cc08=1) + clear the sale flags. */
        if (s_b5c0 == 0 && s_b5b4 > 0xf0 && s_b520 == 1) {
            fade_phase1_start(0, 0x5a);      /* FUN_004526f5(0, 0x5a) tile-dissolve-out */
            s_b520 = 2;
        }
        if (!fade_is_done())                 /* FUN_004528b3 — still dissolving */
            return;
        /* dissolve complete (all.c:60334-60395).  The Recette hop-down player
         * reposition + the free-roam camera-class reset (both below) ARE now ported
         * — they fix the post-tutorial WRAP-UP camera (note #9, RE §18.3: retail
         * free-roams the CONV_POSE cutscene off the repositioned player, the port had
         * left the stale cc08==4 counter cam).  PORT-DEBT(cs-leave-restore) still
         * defers the render/anim rest: FUN_0048439a (3D scene), FUN_00473332,
         * FUN_0046f892, FUN_0045e028 (real-sale tally, f404==0 only), the player
         * octant DAT_056dab00 + DAT_056db05c/048; the shop-FULL (fb88>=4) branch is
         * PORT-DEBT(cs-leave-shopfull). */
        s_cs_active = 0;                     /* DAT_0438b7b0 = 0 */
        player_ctrl_cc08_enter_freeroam();   /* DAT_0438cc08 = 1 (the fb88<4 arm;
                                              * PORT-DEBT(cs-leave-shopfull): the
                                              * fb88>=4 "shop full" branch) */
        {
            uint8_t *bank = (uint8_t *)save_work_dwords_at(save_work_active_slot());
            if (bank != NULL) {
                /* Recette hop-down reposition (all.c:60349-371, the fb88<4 arm).
                 * Read f404 BEFORE the clear below (retail order).  Sets the
                 * free-roam camera CENTER (stage_class=0 at the block end) so the
                 * wrap-up cutscene follows Recette, NOT the fixed counter target.
                 * The HOUSE free-roam clamp caps bias_z at 1.0 (scene1_camera.c:214),
                 * so Z only places the sprite; X is the visible camera center
                 * (sale/tutorial → -1.5, matching the measured retail wrap-up eye). */
                {
                    int sale = bank[CS_F404_SELL_ACTIVE_BYTE_OFF] != 0;   /* f404, pre-clear */
                    int tier = (int)((const int32_t *)bank)[SHOP_DISPLAY_TIER_SELECTOR];
                    if (tier < 3) {
                        g_scene1_player_pos[0] = sale ? -1.5f : 0.8f;     /* 0xbfc00000 / 0x3f4ccccd */
                        g_scene1_player_pos[2] = 9.0f;                     /* 0x41100000 */
                    } else {
                        g_scene1_player_pos[0] = sale ? 9.2f : 11.5f;      /* 0x41133333 / 0x41380000 */
                        g_scene1_player_pos[2] = 16.9f;                    /* 0x41873333 */
                    }
                }
                if (bank[CS_F406_TUTORIAL_BYTE_OFF] != 0) {    /* all.c:60381-384 */
                    bank[CS_F406_TUTORIAL_BYTE_OFF] = 0;
                    bank[CS_F402_BYTE_OFF] = 1;
                }
                if (bank[CS_F404_SELL_ACTIVE_BYTE_OFF] == 1) { /* all.c:60385-392 */
                    bank[CS_F404_SELL_ACTIVE_BYTE_OFF] = 0;
                    if (bank[CS_F405_BYTE_OFF] == 0) {
                        bank[CS_F3FF_BYTE_OFF] = 0;
                        bank[CS_F400_DISPLAY_SUPPRESS_OFF] = 1;
                    }
                    bank[CS_F405_BYTE_OFF] = 0;
                }
            }
        }
        fade_phase_out_start(0, 0x1e);       /* FUN_0045281c(0, 0x1e) fade-IN */
        scene1_camera_set_freeroam_class();  /* DAT_0438b4e8 = 0 (all.c:60394) — drop the
                                              * cc08==4 counter cam so the wrap-up cutscene
                                              * + free-roam use the player-follow camera
                                              * (note #9, RE §18.3).  Retires the camera part
                                              * of PORT-DEBT(camera-hint-b4e8). */
        return;
    }

    if (s_b534 != 0) {
        const uint8_t *bank =
            (const uint8_t *)save_work_dwords_at(save_work_active_slot());
        int f404 = (bank != NULL) && bank[CS_F404_SELL_ACTIVE_BYTE_OFF] != 0;
        int f406 = (bank != NULL) && bank[CS_F406_TUTORIAL_BYTE_OFF] != 0;

        if (s_b534 == 1) {
            if (s_b51c != 0) {
                s_b544 += 1;
                if (s_b544 == 1) { s_b600 = 0; s_b604 = 0; s_b608 = 0; }
                cs_scripted_tick();           /* FUN_00461c00 (the scripted sell) */
                return;
            }
            /* live greeting (all.c:60409-60425): the first real customer greets via
             * the line picker, then Z (after the line reveals) → b534=2 (the live
             * machine).  ea70 (cust_active[0]) is held while waiting so the reveal
             * runs the next frame, like retail. */
            s_b544 += 1;
            if (s_b544 == 1)
                cs_pick_line(0, 0, 0);        /* FUN_00460a1a(&ea90,0,0) */
            if (s_b55c == 0)               { s_cust_active[0] = 1; return; }
            if ((s_in_pressed & 0x10) == 0){ s_cust_active[0] = 1; return; }
            s_cust_active[0] = 0;
            s_b534 = 2;
            /* The held cancel-prompt b150 (s_skip_modal) closes 1f LATER, on the
             * first b534==2 frame in cs_live_machine — NOT inline here.  Retail's
             * b534 1→2 (all.c:60409-425) has NO FUN_00435612; retail clears b150 via
             * the choice-box system the frame AFTER the greeting advances, so its
             * PAUSE_CLOSE lands ON the b534==2 frame.  Clearing inline here fired the
             * port's PAUSE_CLOSE 1f early ⇒ the frame-relative offer-Z + the L90
             * {rngseed} re-pin both applied 1f early ⇒ wrong variant/offer (RE §21.11.2). */
            s_b544 = 0;
            s_b55c = 0;
            return;
        }
        if (s_b534 == 0x1e) {
            /* PORT-DEBT(cs-sold-pause): the buy/sold-pause arm (all.c:60427-60459)
             * — not in the SELL trajectory (1→2→6→0xf→7→0xa→0xc→0x14→0x15→0). */
            return;
        }
        if (s_b534 == 10) {                   /* 0xa — "thank you" line (60461-60478) */
            s_b544 += 1;
            if (s_b544 == 1)
                cs_pick_line(0, 5, 0);        /* FUN_00460a1a(&ea90,5,0) */
            if (s_b55c == 0)               { s_cust_active[0] = 1; return; }
            if ((s_in_pressed & 0x10) == 0){ s_cust_active[0] = 1; return; }
            s_cust_active[0] = 1;
            s_b534 = 0xc;                     /* → closing */
            s_b544 = 0;
            s_b55c = 0;
            return;
        }
        if (s_b534 == 0xb) {                  /* 0xb — leave/dissolve dialogue (60480-60515) */
            if (!f404 || s_b51c != 0) {
                s_b52c -= 1;
                s_cust_active[0] = 0;
            } else {
                s_b544 += 1;
                if (0x14 < s_b544) { s_b544 = 0; s_b534 = 0x14; s_b5b8 = 1; }
            }
            s_b5c4 = 0;
            if (s_b52c < 1) {
                s_b544 += 1;
                s_b52c = 0;
                if (s_b544 == 1)
                    cs_pick_line(0, 3, 0);    /* FUN_00460a1a(&ea90,3,0) */
                if (s_b55c != 0) {
                    if (s_in_pressed & 0x10) {
                        s_cust_active[0] = 0;
                        s_b534 = 0xd;
                        s_b544 = 0;
                        s_b55c = 0;
                        return;
                    }
                    s_cust_active[0] = 1;
                    return;
                }
                s_cust_active[0] = 1;
                return;
            }
            s_b5c4 = 0;
            return;
        }
        if (s_b534 == 0x15) {                 /* 0x15 — queue-advance countdown (60517-60525) */
            s_b52c -= 1;
            s_cust_active[1] = 0;
            if (0 < s_b52c)
                return;
            s_b52c = 0;
            /* falls to the common tail (idle reset) */
        } else if (s_b534 == 0x14) {          /* 0x14 — queue-advance dialogue (60527-60562) */
            s_b544 += 1;
            s_cust_active[1] = 1;
            if (s_b544 == 1) {
                int line_id;
                if (s_b5b8 == 0) {
                    line_id = (s_b528 == 2) - 4;   /* -4 normal, -3 if b528==2 */
                } else {
                    s_b528 -= 1;
                    s_b318 -= 1;
                    s_b5b8 = 0;
                    line_id = -2;
                }
                /* load the conclusion / next-customer line by NEGATIVE id
                 * (all.c:60540-60549): scan g_tuto[fileidx]'s 200 records for
                 * id==line_id, load its text via cs_dialogue_line_setup (which
                 * runs the <C> page split) — e.g. tuto1 id -4 = "Expertly done.
                 * If you ever wish to practice again, simply ask me<C>any time we
                 * are in the shop." (retires PORT-DEBT(cs-queue-line)). */
                const struct tuto_record *qr =
                    &g_tuto[s_price_fileidx * TUTO_CONSUMER_STRIDE];
                for (int qi = 0; qi < TUTO_CONSUMER_STRIDE; qi++) {
                    if (qr[qi].id == line_id) {
                        cs_dialogue_line_setup(qr[qi].text, 1, 0);  /* FUN_0046098f(text,1,0) */
                        break;
                    }
                }
            }
            if (s_b55c == 0)
                return;
            if (s_in_pressed & 0x10) {
                s_cust_active[0] = 0;
                s_b534 = 0x15;
                s_b544 = 0;
                s_b55c = 0;
                return;
            }
            return;
        } else if (s_b534 != 0xc && s_b534 != 0xd) {
            /* the b5a8 transaction dispatch (all.c:60563-60588) for the live states
             * (2/6/0xf/7/8/9).  b5a8==2 = the SELL machine FUN_004658ab.
             * PORT-DEBT(cs-other-kinds): 0/1/3/4/5 = buy/chat/kind0/kind5. */
            if (s_b5a8 == 2) {
                cs_live_machine();            /* FUN_004658ab */
                return;
            }
            return;
        } else {                              /* 0xc / 0xd — closing (all.c:60590-60662) */
            int cVar1 = f404;
            if (cVar1 == 0 || s_b51c != 0)
                s_b52c -= 1;
            if (0 < s_b52c) {
                if (cVar1 == 0)    return;
                if (s_b51c != 0)   return;
            }
            if (cVar1 == 0 || s_b51c != 0)
                s_b52c = 0;
            if (s_b534 == 0xc) {
                s_b544 += 1;
                if (s_b51c != 0) {            /* SCRIPTED tutorial close (ported earlier) */
                    s_b51c = 0;
                    s_b524 = 0;
                    s_b534 = 0;
                    s_b55c = 0;
                    return;
                }
                /* live-customer close (60614-60661): show the close line, Z →
                 * queue-advance (f404) or idle (!f404); f406 → leave/dissolve. */
                s_cust_active[0] = 1;
                if (s_b544 == 1) {
                    /* all.c:60616-60626: set the <I> item-name + <Y> "%dpix"
                     * dialogue macros the close line ("Yay! I sold <I> for <Y>!"
                     * = recette msg08) expands.  uVar18 = the close line type by
                     * transaction kind (all.c:60627-60635).
                     * PORT-DEBT(cs-close-fx): the gold banner + DAT_0438b150
                     * pause FX remain (they don't move the b534 state). */
                    cs_set_item_macro(s_b5a4);             /* FUN_004607f3(b5a4) → <I> */
                    {   char pix[DLG_MACRO_BUFSZ];
                        snprintf(pix, sizeof pix, "%dpix", s_price_ask);  /* "%dpix" b5c6bb8 */
                        dlg_macro_set(DLG_MAC_Y, pix); }   /* → <Y> sale price */
                    int close_line = (s_b5a8 == 3) ? 0xb
                                   : (s_b5a8 == 0) ? 7 : 8;
                    cs_pick_line(0, close_line, 0);  /* FUN_00460a1a(&ea90,uVar18,0) */
                }
                if (s_b55c == 0)
                    return;
                if ((s_in_pressed & 0x10) == 0)
                    return;
                if (!f404) {
                    s_b534 = 0;
                } else {
                    s_b544 = 0;
                    s_b534 = 0x14;            /* → queue-advance the next customer */
                }
                if (!f406) {
                    s_cust_active[0] = 0;
                    s_b524 = 0;
                    s_b55c = 0;
                    return;
                }
                s_cust_active[0] = 0;
                s_b520 = 1;                   /* f406 → leave/dissolve to free-roam */
                s_b524 = 0;
                s_b55c = 0;
                return;
            }
            /* b534 == 0xd: falls to the common tail */
        }

        /* common tail (all.c:60665-60668): reset to idle. */
        s_cust_active[0] = 0;
        s_b524 = 0;
        s_b534 = 0;
        return;
    }

    cs_idle_tick();
}

/* ── customer_service_reset — clear the whole state block (test hook / BSS) ──── */
void customer_service_reset(void)
{
    for (int i = 0; i < (int)(sizeof s_queue / sizeof s_queue[0]); i++) s_queue[i] = 0;
    for (int i = 0; i < CS_ROSTER_PERM_N; i++) s_roster_perm[i] = 0;
    for (int i = 0; i < CS_ELIGIBLE_N; i++) s_eligible[i] = 0;
    for (int i = 0; i < (int)(sizeof s_item_pick / sizeof s_item_pick[0]); i++) s_item_pick[i] = 0;
    s_cust_active[0] = s_cust_active[1] = 0;
    s_queue_count = 0;
    s_b318 = s_b51c = s_b520 = s_b524 = s_b528 = s_b52c = s_b530 = s_b534 = 0;
    s_b538 = 0;
    s_b53c = s_b558 = s_b564 = s_b568 = s_b58c = s_b590 = s_b594 = s_b598 = 0;
    s_b59c = s_b5a0 = s_b5ac = s_b5b0 = s_b5b4 = s_b5b8 = s_b5bc = s_b5c0 = 0;
    s_b5c4 = s_b5c8 = s_b5cc = s_b5d0 = s_b5d4 = s_b5e4 = s_skip_modal = 0;
    s_b540 = s_b544 = s_b548 = s_b54c = s_b550 = s_b55c = s_b560 = s_b570 = 0;
    s_b270 = NULL;
    s_line_buf[0] = s_line_tail[0] = '\0';
    s_b5a4 = s_b600 = s_b604 = s_b608 = 0;
    s_b574 = s_b57c = s_b580 = s_b584 = s_b588 = 0;
    s_b1cc = 0;
    s_csload_hold = 0;             /* {csloadpin} bracket counter (pin itself persists) */
    s_in_cur = s_in_pressed = s_in_held = 0;
    s_b5a8 = -1;
    g_scene_buy_current_page = 0;          /* DAT_0730b56c */
    s_price_fileidx = s_price_bb4 = s_price_ask = s_price_runsum = 0;
    s_price_base = s_price_bc4 = s_price_bc8 = s_price_cursor = 0;
    s_cs_active = 0;
}
