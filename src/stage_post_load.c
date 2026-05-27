/*
 * stage_post_load.c — see stage_post_load.h for the chip writeup.
 *
 * Engine: FUN_00435c98 @ 0x435c98 (309 B), with stubs for FUN_004844ef
 * (stat aggregator), FUN_004360b6 (post-init sibling), FUN_00435fbb,
 * FUN_00435dcd.
 */

#include "stage_post_load.h"

#include <string.h>

#include "call_trace.h"
#include "stage_gate.h"
#include "xp_curve.h"

/* ─── per-chara record storage ─────────────────────────────────────────── */

static int32_t g_stage_chara[STAGE_POST_LOAD_CHARA_COUNT]
                            [STAGE_POST_LOAD_CHARA_DWORDS];

/* ─── scratch globals (engine VAs preserved in names) ──────────────────── */

static int32_t g_dat_056da1cc;
static int32_t g_dat_056da1d0;
static int32_t g_dat_056da1d4;
static int32_t g_dat_056db0d8;
static float   g_dat_056db0bc;
static float   g_dat_056db0c0;
static float   g_dat_056db0c4;
static float   g_dat_056db0c8;
static int32_t g_dat_0438bea0;
static float   g_dat_0438b91c;
static int32_t g_dat_0438b4c4;
static int32_t g_dat_056dae44[6];
static int32_t g_dat_0438b4ec[25];
static int32_t g_dat_0438bedc[6];
static int32_t g_dat_0438bef4[6];
static int32_t g_dat_0438bf0c[6];
static int32_t g_dat_0438bf24[6];

/* ─── sub-call stubs (probes only, body deferred) ──────────────────────── */

/* FUN_004844ef @ 0x4844ef — chara stat aggregator.
 * Reads g_chara + per-chara item slots, distributes equip stats into
 * scratch sum at DAT_056db0ac (consumed by combat damage calc).
 * Not on the HOUSE-entry visible path → defer until equipped-item
 * subsystem ports. */
static void stage_post_load_stat_aggregator_stub(void)
{
    CALL_TRACE_ENTER_STUB(0x4844efu);
}

/* FUN_004360b6 @ 0x4360b6 — post-init sibling (202 B).
 * Writes the same DAT_0438bXXX scratch arrays this function zeros;
 * body deferred. */
static void stage_post_load_post_init_sibling_stub(void)
{
    CALL_TRACE_ENTER_STUB(0x4360b6u);
}

/* FUN_00435fbb @ 0x435fbb — stage-init sibling (224 B).
 *
 * Decomp at all.c L33255-33301.  Drives a 5-element float scratch
 * array (DAT_0438bef4[0..4]) from a per-index counter (DAT_0438bf24[0..4])
 * via threshold/slope/clamp arithmetic against five 5-element RDATA
 * tables.  For indices 2-4 a stage_gate_query() pass triggers a
 * fixed-value override.
 *
 * Two-arg signature:
 *   param_1: 1 resets DAT_0438bef4[0..4] AND DAT_0438bf24[0..4] before
 *            the main loop; 0 skips the reset.  FUN_00435c98 always
 *            passes 1; other callers (dungeon scenes) pass 0.
 *   param_2: when >= 0, additionally clears DAT_0438bf24[param_2]
 *            (forces that specific counter to 0 before the loop).
 *
 * Engine RDATA (5 entries each, in counter-index order):
 *   DAT_005c4ffc int32  : counter threshold       = {0, 3, 0, 0, 2}
 *   DAT_005c5010 float  : low clamp / pre offset  = {0.0, 0.0, 0.02, 0.008, 0.0}
 *   DAT_005c5024 float  : slope                   = {0.002, 0.0005, 0.04, 0.002, 0.0005}
 *   DAT_005c5038 float  : high clamp              = {0.1, 0.1, 0.2, 0.05, 0.01}
 *   DAT_005c504c float  : gate-override value     = {0.1, 0.1, 0.3, 0.1, 0.05}
 *
 * Per-index pipeline:
 *   1. Write 0 into bef4[i].
 *   2. If counter[i] > threshold[i]:
 *        pre = (counter[i] - threshold[i]) * slope[i] + low[i]
 *        bef4[i] = pre
 *   3. Clamp bef4[i] to >= low[i].
 *   4. Clamp bef4[i] to <= high[i].
 *   5. For i in {2, 3, 4}: if stage_gate_query() != 0,
 *      bef4[i] = override[i].
 *
 * After the loop, all five counters are incremented by 1.
 *
 * Storage note: bef4[] holds float bit-patterns but our backing store
 * is int32_t (shared with FUN_00435c98 zeroing); memcpy mediates the
 * float view to keep strict-aliasing clean. */
