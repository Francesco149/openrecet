/*
 * test_customer_service.c — the cc08==4 customer-service ENTRY (FUN_0045edaa,
 * tutorial forced-sale path).  Verifies the session-init STATE setup + the
 * load-bearing single RNG draw (the customer-count draw is consumed on every
 * path; its order/count must match retail for LCG parity).
 */
#include "t.h"
#include <string.h>
#include "../src/customer_service.h"
#include "../src/save_work.h"
#include "../src/save_bank.h"
#include "../src/tables_item.h"
#include "../src/tables_kyaku.h"
#include "../src/tables_tuto.h"
#include "../src/rng.h"
#include "../src/choice_box.h"   /* the ESC "Cancelling tutorial?" skip prompt */

/* DAT_0450f406 / f404 byte offsets within a slot bank (rel. save_work_dwords_at). */
#define F406_TUTORIAL_BYTE_OFF 0x2bc6e
#define F404_SELL_ACTIVE_BYTE_OFF 0x2bc6c

/* Clear the active slot bank + fill the 15×20 display grid with -1 (empty). */
static uint32_t *cs_test_bank_clean(void)
{
    save_work_set_active_slot(0);
    uint32_t *bank = save_work_dwords_at(0);
    uint8_t *bb = (uint8_t *)bank;
    bb[F406_TUTORIAL_BYTE_OFF] = 0;
    bb[F404_SELL_ACTIVE_BYTE_OFF] = 0;
    for (int i = 0; i < 15 * 20; i++)
        bank[SAVE_BANK_FIELD_DISPLAY_GRID + i] = 0xffffffffu;  /* empty cell */
    return bank;
}

/* The tutorial forced-sale path (f406 != 0) queues exactly one customer = kyaku
 * 13 ("Woman"), item slot 0, kind 0; seeds the eligible list {13, -2}; and
 * leaves the machine idle (b534=0, b5a8=-1). */
int test_cs_session_init_tutorial_queue(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F406_TUTORIAL_BYTE_OFF] = 1;   /* arm the tutorial sale */
    rng_seed(0x1234);

    customer_service_session_init();

    T_ASSERT_EQ_I(customer_service_queue_kyaku(0), 0xd);      /* kyaku 13 */
    T_ASSERT_EQ_I(customer_service_queue_item_slot(0), 0);
    T_ASSERT_EQ_I(customer_service_queue_kind(0), 0);
    T_ASSERT_EQ_I(customer_service_queue_count(), 1);
    T_ASSERT_EQ_I(customer_service_eligible(0), 0xd);
    T_ASSERT_EQ_I(customer_service_eligible(1), -2);
    T_ASSERT_EQ_I(customer_service_eligible(2), -1);          /* terminator+rest */
    T_ASSERT_EQ_I(customer_service_b534(), 0);               /* idle */
    return 0;
}

/* The customer-count RNG draw (all.c:57455) happens BEFORE the path branch, so
 * the entry consumes EXACTLY one LCG step on the tutorial path — load-bearing
 * for the shared RNG stream staying aligned with retail. */
int test_cs_session_init_draws_exactly_one_rng(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F406_TUTORIAL_BYTE_OFF] = 1;
    rng_seed(0x1234);

    unsigned long before = rng_call_count();
    customer_service_session_init();
    T_ASSERT_EQ_U(rng_call_count() - before, 1);
    return 0;
}

/* A second session_init (re-entry) must rebuild the same queue from scratch (the
 * entry zeroes its state) — a stale customer must not bleed in. */
int test_cs_session_init_reentry_rebuilds(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F406_TUTORIAL_BYTE_OFF] = 1;
    rng_seed(0x99);
    customer_service_session_init();
    customer_service_session_init();   /* re-enter */
    T_ASSERT_EQ_I(customer_service_queue_kyaku(0), 0xd);
    T_ASSERT_EQ_I(customer_service_eligible(0), 0xd);
    T_ASSERT_EQ_I(customer_service_eligible(1), -2);
    return 0;
}

/* The sell-active / scripted-sell path (f404 != 0) — the path the customer-service
 * TUTORIAL takes (RE doc §3.7): a 3-deep queue of placeholder customers (kyaku=1,
 * kind=0; item_slot left = entry index), count=3.  b56c will resolve to queue[0].
 * kyaku=1 in the master tick (matching the BIT-EXACT capture's b56c=1). */
