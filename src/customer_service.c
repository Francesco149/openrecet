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
#include "scene1_shop_display.h"  /* SHOP_DISPLAY_TIER_SELECTOR (0xb378) */

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

/* shared price scalars (DAT_005c6bXX). */
static int32_t s_price_fileidx;  /* DAT_005c6bb0 — active dialogue-file index */
static int32_t s_price_ask;      /* DAT_005c6bb8 — player's asking price */
static int32_t s_price_runsum;   /* DAT_005c6bbc — running base-price sum */
static int32_t s_price_base;     /* DAT_005c6bc0 — base/reference price */
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
        /* PORT-DEBT(cs-sell-active): the sell-active (f404) re-entry path
         * (all.c:58234-58249) — sets b51c=1 + queue[*]=1; not the tutorial path. */
    }

    /* FUN_00452d3e(0) (all.c:58250): spawn the customer-service asset-load worker
     * (DAT_0438b1cc = 2 = "loading" — the master tick is inert until it clears). */
    worker_load_spawn_d3e(0);
}

/* ── accessors ─────────────────────────────────────────────────────────────── */
int32_t customer_service_b534(void)       { return s_b534; }
int32_t customer_service_player_ask(void)  { return s_price_ask; }
int32_t customer_service_offer(void)       { return s_item_pick[0]; /* placeholder */ }
int32_t customer_service_base_price(void)  { return s_price_base; }

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

/* master tick — ported in the next chip. */
void customer_service_master_tick(void) { }

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
    s_b5a8 = -1;
    s_price_fileidx = s_price_ask = s_price_runsum = s_price_base = 0;
    s_price_cursor = 0;
    s_cs_active = 0;
}
