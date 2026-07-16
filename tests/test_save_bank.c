/*
 * test_save_bank.c — coverage for the save-arena port
 * (src/save_bank.{c,h} = FUN_004901c2 + FUN_0049001c + helpers).
 *
 * Pure-C module — no Win32 surface — so we exercise the full init
 * paths in-process under ASan + UBSan, then verify named field
 * constants, checksum verify, slider defaults, and the post-init RNG
 * state.
 */
#include "t.h"
#include "chara_equip.h"
#include "save_bank.h"
#include "tables_chara.h"
#include "rng.h"

/* Shared chara test fixture — engine reads g_chara[] inside
 * save_bank_init_one(); blank it to known values so the assertions
 * are stable irrespective of vendor data state. */
static void seed_chara_zero(void)
{
    memset(g_chara, 0, sizeof g_chara);
}

static void seed_chara_minimal(void)
{
    /* Just enough to exercise the chara loop with non-zero values. */
    memset(g_chara, 0, sizeof g_chara);
    g_chara[0].level_threshold = 0;
    g_chara[0].hp_base = 50;
    g_chara[0].sp_base = 30;
    g_chara[0].at_base = 10;
    g_chara[0].df_base = 13;
    g_chara[0].mt_base = 5;
    g_chara[0].mf_base = 10;
    g_chara[0].hp_lv100 = 500;
    g_chara[0].sp_lv100 = 300;
    g_chara[1].level_threshold = 5;
    g_chara[1].hp_base = 60;
}

/* ── Arena geometry / sizing ── */

int test_save_bank_arena_sizing(void)
{
    /* Sanity: 100 banks × stride + header == 18,838,832 = 0x011f7530. */
    T_ASSERT_EQ_U(SAVE_BANK_ARENA_BYTES, 0x011f7530u);
    T_ASSERT_EQ_U(SAVE_BANK_STRIDE_BYTES, 0x2dfc8u);
    T_ASSERT_EQ_U(SAVE_BANK_HEADER_BYTES, 0x0b10u);
    T_ASSERT_EQ_U(SAVE_BANK_COUNT, 100u);
    return 0;
}

int test_save_bank_bank_pointer_arithmetic(void)
{
    uint8_t *base = save_arena_base();
    T_ASSERT(base != NULL);
    T_ASSERT(save_bank_at(0)  == base + SAVE_BANK_HEADER_BYTES);
    T_ASSERT(save_bank_at(1)  == base + SAVE_BANK_HEADER_BYTES + SAVE_BANK_STRIDE_BYTES);
    T_ASSERT(save_bank_at(99) == base + SAVE_BANK_HEADER_BYTES + 99 * SAVE_BANK_STRIDE_BYTES);
    T_ASSERT(save_bank_at(-1) == NULL);
    T_ASSERT(save_bank_at(100) == NULL);
    return 0;
}

/* ── init_all on a fresh arena ── */

int test_save_bank_init_all_seeds_header_magic(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);

    T_ASSERT_EQ_U(save_header_magic(), 0u);
    save_bank_init_all();
    T_ASSERT_EQ_U(save_header_magic(), SAVE_BANK_MAGIC);
    return 0;
}

int test_save_bank_init_all_seeds_slider_defaults(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    T_ASSERT_EQ_I(save_header_get_se_slider(),   SAVE_HEADER_SE_DEFAULT);
    T_ASSERT_EQ_I(save_header_get_bgm_slider(),  SAVE_HEADER_BGM_DEFAULT);
    T_ASSERT_EQ_I(save_header_get_se_b_slider(), SAVE_HEADER_SE_B_DEFAULT);
    T_ASSERT_EQ_I(save_header_get_slider3(),     SAVE_HEADER_SLIDER3_DEFAULT);

    /* Hard-coded values matching engine: SE=9, BGM=5, SE_B=9, slider3=1. */
    T_ASSERT_EQ_I(save_header_get_se_slider(),   9);
    T_ASSERT_EQ_I(save_header_get_bgm_slider(),  5);
    T_ASSERT_EQ_I(save_header_get_se_b_slider(), 9);
    T_ASSERT_EQ_I(save_header_get_slider3(),     1);
    return 0;
}

