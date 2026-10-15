/*
 * stage_post_load.c — see stage_post_load.h for the chip writeup.
 *
 * Engine: FUN_00435c98 @ 0x435c98 (309 B).  Stat aggregator
 * FUN_004844ef lives in chara_equip.c (full body); FUN_004360b6 lives
 * in chara_skills.c (full body); FUN_00435fbb and FUN_00435dcd have
 * full bodies below.
 */

#include "stage_post_load.h"

#include <string.h>

#include "call_trace.h"
#include "chara_equip.h"
#include "chara_skills.h"
#include "rng.h"
#include "save_bank.h"
#include "scene1_combat_sm.h"   /* g_scene1_combat_stage_id (DAT_0438b4c8) */
#include "stage_gate.h"
#include "xp_curve.h"
#include "dungeon_flow.h"
#include "party.h"
#include "save_work.h"
#include "dungeon_party.h"   /* FUN_00485712 */
#include "x87.h"

/* ─── per-chara record storage ─────────────────────────────────────────── */

/* The chara records live in the active work record: +0x2ceb0 + 0x6c·chara
 * (DAT_04510648 + DAT_0438b7d8·0x6c). */
static int32_t *chara_rec(int idx)
{
    uint8_t *w = save_work_bank_at(save_work_active_slot());
    return w ? (int32_t *)(void *)(w + 0x2ceb0 + STAGE_POST_LOAD_CHARA_BYTES * idx) : NULL;
}

/* ─── scratch globals (engine VAs preserved in names) ──────────────────── */

/* the party block (party.h) */
#define g_dat_056da1cc (g_party.chr[0])
#define g_dat_056da1d0 (g_party.chr[1])
#define g_dat_056da1d4 (g_party.chr[2])
#define g_dat_056db0d8 (PW(0x56db0d8u).i)
#define g_dat_056db0bc (PW(0x56db0bcu).f)   /* HP */
#define g_dat_056db0c0 (PW(0x56db0c0u).f)   /* SP */
#define g_dat_056db0c4 (PW(0x56db0c4u).f)   /* the HP gauge's shown value */
#define g_dat_056db0c8 (PW(0x56db0c8u).f)   /* the SP gauge's shown value */
#define g_dat_0438bea0 g_equip_prompt       /* dungeon_flow.h */
static float   g_dat_0438b91c;
static int32_t g_dat_0438b4c4;
#define g_dat_056dae44 (&PW(0x56dae44u).i)   /* [6] */
static int32_t g_dat_0438b4ec[25];
static int32_t g_dat_0438bedc[6];
static int32_t g_dat_0438bef4[6];
static int32_t g_dat_0438bf0c[6];
static int32_t g_dat_0438bf24[6];

/* DAT_0438b4d0 — primary mode selector for stage_post_load_pulse_first_row
 * (FUN_00435dcd's iVar3 dispatcher).  BSS-zero default = mode 0.  Engine
 * writes -1 on dungeon-stage entry (decomp L55490) and { 1, 2, 3, 4 }
 * via the dungeon-section commit path.  No port writer yet — the future
 * stage-change chip wires this through. */
static int32_t g_dat_0438b4d0;

/* ─── sub-call stubs (probes only, body deferred) ──────────────────────── */

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

/* FUN_00435dcd: the treasure-rarity weights DAT_0438bedc[6] (the chest roll
 * FUN_0043824b and the drops read them) from the per-tier counters
 * DAT_0438bf0c[6].  `reset` 1 zeroes both rows; `clear` >= 0 zeroes one
 * weight.  The mode is DAT_0438b4d0, or with -1 the dungeon DAT_0438b4c8
 * (dungeon 5 from floor 30 counts as 4).  Then every counter steps 1 — 5 when
 * a tick-time call (reset 0) rolls rand % 10 == 0 and the accessory 2509
 * procs (FUN_00485712). */