int test_cs_session_init_sell_active_queue(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;   /* arm the sell */
    rng_seed(0x1234);

    customer_service_session_init();

    T_ASSERT_EQ_I(customer_service_queue_kyaku(0), 1);
    T_ASSERT_EQ_I(customer_service_queue_kyaku(1), 1);
    T_ASSERT_EQ_I(customer_service_queue_kyaku(2), 1);
    T_ASSERT_EQ_I(customer_service_queue_kind(0), 0);
    T_ASSERT_EQ_I(customer_service_queue_count(), 3);
    T_ASSERT_EQ_I(customer_service_queue_item_slot(0), 0);  /* left = entry idx */
    T_ASSERT_EQ_I(customer_service_queue_item_slot(1), 1);
    T_ASSERT_EQ_I(customer_service_b534(), 0);              /* idle */
    return 0;
}

/* Drive the master tick through the idle → greeting on the sell path.  The kind
 * selector (b524==0x3c) sets b5a8=2, b56c=queue[0].kyaku(=1), and the offered
 * handle b5a4=0xc0 (item id 3).  The greeting trigger (b524>0x77 & b52c>=0x20)
 * promotes b534 0→1 and computes base = item.price·count(=1) and
 * ask = ftol((float)item.price) [b5a8==2].  Mirrors the BIT-EXACT capture's
 * greeting frame: b56c=1, b5a8=2, base=ask=3000 (item id 3 → price 3000), b534=1. */
int test_cs_master_tick_sell_trajectory(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;
    rng_seed(0x1234);

    /* item DB: id 3 → price 3000 (the f404 selector points b5a4=0xc0 at id 3).
     * Borrow + restore the shared g_item slot 0 so other tests are unaffected. */
    int32_t save_count = g_item.count;
    int32_t save_id    = g_item.records[0].item_id;
    int32_t save_price = g_item.records[0].price;
    g_item.count = 1;
    g_item.records[0].item_id = 3;
    g_item.records[0].price   = 3000;

    customer_service_session_init();

    /* Play the role of the worker thread: release each d3e asset-load gate the
     * frame after it spawns (mirrors the cc08==4 arm's notify bridge).  Both the
     * session-init load AND the second/occ3 load (FUN_00452d3e(1), spawned at
     * b524==0x3c) re-arm b1cc=2; in the host build no thread runs, so the test
     * drives the completion.  Released at the tick HEAD ⇒ no frozen frames. */
    for (int i = 0; i < 200; i++) {
        if (customer_service_b1cc() == 2)
            customer_service_notify_loaded();
        customer_service_master_tick(0, 0, 0);    /* no input → idle climbs to greeting */
    }

    int b5a8 = customer_service_b5a8();
    int b56c = customer_service_b56c();
    int b534 = customer_service_b534();
    int base = customer_service_base_price();
    int ask  = customer_service_player_ask();

    g_item.count = save_count;                 /* restore shared state */
    g_item.records[0].item_id = save_id;
    g_item.records[0].price   = save_price;

    T_ASSERT_EQ_I(b5a8, 2);                    /* kind selector ran */
    T_ASSERT_EQ_I(b56c, 1);                    /* queue[0].kyaku */
    T_ASSERT_EQ_I(b534, 1);                    /* greeting */
    T_ASSERT_EQ_I(base, 3000);                 /* item.price · 1 */
    T_ASSERT_EQ_I(ask, 3000);                  /* ftol((float)item.price) */
    return 0;
}

/* The master tick is inert while the asset-load worker is running (b1cc==2, set by
 * session_init); the idle only advances once the load callback (notify_loaded)
 * fires — matching the engine's `if (DAT_0438b1cc == 2) return;` gate. */
int test_cs_master_tick_idle_gated_by_load(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;
    rng_seed(0x1234);
    customer_service_session_init();           /* sets b1cc=2 (loading) */

    for (int i = 0; i < 200; i++)
        customer_service_master_tick(0, 0, 0);
    T_ASSERT_EQ_I(customer_service_b534(), 0);  /* still idle — load not done */
    T_ASSERT_EQ_I(customer_service_b5a8(), -1); /* selector never ran */

    /* Release the gates and drive on: the session-init load AND the occ3 load
     * (FUN_00452d3e(1) at b524==0x3c) both re-arm b1cc=2; play the worker each
     * frame (mirrors the cc08==4 arm) so the idle reaches the greeting. */
    for (int i = 0; i < 200; i++) {
        if (customer_service_b1cc() == 2)
            customer_service_notify_loaded();
        customer_service_master_tick(0, 0, 0);
    }
    T_ASSERT_EQ_I(customer_service_b534(), 1);  /* now reaches the greeting */
    T_ASSERT_EQ_I(customer_service_b5a8(), 2);
    return 0;
}