int test_save_bank_init_all_stamps_all_banks(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    /* Every bank should now have live magic + valid checksum. */
    for (int idx = 0; idx < SAVE_BANK_COUNT; idx++) {
        uint32_t *bank = save_bank_dwords_at(idx);
        T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_MAGIC], SAVE_BANK_MAGIC);
        if (!save_bank_checksum_ok(idx)) {
            T_FAIL("bank %d checksum mismatch after init_all", idx);
        }
    }
    return 0;
}

int test_save_bank_init_all_is_idempotent(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    /* Snapshot one bank's checksum, run init_all again, and confirm
     * the snapshot is unchanged — init_all is a no-op when the
     * arena is already live and checksums verify. */
    uint32_t before = save_bank_dwords_at(7)[SAVE_BANK_FIELD_CHECKSUM];
    save_bank_init_all();
    uint32_t after = save_bank_dwords_at(7)[SAVE_BANK_FIELD_CHECKSUM];
    T_ASSERT_EQ_U(before, after);
    return 0;
}

/* ── init_one named field constants ── */

int test_save_bank_init_one_sets_money_and_objective(void)
{
    save_bank_arena_clear();
    seed_chara_minimal();
    rng_seed(1);
    save_bank_init_all();

    uint32_t *bank = save_bank_dwords_at(0);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_GOLD],           1000u);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_OBJECTIVE_GOLD], 1000u);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_WEEK_COUNTER],   7u);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_DAY_INDEX],      0u);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_RANK_THRESHOLD], 100u);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_MAGIC],          SAVE_BANK_MAGIC);

    /* The short at byte offset 0x10 should be 2. */
    uint16_t s = *(uint16_t *)((uint8_t *)bank + 0x10);
    T_ASSERT_EQ_U(s, 2u);
    return 0;
}

int test_save_bank_init_one_fills_item_slot_spans(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    uint32_t *bank = save_bank_dwords_at(0);

    /* First span: [6, 6+20000) all 0xFFFFFFFF. Spot-check head/mid/tail.
     * Note 6+20000 = 20006 = 0x4e26 — exact start of the second span,
     * so the two spans abut with no gap (a fact worth pinning). */
    T_ASSERT_EQ_U(bank[6],         0xffffffffu);
    T_ASSERT_EQ_U(bank[6 + 9999],  0xffffffffu);
    T_ASSERT_EQ_U(bank[6 + 19999], 0xffffffffu);
    T_ASSERT_EQ_U(bank[0x4e26],    0xffffffffu);  /* abuts first span */

    /* Just past the second span's end (0x4e26 + 300 = 0x4f52): in the
     * 100-record item-grid scratch, zero from memset. */
    T_ASSERT_EQ_U(bank[0x4f52],    0u);

    /* Third + fourth spans: spot-check first element of each. */
    T_ASSERT_EQ_U(bank[44999],    0xffffffffu);
    T_ASSERT_EQ_U(bank[0xb1e8],   0xffffffffu);
    return 0;
}

int test_save_bank_init_one_mini_block(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    uint32_t *bank = save_bank_dwords_at(0);

    /* ENGINE QUIRK: the named mini-block writes at bank[0xb388..0xb38d]
     * (constants 3,3,1,0,0,1) are DEAD WRITES — they get fully
     * overwritten by FUN_0048ffd9 (apply_starter_flag_pairs), which
     * spans bank[0xb384..0xb397] for chara index 0. So the final
     * values at 0xb388..0xb38d are the chara-0 flag-pairs[2..4]
     * (0,1, 9,1, 10,3), NOT the literal constants. Faithfully
     * reproduced.
     *
     * The lone surviving "constant" write is bank[0xaec6]=0 — well
     * outside the apply_starter_flag_pairs span. */
    T_ASSERT_EQ_U(bank[0xb388], 0u);   /* pair[2][0] */
    T_ASSERT_EQ_U(bank[0xb389], 1u);   /* pair[2][1] */
    T_ASSERT_EQ_U(bank[0xb38a], 9u);   /* pair[3][0] */
    T_ASSERT_EQ_U(bank[0xb38b], 1u);   /* pair[3][1] */
    T_ASSERT_EQ_U(bank[0xb38c], 10u);  /* pair[4][0] */
    T_ASSERT_EQ_U(bank[0xb38d], 3u);   /* pair[4][1] */
    T_ASSERT_EQ_U(bank[0xaec6], 0u);   /* mini-block constant survives */
    return 0;
}

