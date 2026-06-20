/*
 * customer_service.c — see customer_service.h.
 *
 * The cc08==4 in-shop customer-service / selling mode.  Transcribed from the
 * unpacked disassembly + docs/decompiled (the integer bookkeeping is faithful in
 * the decompile; the haggle FP lives in customer_haggle.c).  The TUTORIAL haggle
 * is the kind-2 machine FUN_004658ab (NOT the kind-4 FUN_00463cfb — see the RE
 * doc §3.5 correction); this file ports the entry (FUN_0045edaa) first.
 */

#include "customer_service.h"
#include "customer_haggle.h"
#include "rng.h"
#include "save_work.h"
#include "save_bank.h"
#include "scene_buy.h"        /* g_scene_buy_current_page == engine DAT_0730b56c */
#include "worker_load.h"      /* worker_load_spawn_d3e == FUN_00452d3e */
#include "tables_kyaku.h"     /* g_kyaku — the customer tuning fields */
#include "tables_item.h"      /* g_item / tables_item_find_slot_by_id (FUN_004681f6) */
#include "tables_tuto.h"      /* g_tuto — the scripted-sell script (FUN_00461c00 consumer) */
#include "customer_haggle.h"  /* haggle_offer_up (FUN_00460161) */
#include "scene1_shop_display.h"  /* SHOP_DISPLAY_TIER_SELECTOR (0xb378) */
#include "scene1_camera.h"        /* scene1_camera_cs_counter_cam (cc08==4 counter cam) */

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

/* ── FUN_0046f8ba — load the eligible customers' portrait textures ───────────
 * Records, for each eligible-list entry, the index of its loaded portrait
 * texture (engine DAT_073a7f30[] from the DAT_005c7ce0 name→handle table) +
 * DAT_005c7dd0 = count.  Read by the render (FUN_0046602e portrait draw).
 *
 * PORT-DEBT(cs-portrait-load): the texture table (DAT_005c7ce0) and the index
 * array (DAT_073a7f30) are not yet modeled in the port — deferred to the render
 * chip (step 3).  Stubbed no-op here: it draws no RNG and touches no state the
 * entry/state-machine reads, so it is inert for the entry + the flow-trace
 * verification.  Retire with FUN_0046602e (the customer portrait). */
static void cs_load_eligible_portraits(const int32_t *eligible)
{
    (void)eligible;
}

/* ── customer_service_session_init — FUN_0045edaa (TUTORIAL path) ───────────── */
void customer_service_session_init(void)
{
    const uint8_t *bank = (const uint8_t *)save_work_dwords_at(save_work_active_slot());

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
    worker_load_spawn_d3e(0);
}

/* Load-worker completion (DAT_0438b1cc 2 → 1): the cc08==4 asset-load worker's
 * callback calls this when the customer-service assets finish loading.  The
 * engine's d3e worker BODY (LAB_00452ae8/b13) writes DAT_0438b1cc = 1 — NOT 0 —
 * which is the state BOTH the master tick (runs while != 2) AND the render
 * (FUN_0046602e draws while == 1) read.  The wiring (scene1_player_ctrl) calls
 * this once the d3e worker is no longer pending; host tests call it directly. */
void customer_service_notify_loaded(void) { s_b1cc = 1; }

/* ── accessors ─────────────────────────────────────────────────────────────── */
int32_t customer_service_b534(void)        { return s_b534; }
int32_t customer_service_player_ask(void)  { return s_price_ask; }
int32_t customer_service_offer(void)       { return s_b574; }
int32_t customer_service_base_price(void)  { return s_price_base; }
int32_t customer_service_b5a8(void)        { return s_b5a8; }
int32_t customer_service_b56c(void)        { return g_scene_buy_current_page; }
int32_t customer_service_arrival_anim(void){ return s_b5a0; }
int32_t customer_service_round(void)       { return s_b584; }
int32_t customer_service_b520(void)        { return s_b520; }
int32_t customer_service_b524(void)        { return s_b524; }
int32_t customer_service_b544(void)        { return s_b544; }
int32_t customer_service_b590(void)        { return s_b590; }
int32_t customer_service_b1cc(void)        { return s_b1cc; }
int32_t customer_service_active(void)      { return s_cs_active; }
int32_t customer_service_b51c(void)        { return s_b51c; }
int32_t customer_service_b608(void)        { return s_b608; }
int32_t customer_service_b604(void)        { return s_b604; }   /* the script PC (g_tuto index) */
int32_t customer_service_fileidx(void)     { return s_price_fileidx; }
/* The BARGAIN price-confirm choice is open (scripted machine b608==4 — the
 * cs_input_poll Yes/No state that ramps b58c).  Retail sets DAT_0438b150 (the
 * shared modal-cursor flag, → the PAUSE_OPEN anchor) here via choice_box_open;
 * the port split b150 so the haggle never set the flag the anchor reads.  This
 * lets the anchor OR it in so PAUSE_OPEN fires at the BARGAIN like retail
 * (RE §9.6) — the prerequisite for replaying any haggle trace on the port. */