static const int32_t kFun435fbbThresholds[5] = { 0, 3, 0, 0, 2 };
static const float   kFun435fbbLows[5]       = { 0.0f, 0.0f, 0.02f, 0.008f, 0.0f };
static const float   kFun435fbbSlopes[5]     = { 0.002f, 0.0005f, 0.04f, 0.002f, 0.0005f };
static const float   kFun435fbbHighs[5]      = { 0.1f, 0.1f, 0.2f, 0.05f, 0.01f };
static const float   kFun435fbbOverrides[5]  = { 0.1f, 0.1f, 0.3f, 0.1f, 0.05f };

void stage_post_load_pulse_5fold(int param_1, int param_2)
{
    CALL_TRACE_ENTER(0x435fbbu);

    /* L33264-33271: param_1 == 1 zeros bef4[0..4] and bf24[0..4]. */
    if (param_1 == 1) {
        for (int i = 0; i < 5; i++) {
            g_dat_0438bef4[i] = 0;
            g_dat_0438bf24[i] = 0;
        }
    }

    /* L33272-33274: zero a specific counter slot.  Engine guards on
     * `-1 < param_2`; we additionally bound-check to keep the index
     * in-range under any caller. */
    if (param_2 >= 0 && param_2 < 5) {
        g_dat_0438bf24[param_2] = 0;
    }

    /* L33275-33295: per-index pipeline. */
    for (int i = 0; i < 5; i++) {
        int32_t counter = g_dat_0438bf24[i];

        /* Default to 0 (int 0 == float 0.0f bit-pattern). */
        g_dat_0438bef4[i] = 0;

        /* Threshold-driven write. */
        if (kFun435fbbThresholds[i] < counter) {
            float pre = (float)(counter - kFun435fbbThresholds[i])
                      * kFun435fbbSlopes[i] + kFun435fbbLows[i];
            memcpy(&g_dat_0438bef4[i], &pre, sizeof(float));
        }

        /* Clamp to >= low. */
        float current;
        memcpy(&current, &g_dat_0438bef4[i], sizeof(float));
        if (current < kFun435fbbLows[i]) {
            memcpy(&g_dat_0438bef4[i], &kFun435fbbLows[i], sizeof(float));
        }

        /* Clamp to <= high (re-read after the low clamp). */
        memcpy(&current, &g_dat_0438bef4[i], sizeof(float));
        if (kFun435fbbHighs[i] < current) {
            memcpy(&g_dat_0438bef4[i], &kFun435fbbHighs[i], sizeof(float));
        }

        /* Gate-override at indices 2..4 (engine: `if (7 < iVar4)` where
         * iVar4 is the byte offset 0/4/8/12/16, equivalent to i >= 2). */
        if (i >= 2) {
            if (stage_gate_query() != 0) {
                memcpy(&g_dat_0438bef4[i], &kFun435fbbOverrides[i], sizeof(float));
            }
        }
    }

    /* L33296-33300: counter += 1 for all five indices. */
    for (int i = 0; i < 5; i++) {
        g_dat_0438bf24[i] += 1;
    }
}

/* FUN_00435dcd @ 0x435dcd — stage-init sibling (494 B, heaviest of
 * the three).  Body is mostly scratch-float arithmetic on
 * DAT_0438bedc + per-stage RDATA at DAT_005c4fcc.  Deferred. */
static void stage_post_load_sibling_435dcd_stub(int param_1, int param_2)
{
    (void)param_1; (void)param_2;
    CALL_TRACE_ENTER_STUB(0x435dcdu);
}

/* ─── chara record accessors ──────────────────────────────────────────── */

int32_t stage_post_load_chara_field(int chara_idx, int dword_idx)
{
    if (chara_idx < 0 || chara_idx >= STAGE_POST_LOAD_CHARA_COUNT) return 0;
    if (dword_idx < 0 || dword_idx >= STAGE_POST_LOAD_CHARA_DWORDS) return 0;
    return g_stage_chara[chara_idx][dword_idx];
}

void stage_post_load_set_chara_field(int chara_idx, int dword_idx, int32_t value)
{
    if (chara_idx < 0 || chara_idx >= STAGE_POST_LOAD_CHARA_COUNT) return;
    if (dword_idx < 0 || dword_idx >= STAGE_POST_LOAD_CHARA_DWORDS) return;
    g_stage_chara[chara_idx][dword_idx] = value;
}

int16_t stage_post_load_get_chara_short(int chara_idx, int byte_off)
{
    if (chara_idx < 0 || chara_idx >= STAGE_POST_LOAD_CHARA_COUNT) return 0;
    if (byte_off < 0 || byte_off + (int)sizeof(int16_t) > STAGE_POST_LOAD_CHARA_BYTES) return 0;
    const uint8_t *p = (const uint8_t *)g_stage_chara[chara_idx] + byte_off;
    int16_t out;
    memcpy(&out, p, sizeof(int16_t));
    return out;
}