/* ── Chara record init ── */

int test_save_bank_init_one_chara_records(void)
{
    save_bank_arena_clear();
    seed_chara_minimal();
    rng_seed(1);
    save_bank_init_all();

    uint32_t *bank = save_bank_dwords_at(0);
    uint32_t *rec0 = bank + SAVE_BANK_CHARA_BASE_DWORD;
    uint32_t *rec1 = bank + SAVE_BANK_CHARA_BASE_DWORD + SAVE_BANK_CHARA_STRIDE_DWORDS;

    /* ENGINE QUIRK: chara record dwords [1..10] are overlaid by
     * apply_starter_items, which writes encoded slot IDs (id<<6|0x20)
     * at puVar4[-5..-1] AND puVar4[0..4] where puVar4 = bank + 0x2cec8 +
     * chara*0x6c — i.e. 5 dwords BEFORE and 5 dwords AT puVar4.  Mapped
     * to the chara record (which starts at bank dword 0xb3ac = byte
     * 0x2ceb0), the writes hit rec[1..10] (= bank bytes 0x2ceb4..0x2ced8).
     * Original per-chara stat writes (rec[0xb..0x13]) survive — they
     * sit AFTER the starter-items write window.
     *
     * Recette (chara 0) — `*puVar1 = 0` overrides level_threshold. */
    T_ASSERT_EQ_I(rec0[0], 0);
    /* Per-chara presence byte at +0x60 (rec[0x18]) — survives. */
    T_ASSERT_EQ_I(((uint8_t *)(rec0 + 0x18))[0], 1);

    /* Final values at rec0[1..0xa] reflect apply_starter_items'
     * symmetric L/R write pattern.  STARTER_ITEMS[0] = {1, 1301, 1501,
     * 2301, -1}.  Encoded: (id<<6)|0x20 = 0x60 / 0x14560 / 0x17560 /
     * 0x23860 / 0xffffffff.  Each STARTER_ITEMS[0][N] is written to
     * rec[1+N] AND rec[6+N], with N from 0 (first inner iter) to 4
     * (last inner iter — emits the -1 sentinel). */
    T_ASSERT_EQ_U(rec0[1], (1u    << 6) | 0x20u);  /* L-write, slot 0 */
    T_ASSERT_EQ_U(rec0[2], (1301u << 6) | 0x20u);  /* L-write, slot 1 */
    T_ASSERT_EQ_U(rec0[3], (1501u << 6) | 0x20u);  /* L-write, slot 2 */
    T_ASSERT_EQ_U(rec0[4], (2301u << 6) | 0x20u);  /* L-write, slot 3 */
    T_ASSERT_EQ_U(rec0[5], 0xffffffffu);           /* L-write, slot 4 = -1 */
    T_ASSERT_EQ_U(rec0[6], (1u    << 6) | 0x20u);  /* R-write, slot 0 */
    T_ASSERT_EQ_U(rec0[7], (1301u << 6) | 0x20u);  /* R-write, slot 1 */
    T_ASSERT_EQ_U(rec0[8], (1501u << 6) | 0x20u);  /* R-write, slot 2 */
    T_ASSERT_EQ_U(rec0[9], (2301u << 6) | 0x20u);  /* R-write, slot 3 */
    T_ASSERT_EQ_U(rec0[0xa], 0xffffffffu);         /* R-write, slot 4 = -1 */

    /* Chara 1 — level_threshold preserved (NOT overridden — only
     * chara 0 gets the `*puVar1 = 0;` zeroing). */
    T_ASSERT_EQ_I(rec1[0], 5);
    /* Chara 1 also gets the symmetric L/R writes; first entry =
     * STARTER_ITEMS[1][0] = 101. */
    T_ASSERT_EQ_U(rec1[1], (101u << 6) | 0x20u);
    T_ASSERT_EQ_U(rec1[6], (101u << 6) | 0x20u);
    T_ASSERT_EQ_I(((uint8_t *)(rec1 + 0x18))[0], 1);
    return 0;
}