int32_t customer_service_bargain_active(void) { return s_b51c != 0 && s_b608 == 4; }
void    customer_service_set_script_file(int32_t idx) { s_price_fileidx = idx; }

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

/* ── FUN_00461303 f404 head — the sell-active kind selector ──────────────────
 * The only branch the scripted sell reaches (all.c:59312-59317): bind the active
 * customer (b56c/b570 from the queue head) + the offered-item handle and select
 * the b5a8==2 dispatch.  Returns 1 on this path.
 * PORT-DEBT(cs-kind-select-full): the f406 / roster-scan / buysell-debug branches
 * (all.c:59319-59514) are the autonomous-customer path, not the sell. */
static int cs_kind_select(void)
{
    int e = s_roster_perm[s_b318];
    g_scene_buy_current_page = s_queue[e * CS_QUEUE_STRIDE + 0]; /* b56c = queue[*].kyaku */
    s_b570                   = s_queue[e * CS_QUEUE_STRIDE + 1]; /* b570 = queue[*].item_slot */
    s_b5a4 = 0xc0;                                               /* offered-item handle */
    s_b5a8 = 2;
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
static void cs_dialogue_line_setup(const char *text, int idx, int32_t chr_arg)
{
    if (idx == 0) s_b54c = chr_arg;          /* (&DAT_0730b54c)[idx] = chr_arg */
    else          s_b550 = chr_arg;
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
    /* PORT-DEBT(cs-debug-leave): the b5e4==1 debug-skip branch (all.c:60168-60186). */

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
        /* PORT-DEBT(cs-leave): the leave/dissolve → queue-advance / cc08 reset
         * (all.c:60325-60396); b520==0 in the captured window. */
        return;
    }

    if (s_b534 != 0) {
        if (s_b534 == 1) {
            if (s_b51c != 0) {
                s_b544 += 1;
                if (s_b544 == 1) { s_b600 = 0; s_b604 = 0; s_b608 = 0; }
                cs_scripted_tick();           /* FUN_00461c00 (the scripted sell) */
                return;
            }
            /* PORT-DEBT(cs-generic-greeting): the b51c==0 live-greeting arm
             * (all.c:60409-60425) → b534=2 → FUN_004658ab; unused by the scripted
             * tutorial sell (b51c==1). */
            return;
        }
        /* PORT-DEBT(cs-closing-states): b534 ∈ {0x1e,10,0xb,0x15,0x14,0xc,0xd}
         * closing / sold-pause / queue-advance + the b5a8 live-machine dispatch
         * (all.c:60427-60668) — reached at script end (b534→0xc), beyond window. */
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
    s_b53c = s_b558 = s_b564 = s_b568 = s_b58c = s_b590 = s_b594 = s_b598 = 0;
    s_b59c = s_b5a0 = s_b5ac = s_b5b0 = s_b5b4 = s_b5b8 = s_b5bc = s_b5c0 = 0;
    s_b5c4 = s_b5c8 = s_b5cc = s_b5d0 = s_b5d4 = 0;
    s_b540 = s_b544 = s_b548 = s_b54c = s_b550 = s_b55c = s_b560 = s_b570 = 0;
    s_b270 = NULL;
    s_line_buf[0] = s_line_tail[0] = '\0';
    s_b5a4 = s_b600 = s_b604 = s_b608 = 0;
    s_b574 = s_b57c = s_b580 = s_b584 = s_b588 = 0;
    s_b1cc = 0;
    s_in_cur = s_in_pressed = s_in_held = 0;
    s_b5a8 = -1;
    g_scene_buy_current_page = 0;          /* DAT_0730b56c */
    s_price_fileidx = s_price_bb4 = s_price_ask = s_price_runsum = 0;
    s_price_base = s_price_bc4 = s_price_bc8 = s_price_cursor = 0;
    s_cs_active = 0;
}