void stage_post_load_set_chara_short(int chara_idx, int byte_off, int16_t value)
{
    if (chara_idx < 0 || chara_idx >= STAGE_POST_LOAD_CHARA_COUNT) return;
    if (byte_off < 0 || byte_off + (int)sizeof(int16_t) > STAGE_POST_LOAD_CHARA_BYTES) return;
    uint8_t *p = (uint8_t *)g_stage_chara[chara_idx] + byte_off;
    memcpy(p, &value, sizeof(int16_t));
}

/* ─── scratch global accessors ────────────────────────────────────────── */

int32_t stage_post_load_get_dat_056da1cc(void) { return g_dat_056da1cc; }
int32_t stage_post_load_get_dat_056da1d0(void) { return g_dat_056da1d0; }
int32_t stage_post_load_get_dat_056da1d4(void) { return g_dat_056da1d4; }
int32_t stage_post_load_get_dat_056db0d8(void) { return g_dat_056db0d8; }
float   stage_post_load_get_dat_056db0bc(void) { return g_dat_056db0bc; }
float   stage_post_load_get_dat_056db0c0(void) { return g_dat_056db0c0; }
float   stage_post_load_get_dat_056db0c4(void) { return g_dat_056db0c4; }
float   stage_post_load_get_dat_056db0c8(void) { return g_dat_056db0c8; }
int32_t stage_post_load_get_dat_0438bea0(void) { return g_dat_0438bea0; }
float   stage_post_load_get_dat_0438b91c(void) { return g_dat_0438b91c; }
int32_t stage_post_load_get_dat_0438b4c4(void) { return g_dat_0438b4c4; }

int32_t stage_post_load_get_dat_056dae44(int idx)
{
    return (idx < 0 || idx >= 6) ? 0 : g_dat_056dae44[idx];
}
int32_t stage_post_load_get_dat_0438b4ec(int idx)
{
    return (idx < 0 || idx >= 25) ? 0 : g_dat_0438b4ec[idx];
}
int32_t stage_post_load_get_dat_0438bedc(int idx)
{
    return (idx < 0 || idx >= 6) ? 0 : g_dat_0438bedc[idx];
}
int32_t stage_post_load_get_dat_0438bef4(int idx)
{
    return (idx < 0 || idx >= 6) ? 0 : g_dat_0438bef4[idx];
}
int32_t stage_post_load_get_dat_0438bf0c(int idx)
{
    return (idx < 0 || idx >= 6) ? 0 : g_dat_0438bf0c[idx];
}
int32_t stage_post_load_get_dat_0438bf24(int idx)
{
    return (idx < 0 || idx >= 6) ? 0 : g_dat_0438bf24[idx];
}

float stage_post_load_get_dat_0438bef4_as_float(int idx)
{
    if (idx < 0 || idx >= 6) return 0.0f;
    float out;
    memcpy(&out, &g_dat_0438bef4[idx], sizeof(out));
    return out;
}

void stage_post_load_reset_for_test(void)
{
    memset(g_stage_chara, 0, sizeof(g_stage_chara));
    g_dat_056da1cc = 0;
    g_dat_056da1d0 = 0;
    g_dat_056da1d4 = 0;
    g_dat_056db0d8 = 0;
    g_dat_056db0bc = 0.0f;
    g_dat_056db0c0 = 0.0f;
    g_dat_056db0c4 = 0.0f;
    g_dat_056db0c8 = 0.0f;
    g_dat_0438bea0 = 0;
    g_dat_0438b91c = 0.0f;
    g_dat_0438b4c4 = 0;
    memset(g_dat_056dae44, 0, sizeof(g_dat_056dae44));
    memset(g_dat_0438b4ec, 0, sizeof(g_dat_0438b4ec));
    memset(g_dat_0438bedc, 0, sizeof(g_dat_0438bedc));
    memset(g_dat_0438bef4, 0, sizeof(g_dat_0438bef4));
    memset(g_dat_0438bf0c, 0, sizeof(g_dat_0438bf0c));
    memset(g_dat_0438bf24, 0, sizeof(g_dat_0438bf24));
}

/* ─── FUN_00435c98 body ──────────────────────────────────────────────── */