void stage_post_load_pulse_first_row(int reset_arrays, int force_clear_idx)
{
    CALL_TRACE_ENTER(0x435dcdu);
    /* .data 0x5c4fcc + 8·mode / 0x5c4fd0 + 8·mode: the caps of the mode's
     * second / third weight (modes 2..5 read (0.2, 0.05)) */
    static const uint32_t k_cap_a[6] = { 0x00000001u, 0x41600000u, 0x3e4ccccdu, 0x3e4ccccdu, 0x3e4ccccdu, 0x3e4ccccdu };
    static const uint32_t k_cap_b[6] = { 0xbfe66666u, 0x41a80000u, 0x3d4ccccdu, 0x3d4ccccdu, 0x3d4ccccdu, 0x3d4ccccdu };
    static const float k0p02 = 0.019999999552965164f, k0p2 = 0.20000000298023224f;
    static const float k0p04 = 0.03999999910593033f, k0p005 = 0.004999999888241291f;
    float w[6];
    memcpy(w, g_dat_0438bedc, sizeof w);
    if (reset_arrays == 1) {
        for (int i = 0; i < 6; i++) {
            g_dat_0438bf0c[i] = 0;
            w[i] = 0.0f;
        }
    }
    if (force_clear_idx >= 0 && force_clear_idx < 6)
        w[force_clear_idx] = 0.0f;
    int mode = g_dat_0438b4d0;
    if (mode == -1) {
        mode = g_scene1_combat_stage_id;
        if (mode == 5 && stage_gate_get_next() >= 30) mode = 4;
    }
    if (mode == 0) {
        w[0] = 1.0f;
    } else if (mode == 1) {
        w[1] = 0.0f;
        if (g_dat_0438bf0c[1] > 2) {
            w[1] = spill32((long double)spill32((long double)(g_dat_0438bf0c[1] - 2)) * k0p02);
            if (w[1] > k0p2) w[1] = k0p2;
        }
        w[0] = 1.0f;
        for (int i = 1; i < 5; i++) w[0] = spill32((long double)w[0] - w[i]);
    } else {
        int k = 2;
        if (mode == 2) k = 0;
        else if (mode == 3) k = 1;
        else if (mode == 5) k = 3;
        w[k] = 1.0f;
        w[k + 1] = k0p04;
        if (g_dat_0438bf0c[k + 1] > 0)
            w[k + 1] = spill32((long double)spill32((long double)g_dat_0438bf0c[k + 1]) * k0p04);
        /* PORT-DEBT(stub, FUN_00435dcd): modes outside 0..5 read the caps past
         * the table in .data — no stage has one */
        float ca = 0.0f, cb = 0.0f;
        if (mode >= 0 && mode < 6) {
            memcpy(&ca, &k_cap_a[mode], 4);
            memcpy(&cb, &k_cap_b[mode], 4);
        }
        if (w[k + 1] > ca) w[k + 1] = ca;
        w[k + 2] = 0.0f;
        if (g_dat_0438bf0c[k + 2] > 3)
            w[k + 2] = spill32((long double)spill32((long double)(g_dat_0438bf0c[k + 2] - 3)) * k0p005);
        if (w[k + 2] > cb) w[k + 2] = cb;
        w[k] = spill32((long double)1.0f - w[k + 1] - w[k + 2]);
    }
    memcpy(g_dat_0438bedc, w, sizeof w);
    int32_t step = 1;
    if (reset_arrays == 0 && rng_next15() % 10u == 0 && party_accessory_proc(2509, 3, 30) != 0)
        step = 5;
    for (int i = 0; i < 6; i++) g_dat_0438bf0c[i] += step;
}

/* the drop-weight rows DAT_0438bedc / bef4 / bf0c / bf24 by retail VA
 * (state_va: portstate --va) */
const int32_t *stage_post_load_va(uint32_t va)
{
    if (va % 4 || va < 0x438bedcu || va >= 0x438bf3cu) return NULL;
    const uint32_t k = (va - 0x438bedcu) / 4;
    int32_t *rows[4] = { g_dat_0438bedc, g_dat_0438bef4, g_dat_0438bf0c, g_dat_0438bf24 };
    return &rows[k / 6][k % 6];
}