int test_save_bank_init_one_mirrors_starter_items_into_chara_equip(void)
{
    /* apply_starter_items writes into both save_bank arena (faithfully)
     * AND chara_equip storage (mirror, since the port has the two
     * arenas in SEPARATE memory while the engine overlaps them).
     * Without the mirror, slot A[0] would stay BSS-zero and the stat
     * aggregator would fire items_find_by_id 5× instead of retail's 4×.
     *
     * For chara 0, STARTER_ITEMS[0] = {1, 1301, 1501, 2301, -1}.
     * The symmetric L/R write pattern places STARTER_ITEMS[0][4] = -1
     * at chara_equip[0].slot_A[0] (via the L write of the last inner
     * iter, which lands at puVar4 - 0x14 + 4 = chara_equip[0] + 4).
     * Slot A[1..4] hold STARTER_ITEMS[0][0..3] encoded.  Slot B[0] also
     * holds the sentinel.  Level field gets stomped with the encoded
     * STARTER_ITEMS[0][3] = 2301. */
    save_bank_arena_clear();
    chara_equip_reset_for_test();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_one(0);

    /* Slot A[0] = sentinel — the chip's whole point. */
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 0), 0xffffffffu);
    /* Slot A[1..4] = STARTER_ITEMS[0][0..3] encoded. */
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 1), (1u    << 6) | 0x20u);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 2), (1301u << 6) | 0x20u);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 3), (1501u << 6) | 0x20u);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 4), (2301u << 6) | 0x20u);
    /* Slot B[0] (record byte +0x18) = sentinel from the same final iter. */
    T_ASSERT_EQ_U(chara_equip_get_record_dword(0, 0, 0x18), 0xffffffffu);
    /* Level field (record byte +0) gets stomped with encoded
     * STARTER_ITEMS[0][3] = 2301 (via the L write of iter 4). */
    T_ASSERT_EQ_U(chara_equip_get_record_dword(0, 0, 0x00),
                  (2301u << 6) | 0x20u);

    /* Chara 7 (last) — same pattern with STARTER_ITEMS[7] =
     * {701, 1201, 1601, -1, -1}.  Slot A[0] still the sentinel
     * (STARTER_ITEMS[7][4] = -1).  Slot A[4] is ALSO a sentinel
     * (STARTER_ITEMS[7][3] = -1 lands at slot A[4]). */
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 7, 0), 0xffffffffu);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 7, 1), (701u  << 6) | 0x20u);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 7, 4), 0xffffffffu);
    return 0;
}

int test_save_bank_init_one_consumes_8_rng_steps(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    /* init_all calls init_one on 100 banks, each consuming 8 RNG
     * draws (one per chara). 800 draws total. Capture the RNG state
     * before and after. */
    uint16_t baseline = 0;
    {
        /* Pre-draw 800 from the same initial seed, to compute the
         * expected post-state. */
        rng_seed(1);
        for (int i = 0; i < 800; i++) baseline = rng_next15();
    }
    rng_seed(1);
    save_bank_init_all();
    uint16_t after_init = rng_next15();
    /* The 801st draw after seed 1 == one draw past 800-draw baseline. */
    rng_seed(1);
    uint16_t skip_baseline = 0;
    for (int i = 0; i < 800; i++) skip_baseline = rng_next15();
    (void)baseline;
    uint16_t expected = rng_next15();
    T_ASSERT_EQ_U(after_init, expected);
    (void)skip_baseline;
    return 0;
}