/* Chip 2b — the scripted machine FUN_00461c00 drives the price/offer flow.
 * Minimal script (file 0): [op 2 price-set, op 4 PRIA, sentinel].  With item 2 →
 * price 1200 and kyaku[1] initial=128/random=0, the customer's first offer is
 * 1200·128/100 = 1536 (no f406 override) — the BIT-EXACT capture's b574 value.
 * Drives the full path: idle → greeting (base 3000, item 3 via b5a4=0xc0) →
 * scripted op 2 (base→1200, item 2) → op 4 PRIA, Z → FUN_00460161 offer 1536. */
int test_cs_scripted_first_offer(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;
    rng_seed(0x1234);

    /* borrow the shared DBs: g_item id 3→3000 (greeting), id 2→1200 (op 2);
     * g_kyaku[1] tuning; g_tuto[0..2] script. Restore all at the end. */
    int32_t      sv_cnt = g_item.count;
    item_record_t sv_r0 = g_item.records[0], sv_r1 = g_item.records[1];
    kyaku_record_t sv_k1 = g_kyaku.records[1];
    struct tuto_record sv_t0 = g_tuto[0], sv_t1 = g_tuto[1], sv_t2 = g_tuto[2];

    g_item.count = 2;
    memset(&g_item.records[0], 0, sizeof g_item.records[0]);
    memset(&g_item.records[1], 0, sizeof g_item.records[1]);
    g_item.records[0].item_id = 3; g_item.records[0].price = 3000;
    g_item.records[1].item_id = 2; g_item.records[1].price = 1200;
    g_kyaku.records[1].initial = 128;
    g_kyaku.records[1].random  = 0;
    memset(&g_tuto[0], 0, sizeof g_tuto[0]);
    memset(&g_tuto[1], 0, sizeof g_tuto[1]);
    memset(&g_tuto[2], 0, sizeof g_tuto[2]);
    g_tuto[0].id = 0; g_tuto[0].opcode = 2;     /* price-set */
    g_tuto[1].id = 1; g_tuto[1].opcode = 4;     /* PRIA */
    g_tuto[2].id = 2; g_tuto[2].opcode = -1;    /* sentinel */

    customer_service_session_init();

    int greet_base = 0;
    for (int i = 0; i < 160; i++) {             /* idle → greeting → scripted op 2 */
        if (customer_service_b1cc() == 2)       /* release each d3e load gate (incl. occ3) */
            customer_service_notify_loaded();
        customer_service_master_tick(0, 0, 0);
        if (customer_service_b534() == 1 && greet_base == 0)
            greet_base = customer_service_base_price();   /* item 3 = 3000 */
    }
    int base_after_op2 = customer_service_base_price();    /* op 2 → item 2 = 1200 */
    customer_service_master_tick(0, 0x10, 0);   /* op 4 PRIA + Z → compute offer */

    int offer = customer_service_offer();
    int round = customer_service_round();

    g_item.count = sv_cnt; g_item.records[0] = sv_r0; g_item.records[1] = sv_r1;
    g_kyaku.records[1] = sv_k1;
    g_tuto[0] = sv_t0; g_tuto[1] = sv_t1; g_tuto[2] = sv_t2;

    T_ASSERT_EQ_I(greet_base, 3000);            /* greeting item 3 */
    T_ASSERT_EQ_I(base_after_op2, 1200);        /* scripted op 2 → item 2 */
    T_ASSERT_EQ_I(offer, 1536);                 /* 1200 · 128/100, no f406 override */
    T_ASSERT_EQ_I(round, 1);                    /* haggle round 0 → 1 */
    return 0;
}

/* Chip 2d — the SECOND d3e asset-load (occ3, FUN_00452d3e(1)) fires from the
 * master-tick queue-advance (b524==0x3c) once a customer is queued (b56c>0) and
 * no leave is in progress (b520==0).  It re-arms the load gate (b1cc=2), pausing
 * the master tick the same way retail does — the load-structure fix that aligns
 * the haggle-window frame count / RNG with retail (RE §8.3).  Verifies the gate
 * re-arms at exactly b524==0x3c with the queued customer bound. */
int test_cs_occ3_second_load_gates_at_queue_advance(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;
    rng_seed(0x1234);

    customer_service_session_init();            /* first (session-init) load: b1cc=2 */
    customer_service_notify_loaded();           /* release it → master tick active */

    /* Drive the idle; catch the frame the gate re-arms (the occ3 spawn). */
    int rearmed_at_b524 = -1;
    for (int i = 0; i < 70; i++) {
        customer_service_master_tick(0, 0, 0);
        if (customer_service_b1cc() == 2) {     /* occ3 re-armed the gate */
            rearmed_at_b524 = customer_service_b524();
            break;
        }
    }
    T_ASSERT_EQ_I(rearmed_at_b524, 0x3c);       /* fires at the queue-advance frame */
    T_ASSERT_EQ_I(customer_service_b56c(), 1);  /* the b56c>0 queued-customer gate */
    return 0;
}