/* ─── chara record accessors ──────────────────────────────────────────── */

int32_t stage_post_load_chara_field(int chara_idx, int dword_idx)
{
    if (chara_idx < 0 || chara_idx >= STAGE_POST_LOAD_CHARA_COUNT) return 0;
    if (dword_idx < 0 || dword_idx >= STAGE_POST_LOAD_CHARA_DWORDS) return 0;
    const int32_t *c = chara_rec(chara_idx);
    return c ? c[dword_idx] : 0;
}

void stage_post_load_set_chara_field(int chara_idx, int dword_idx, int32_t value)
{
    if (chara_idx < 0 || chara_idx >= STAGE_POST_LOAD_CHARA_COUNT) return;
    if (dword_idx < 0 || dword_idx >= STAGE_POST_LOAD_CHARA_DWORDS) return;
    int32_t *c = chara_rec(chara_idx);
    if (c) c[dword_idx] = value;
}

int16_t stage_post_load_get_chara_short(int chara_idx, int byte_off)
{
    if (chara_idx < 0 || chara_idx >= STAGE_POST_LOAD_CHARA_COUNT) return 0;
    if (byte_off < 0 || byte_off + (int)sizeof(int16_t) > STAGE_POST_LOAD_CHARA_BYTES) return 0;
    const int32_t *c = chara_rec(chara_idx);
    if (!c) return 0;
    int16_t out;
    memcpy(&out, (const uint8_t *)c + byte_off, sizeof(int16_t));
    return out;
}

void stage_post_load_set_chara_short(int chara_idx, int byte_off, int16_t value)
{
    if (chara_idx < 0 || chara_idx >= STAGE_POST_LOAD_CHARA_COUNT) return;
    if (byte_off < 0 || byte_off + (int)sizeof(int16_t) > STAGE_POST_LOAD_CHARA_BYTES) return;
    int32_t *c = chara_rec(chara_idx);
    if (c) memcpy((uint8_t *)c + byte_off, &value, sizeof(int16_t));
}

/* ─── scratch global accessors ────────────────────────────────────────── */

int32_t stage_post_load_get_dat_056da1cc(void) { return g_dat_056da1cc; }
int32_t stage_post_load_get_dat_056da1d0(void) { return g_dat_056da1d0; }
int32_t stage_post_load_get_dat_056da1d4(void) { return g_dat_056da1d4; }
int32_t stage_post_load_get_dat_056db0d8(void) { return g_dat_056db0d8; }
float   stage_post_load_get_dat_056db0bc(void) { return g_dat_056db0bc; }
void    stage_post_load_set_dat_056db0bc(float v) { g_dat_056db0bc = v; }
float   stage_post_load_get_dat_056db0c0(void) { return g_dat_056db0c0; }
float   stage_post_load_get_dat_056db0c4(void) { return g_dat_056db0c4; }
float   stage_post_load_get_dat_056db0c8(void) { return g_dat_056db0c8; }
int32_t stage_post_load_get_dat_0438bea0(void) { return g_dat_0438bea0; }
float   stage_post_load_get_dat_0438b91c(void) { return g_dat_0438b91c; }
int32_t stage_post_load_get_dat_0438b4c4(void) { return g_dat_0438b4c4; }
void    stage_post_load_set_dat_0438b4c4(int32_t v) { g_dat_0438b4c4 = v; }

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
    for (int k = 0; k < STAGE_POST_LOAD_CHARA_COUNT; k++) {
        int32_t *c = chara_rec(k);
        if (c) memset(c, 0, STAGE_POST_LOAD_CHARA_BYTES);
    }
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
    memset(g_dat_056dae44, 0, 6 * sizeof(int32_t));
    memset(g_dat_0438b4ec, 0, sizeof(g_dat_0438b4ec));
    memset(g_dat_0438bedc, 0, sizeof(g_dat_0438bedc));
    memset(g_dat_0438bef4, 0, sizeof(g_dat_0438bef4));
    memset(g_dat_0438bf0c, 0, sizeof(g_dat_0438bf0c));
    memset(g_dat_0438bf24, 0, sizeof(g_dat_0438bf24));
    g_dat_0438b4d0 = 0;
}