/* ── Checksum ── */

int test_save_bank_checksum_detects_tamper(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    /* Live bank — checksum should match. */
    T_ASSERT(save_bank_checksum_ok(3));

    /* Tamper with one byte and re-verify — should fail. */
    uint32_t *bank = save_bank_dwords_at(3);
    bank[SAVE_BANK_FIELD_GOLD] = 99999;
    T_ASSERT(!save_bank_checksum_ok(3));

    /* save_bank_init_all re-runs init_one for any failing bank. */
    save_bank_init_all();
    T_ASSERT(save_bank_checksum_ok(3));
    T_ASSERT_EQ_U(save_bank_dwords_at(3)[SAVE_BANK_FIELD_GOLD], 1000u);
    return 0;
}

/* The skip-verify gate (engine DAT_095d3728): with it set, init_all must
 * NOT re-init a bank whose stored checksum is stale — retail preserves
 * LOADED banks even when their checksum is 0/mismatched. This is the
 * behaviour the save pillar's ranking_records FAIL came down to. */
int test_save_bank_skip_verify_preserves_stale_bank(void)
{
    save_bank_arena_clear();          /* resets the gate to 0 */
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();
    T_ASSERT_EQ_I(save_bank_get_skip_verify(), 0);

    /* Make bank 7 look like a loaded-but-never-committed slot: a real
     * value plus a deliberately STALE checksum (0), like the seed's
     * non-active banks. */
    uint32_t *bank = save_bank_dwords_at(7);
    bank[SAVE_BANK_FIELD_GOLD]     = 4242;
    bank[SAVE_BANK_FIELD_CHECKSUM] = 0;
    T_ASSERT(!save_bank_checksum_ok(7));

    /* Gate ON → the sweep is skipped → the stale bank is PRESERVED. */
    save_bank_set_skip_verify(1);
    save_bank_init_all();
    T_ASSERT_EQ_U(save_bank_dwords_at(7)[SAVE_BANK_FIELD_GOLD], 4242u);
    T_ASSERT_EQ_U(save_bank_dwords_at(7)[SAVE_BANK_FIELD_CHECKSUM], 0u);

    /* Gate OFF → the sweep runs again → the stale bank is re-inited. */
    save_bank_set_skip_verify(0);
    save_bank_init_all();
    T_ASSERT_EQ_U(save_bank_dwords_at(7)[SAVE_BANK_FIELD_GOLD], 1000u);
    T_ASSERT(save_bank_checksum_ok(7));
    return 0;
}

/* ── Header init hook ── */

static int g_hook_call_count = 0;
static void test_hook(void) { g_hook_call_count++; }

int test_save_bank_header_init_hook_fires_once_per_reset(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    g_hook_call_count = 0;
    save_bank_set_header_init_hook(test_hook);

    save_bank_init_all();
    T_ASSERT_EQ_I(g_hook_call_count, 1);

    /* Second call: header magic is already live, so hook should NOT
     * fire again. */
    save_bank_init_all();
    T_ASSERT_EQ_I(g_hook_call_count, 1);

    save_bank_set_header_init_hook(NULL);
    return 0;
}

/* ── Slider setters ── */

int test_save_bank_header_slider_setters_clamp(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    save_header_set_se_slider(11);
    T_ASSERT_EQ_I(save_header_get_se_slider(), 9);
    save_header_set_se_slider(-3);
    T_ASSERT_EQ_I(save_header_get_se_slider(), 0);
    save_header_set_se_slider(5);
    T_ASSERT_EQ_I(save_header_get_se_slider(), 5);

    save_header_set_slider3(7);
    T_ASSERT_EQ_I(save_header_get_slider3(), 2);
    save_header_set_slider3(-1);
    T_ASSERT_EQ_I(save_header_get_slider3(), 0);
    return 0;
}