/* Closing reset — when the scripted PC hits the -1 sentinel the scripted tick
 * stamps b534=0xc, and the master tick's closing branch (all.c:60605-60613,
 * the b51c!=0 arm) resets the session back to idle (b51c=0, b524=0, b534=0).
 * Before this was ported the tutorial SOFTLOCKED at "If you can sell me an
 * item…" (the script-end close was a bare `return`).  Drive: greeting →
 * sentinel → b534=0xc → one more tick → b534==0, b51c==0. */
int test_cs_closing_resets_session_at_sentinel(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;
    rng_seed(0x1234);

    int32_t      sv_cnt = g_item.count;
    item_record_t sv_r0 = g_item.records[0];
    struct tuto_record sv_t0 = g_tuto[0];

    g_item.count = 1;
    memset(&g_item.records[0], 0, sizeof g_item.records[0]);
    g_item.records[0].item_id = 3; g_item.records[0].price = 3000;
    /* g_tuto[0] = the -1 sentinel: the very first scripted tick closes. */
    memset(&g_tuto[0], 0, sizeof g_tuto[0]);
    g_tuto[0].id = 0; g_tuto[0].opcode = -1;

    customer_service_session_init();

    int saw_closing = 0;
    for (int i = 0; i < 200; i++) {
        if (customer_service_b1cc() == 2)
            customer_service_notify_loaded();
        customer_service_master_tick(0, 0, 0);
        if (customer_service_b534() == 0xc) saw_closing = 1;
        /* once closed + reset back to idle, stop */
        if (saw_closing && customer_service_b534() == 0 &&
            customer_service_b51c() == 0)
            break;
    }

    int b534 = customer_service_b534();
    int b51c = customer_service_b51c();

    g_item.count = sv_cnt; g_item.records[0] = sv_r0;
    g_tuto[0] = sv_t0;

    T_ASSERT_EQ_I(saw_closing, 1);              /* sentinel → b534=0xc */
    T_ASSERT_EQ_I(b534, 0);                     /* closing reset → idle */
    T_ASSERT_EQ_I(b51c, 0);                     /* scripted session ended */
    return 0;
}

/* ── ESC "Cancelling tutorial?" skip (FUN_0045e6a5 + the master-tick b5e4 poll) ──
 * The cc08==4 skip is gated on the SCRIPTED tutorial (b51c==1) + not already
 * leaving (b520==0) / armed (b5e4==0).  Arming opens the choice box and latches
 * b5e4; a live customer (b51c==0) is NOT skippable. */
int test_cs_esc_skip_arms_during_tutorial(void)
{
    /* tutorial / scripted-sell path → b51c==1. */
    customer_service_reset();
    choice_box_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;
    rng_seed(0x1234);
    customer_service_session_init();
    T_ASSERT_EQ_I(customer_service_b51c(), 1);          /* scripted tutorial */
    T_ASSERT_EQ_I(customer_service_b5e4(), 0);

    /* ESC arms the prompt: returns 1, latches b5e4, opens the choice box. */
    T_ASSERT_EQ_I(customer_service_esc_skip_arm(), 1);
    T_ASSERT_EQ_I(customer_service_b5e4(), 1);
    T_ASSERT(choice_box_active());
    /* a second ESC while armed is a no-op (FUN_0045e6a5 b5e4!=0 gate). */
    T_ASSERT_EQ_I(customer_service_esc_skip_arm(), 0);
    T_ASSERT_EQ_I(customer_service_b5e4(), 1);

    /* a LIVE customer (b51c==0, e.g. the tutorial forced-sale path) is NOT
     * skippable — ESC falls through (returns 0, no prompt). */
    customer_service_reset();
    choice_box_reset();
    bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F406_TUTORIAL_BYTE_OFF] = 1;      /* forced sale → b51c==0 */
    rng_seed(0x1234);
    customer_service_session_init();
    T_ASSERT_EQ_I(customer_service_b51c(), 0);
    T_ASSERT_EQ_I(customer_service_esc_skip_arm(), 0);
    T_ASSERT_EQ_I(customer_service_b5e4(), 0);
    T_ASSERT(!choice_box_active());
    return 0;
}

/* Yes on the skip prompt starts the leave: the master-tick b5e4 poll commits the
 * choice box (CB_OPT0) → b520 (leave/dissolve phase) leaves 0.  Mirrors the
 * scenario-drive (b520 0→1→2 → dissolve → free-roam) verified vs retail. */