void stage_post_load_set_mode_b4d0(int32_t mode)
{
    g_dat_0438b4d0 = mode;
}

int32_t stage_post_load_get_mode_b4d0(void)
{
    return g_dat_0438b4d0;
}

float stage_post_load_get_dat_0438bedc_as_float(int idx)
{
    if (idx < 0 || idx >= 6) return 0.0f;
    float out;
    memcpy(&out, &g_dat_0438bedc[idx], sizeof(out));
    return out;
}

/* ─── FUN_00435c98 body ──────────────────────────────────────────────── */

void stage_post_load_init(void)
{
    /* E.3 probe — FUN_00435c98 @ 0x435c98 (post-new-game stage init). */
    CALL_TRACE_ENTER(0x435c98u);

    /* piVar2 = the active chara's record in the work record (+0x2ceb0 +
     * DAT_0438b7d8 * 0x6c). */
    int32_t *chara = chara_rec(chara_equip_get_current_chara());
    if (chara == NULL) return;

    /* L33114: stat aggregator — full body in chara_equip. */
    chara_equip_recompute_aggregate();

    /* L33115-33117: scratch tag triple (0, 3, 1). */
    g_dat_056da1cc = 0;
    g_dat_056da1d0 = 3;
    g_dat_056da1d4 = 1;

    /* L33118: FUN_0047a8c0(stage_record) — engine fires this with
     * &DAT_044e3798 + DAT_0438b1e0 * 0x2dfc8 = save_bank arena for
     * current bank.  In the engine the stage_record and save_bank
     * arena are the same memory; in the port they're separate stores
     * but DAT_0438b1e0 is BSS-zero on NEW GAME so chara_equip's bank
     * 0 = save_bank's bank 0 — apply the interp against bank 0 to
     * match retail's call count.  Body is idempotent (same level
     * produces same writes), so re-firing after save_bank_init_one's
     * 8 in-loop calls is a no-op on state but fires the probe (= the
     * 9th call needed for call-count parity). */
    save_bank_apply_chara_interp(chara_equip_get_current_bank());

    /* L33119-33124: HP = max HP (int16 +0x3c + int16 +0x3e), SP = +0x40 + +0x42;
     * the gauges' shown values (db0c4 / db0c8) start full. */
    g_dat_056db0d8 = 0;
    {
        int16_t hp_base, hp_bonus;
        const uint8_t *p = (const uint8_t *)chara;
        memcpy(&hp_base, p + 0x3c, sizeof(hp_base));
        memcpy(&hp_bonus, p + 0x3e, sizeof(hp_bonus));
        g_dat_056db0bc = (float)((int32_t)hp_base + (int32_t)hp_bonus);
    }
    g_dat_0438bea0 = 0;
    {
        int16_t sp_base, sp_bonus;
        const uint8_t *p = (const uint8_t *)chara;
        memcpy(&sp_base, p + 0x40, sizeof(sp_base));
        memcpy(&sp_bonus, p + 0x42, sizeof(sp_bonus));
        g_dat_056db0c0 = (float)((int32_t)sp_base + (int32_t)sp_bonus);
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

    /* L33136: DAT_0438b91c = (float)work dword +0x2c3f4. */
    {
        const uint32_t *w = save_work_dwords_at(save_work_active_slot());
        g_dat_0438b91c = w ? (float)(int32_t)w[0x2c3f4 / 4] : 0.0f;
    }

    /* L33137: post-init sibling — full body in chara_skills. */
    chara_skills_init_at_stage_load();

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

    /* L33157-33158: siblings (both full bodies now).  Engine passes
     * (1, 0) — reset arrays, force-clear slot 0. */
    stage_post_load_pulse_5fold(1, 0);
    stage_post_load_pulse_first_row(1, 0);
}