void stage_post_load_init(void)
{
    /* E.3 probe — FUN_00435c98 @ 0x435c98 (post-new-game stage init). */
    CALL_TRACE_ENTER(0x435c98u);

    /* Engine L33111-13: iVar4 = DAT_0438b1e0 (stage), iVar5 = stage *
     * 0x2dfc8 (record offset), piVar2 = &record[+0x2ceb0] +
     * DAT_0438b7d8 * 0x6c (active chara record).  Both BSS-zero on
     * NEW GAME → stage 0, chara 0. */
    int32_t *chara = g_stage_chara[0];

    /* L33114: stat aggregator (stub). */
    stage_post_load_stat_aggregator_stub();

    /* L33115-33117: scratch tag triple (0, 3, 1). */
    g_dat_056da1cc = 0;
    g_dat_056da1d0 = 3;
    g_dat_056da1d4 = 1;

    /* L33118: FUN_0047a8c0(stage_record) — apply_chara_interp on the
     * stage record's chara block.  OMITTED: see stage_post_load.h
     * "apply_chara_interp note" for the call-count divergence write-up. */

    /* L33119-33124: position carry-forward.
     *   Engine: sum the low int16 of piVar2[0xf] (chara bytes 0x3c..0x3d)
     *   with the int16 at bytes 0x3e..0x3f, both sign-extended.  Mirror
     *   for Y at 0x40 / 0x42.  Then copy DAT_056db0bc/c0 → DAT_056db0c4/c8
     *   as the "previous position" backup pair.
     *   Use memcpy to avoid strict-aliasing UB on the int16 reads. */
    g_dat_056db0d8 = 0;
    {
        int16_t pos_x_lo, pos_x_hi;
        const uint8_t *p = (const uint8_t *)chara;
        memcpy(&pos_x_lo, p + 0x3c, sizeof(pos_x_lo));
        memcpy(&pos_x_hi, p + 0x3e, sizeof(pos_x_hi));
        g_dat_056db0bc = (float)((int32_t)pos_x_lo + (int32_t)pos_x_hi);
    }
    g_dat_0438bea0 = 0;
    {
        int16_t pos_y_lo, pos_y_hi;
        const uint8_t *p = (const uint8_t *)chara;
        memcpy(&pos_y_lo, p + 0x40, sizeof(pos_y_lo));
        memcpy(&pos_y_hi, p + 0x42, sizeof(pos_y_hi));
        g_dat_056db0c0 = (float)((int32_t)pos_y_lo + (int32_t)pos_y_hi);
    }
    g_dat_056db0c4 = g_dat_056db0bc;
    g_dat_056db0c8 = g_dat_056db0c0;

    /* L33125-33128: XP threshold writes via xp_curve.
     *   chara[0]    = level
     *   chara[0x12] = xp_curve(level)
     *   chara[0x13] = xp_curve(level + 1) */
    int32_t level = chara[0];
    chara[0x12] = xp_curve_threshold(level);
    chara[0x13] = xp_curve_threshold(level + 1);

    /* L33129-33135: clamp chara[0x11] (xp_value) into [xp_curr, xp_next].
     * Engine writes via a piVar1 alias of piVar2 + 0x11. */
    if (chara[0x11] < chara[0x12]) {
        chara[0x11] = chara[0x12];
    }
    if (chara[0x13] < chara[0x11]) {
        chara[0x11] = chara[0x13];
    }

    /* L33136: DAT_0438b91c = (float)(int)stage_record[+0x2c3f4].
     * Stage record isn't ported; default to 0 for stage 0. */
    g_dat_0438b91c = 0.0f;

    /* L33137: post-init sibling (stub). */
    stage_post_load_post_init_sibling_stub();

    /* L33138-33142: 6 dwords at DAT_056dae44 → -1. */
    for (int i = 0; i < 6; i++) {
        g_dat_056dae44[i] = -1;
    }

    /* L33143-33147: 25 dwords at DAT_0438b4ec → 0. */
    for (int i = 0; i < 25; i++) {
        g_dat_0438b4ec[i] = 0;
    }

    /* L33148: DAT_0438b4c4 = 1. */
    g_dat_0438b4c4 = 1;

    /* L33149-33156: 4-row × 6-col grid zero.  Engine uses a single
     * pointer walk over &DAT_0438bef4, indexing the 4 rows via
     * fixed dword offsets (-6, 0, +6, +12).  Equivalent to four
     * parallel for-loops over 6 dwords each. */
    for (int i = 0; i < 6; i++) {
        g_dat_0438bedc[i] = 0;
        g_dat_0438bef4[i] = 0;
        g_dat_0438bf0c[i] = 0;
        g_dat_0438bf24[i] = 0;
    }

    /* L33157-33158: siblings (435fbb full body, 435dcd still STUB).
     * Engine passes (1, 0) to both. */
    stage_post_load_pulse_5fold(1, 0);
    stage_post_load_sibling_435dcd_stub(1, 0);
}