int test_cs_esc_skip_yes_starts_leave(void)
{
    customer_service_reset();
    choice_box_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F404_SELL_ACTIVE_BYTE_OFF] = 1;
    rng_seed(0x1234);
    customer_service_session_init();
    customer_service_notify_loaded();                  /* clear the b1cc load gate */

    T_ASSERT_EQ_I(customer_service_esc_skip_arm(), 1); /* arm the prompt */

    /* drive the master tick holding Z (0x10): the choice box open-anim completes,
     * then the Z-edge commits Yes (sel 0) → the b5e4 poll starts the leave. */
    int b520 = 0;
    for (int i = 0; i < 40; i++) {
        customer_service_master_tick(0x10, 0x10, 0);
        b520 = customer_service_b520();
        if (b520 != 0)
            break;
    }
    T_ASSERT(b520 != 0);                                /* leave/dissolve started */
    T_ASSERT_EQ_I(customer_service_b5e4(), 0);          /* prompt consumed */
    return 0;
}

/* ── the LIVE kind-2 sell machine FUN_004658ab (the first real customer) ──────
 * The forced-sale path (f406!=0) runs b51c=0 → the live machine (FUN_004658ab),
 * NOT the scripted tutorial.  Drives the FULL sell cycle deterministically:
 * idle → greeting (b534=1,b51c=0) → live machine 2 (greeting) → 6 (reaction) →
 * 0xf (decision, offer>=ask → accept) → 7 → master-tick 0xa (thanks) → 0xc
 * (close) → (f406 → b520=1 leave/dissolve).  kyaku 13 initial=128 ⇒ the first
 * offer (base 3000 × 1.28 = 3840) ≥ ask 3000 ⇒ the accept branch.  This is the
 * un-softlock: pre-fix the b534==1,b51c==0 greeting was a bare return (frozen). */
int test_cs_live_machine_sell_cycle(void)
{
    customer_service_reset();
    uint32_t *bank = cs_test_bank_clean();
    ((uint8_t *)bank)[F406_TUTORIAL_BYTE_OFF] = 1;   /* forced sale → b51c=0 (live) */
    rng_seed(0x1234);

    int32_t      sv_cnt = g_item.count;
    item_record_t sv_r0 = g_item.records[0];
    kyaku_record_t sv_k13 = g_kyaku.records[13];
    g_item.count = 1;
    memset(&g_item.records[0], 0, sizeof g_item.records[0]);
    g_item.records[0].item_id = 3; g_item.records[0].price = 3000;
    g_kyaku.records[13].initial = 128;   /* offer = 3000·1.28 = 3840 ≥ ask 3000 */
    g_kyaku.records[13].random  = 0;

    customer_service_session_init();
    T_ASSERT_EQ_I(customer_service_b51c(), 0);        /* live (NOT scripted) */

    /* idle → greeting (no input). */
    for (int i = 0; i < 200; i++) {
        if (customer_service_b1cc() == 2) customer_service_notify_loaded();
        customer_service_master_tick(0, 0, 0);
    }
    T_ASSERT_EQ_I(customer_service_b534(), 1);        /* the first-customer greeting */
    T_ASSERT_EQ_I(customer_service_b51c(), 0);        /* b51c==0 (used to FREEZE here) */

    /* drive the sell with Z held: greeting → 2 → 6 → 0xf → 7 → 0xa → 0xc → leave. */
    int seen[64]; for (int i = 0; i < 64; i++) seen[i] = 0;
    int leave = 0;
    for (int i = 0; i < 900; i++) {
        if (customer_service_b1cc() == 2) customer_service_notify_loaded();
        customer_service_master_tick(0x10, 0x10, 0);
        int s = customer_service_b534();
        if (s >= 0 && s < 64) seen[s] = 1;
        if (customer_service_b520() != 0) { leave = 1; break; }
    }

    g_item.count = sv_cnt;                            /* restore shared state */
    g_item.records[0] = sv_r0;
    g_kyaku.records[13] = sv_k13;

    T_ASSERT(seen[2]);            /* live-machine greeting */
    T_ASSERT(seen[6]);            /* reaction / price-edit */
    T_ASSERT(seen[0xf]);         /* haggle decision */
    T_ASSERT(seen[7]);            /* ACCEPT (offer ≥ ask) */
    T_ASSERT(seen[0xa]);         /* master-tick "thank you" */
    T_ASSERT(seen[0xc]);         /* master-tick close */
    T_ASSERT(leave);              /* f406 close → b520 leave (no softlock) */
    return 0;
}
