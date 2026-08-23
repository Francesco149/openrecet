/*
 * diff_entry.h — ABI for tools/diff_test.py's ctypes consumers.
 *
 * Every target follows the same shape:
 *
 *   void engine_<target>(const Engine<Target>In *in, Engine<Target>Out *out);
 *
 * The In struct holds the inputs the orchestrator must inject on the
 * retail side (via Frida writeU32 / writeMemory / ...) before invoking
 * the retail function; the Out struct holds the observables the
 * orchestrator reads back to compare.
 *
 * Wire stability: append new fields at the end of existing structs;
 * never reorder.  diff_test.py mirrors these structs with
 * ctypes.Structure declarations, so layout changes here require
 * matching changes on the Python side.
 */

#ifndef OPENRECET_DIFF_ENTRY_H
#define OPENRECET_DIFF_ENTRY_H

#include <stdint.h>

/* ── rng_next15 (FUN_005041f6 / DAT_006023a0) ──────────────────────── */

typedef struct EngineRngIn {
    uint32_t seed;          /* injected pre-state for DAT_006023a0 */
} EngineRngIn;

typedef struct EngineRngOut {
    uint32_t post_state;    /* DAT_006023a0 after one LCG step */
    uint16_t ret_value;     /* 15-bit return of FUN_005041f6 */
    uint16_t _pad;          /* explicit, keeps layout deterministic */
} EngineRngOut;

void engine_rng_next15(const EngineRngIn *in, EngineRngOut *out);

/* ── audio_fade (FUN_00499583 / BGM cos-curve fade) ────────────────── */

typedef struct EngineFadeIn {
    int32_t slider;         /* BGM volume slider in [0, 9] */
} EngineFadeIn;

typedef struct EngineFadeOut {
    int32_t centibel;       /* audio_fade_compute(slider, 0) result */
} EngineFadeOut;

void engine_audio_fade(const EngineFadeIn *in, EngineFadeOut *out);

/* ── stage_gate_boss_id_allowed (FUN_00431990) ─────────────────────────
 *
 * E.4 Tier 1 (first STATEFUL/non-pure-RNG diff target): a pure boss-id
 * range predicate.  No globals — proves the arg-injection path (the
 * retail side passes the id as a cdecl stack arg, where rng/fade took
 * none).  enemy_id is signed (the engine does signed compares; the -1
 * empty-slot sentinel must return 0). */

typedef struct EngineBossIdIn {
    int32_t enemy_id;       /* injected as FUN_00431990's cdecl arg */
} EngineBossIdIn;

typedef struct EngineBossIdOut {
    int32_t allowed;        /* 0/1 return of FUN_00431990 */
} EngineBossIdOut;

void engine_stage_gate_boss_id_allowed(const EngineBossIdIn *in,
                                       EngineBossIdOut *out);

/* ── stage_gate_floor_is_checkpoint (FUN_0043195d) ─────────────────────
 *
 * E.4 Tier 1: the canonical "stateful pure-ish leaf" — reads two globals
 * (no args) and returns 0/1.  Proves the GLOBAL-injection path: the
 * retail side snapshots+writes DAT_0438b4c8 (dungeon id) and
 * DAT_0438b4cc (next floor), calls, reads back, restores.  next_floor is
 * signed — the engine's `next % 5` is a signed idiv, matching C's `%`, so
 * the vectors include negative next_floor to prove the sign agreement. */

typedef struct EngineCheckpointIn {
    int32_t dungeon_id;     /* injected pre-state for DAT_0438b4c8 */
    int32_t next_floor;     /* injected pre-state for DAT_0438b4cc */
} EngineCheckpointIn;

typedef struct EngineCheckpointOut {
    int32_t is_checkpoint;  /* 0/1 return of FUN_0043195d */
} EngineCheckpointOut;

void engine_stage_gate_floor_is_checkpoint(const EngineCheckpointIn *in,
                                           EngineCheckpointOut *out);

/* ── haggle_decide (FUN_00460672 / Accept-Counter-Reject Evaluator) ─── */

typedef struct EngineHaggleDecideIn {
    int32_t player_ask;     /* named ask price */
    int32_t accept_ref;     /* customer reference price */
} EngineHaggleDecideIn;

typedef struct EngineHaggleDecideOut {
    int32_t verdict;        /* 1 = ACCEPT, 2 = COUNTER, 0 = REJECT */
} EngineHaggleDecideOut;

void engine_haggle_decide(const EngineHaggleDecideIn *in,
                          EngineHaggleDecideOut *out);

/* ── haggle_budget_ceiling (FUN_0045ecc0 / Customer Budget Calculator) ─ */

typedef struct EngineHaggleBudgetCeilingIn {
    int32_t market_price;   /* item market price */
    int32_t budget_low;     /* customer low budget */
    int32_t budget_high;    /* customer high budget */
} EngineHaggleBudgetCeilingIn;

typedef struct EngineHaggleBudgetCeilingOut {
    int32_t ceiling;        /* computed hard budget ceiling */
} EngineHaggleBudgetCeilingOut;

void engine_haggle_budget_ceiling(const EngineHaggleBudgetCeilingIn *in,
                                  EngineHaggleBudgetCeilingOut *out);

/* ── audio_is_one_shot_track (FUN_00498ef4 / Track Loop Predicate) ──── */

typedef struct EngineAudioOneShotIn {
    int32_t track;          /* BGM track index */
} EngineAudioOneShotIn;

typedef struct EngineAudioOneShotOut {
    int32_t is_one_shot;    /* 1 if one-shot, 0 if looping */
} EngineAudioOneShotOut;

void engine_audio_is_one_shot_track(const EngineAudioOneShotIn *in,
                                    EngineAudioOneShotOut *out);

/* ── customer_service_pushback_patience (FUN_00460f16) ──────────────── */

typedef struct EnginePushbackPatienceIn {
    int32_t loyalty_level;  /* customer loyalty level (0..8) */
    int32_t sell_active;    /* player-initiated sell flag (0 or 1) */
} EnginePushbackPatienceIn;

typedef struct EnginePushbackPatienceOut {
    int32_t patience_variant; /* pushback line variant (2, 3, or 4) */
} EnginePushbackPatienceOut;

void engine_customer_service_pushback_patience(const EnginePushbackPatienceIn *in,
                                              EnginePushbackPatienceOut *out);

/* ── customer_service_budget_level_day (FUN_00461011) ───────────────── */

typedef struct EngineBudgetLevelDayIn {
    int32_t cand_idx;       /* candidate index */
    int32_t shop_day;       /* current shop day */
    int32_t closeness_level;/* customer closeness level (0..8) */
} EngineBudgetLevelDayIn;

typedef struct EngineBudgetLevelDayOut {
    int32_t budget;         /* day-scaled budget */
} EngineBudgetLevelDayOut;

void engine_customer_service_budget_level_day(const EngineBudgetLevelDayIn *in,
                                             EngineBudgetLevelDayOut *out);

/* ── tables_item_find_slot_by_id (FUN_004681f6 / Item ID Slot Lookup) ── */

typedef struct EngineItemFindSlotIn {
    int32_t item_id;        /* target item id */
} EngineItemFindSlotIn;

typedef struct EngineItemFindSlotOut {
    int32_t slot_idx;       /* matched slot index or -1 */
} EngineItemFindSlotOut;

void engine_tables_item_find_slot_by_id(const EngineItemFindSlotIn *in,
                                        EngineItemFindSlotOut *out);

/* ── chara_equip_item_stats (FUN_0048093f / Equipment Stat Distributor) ─ */

typedef struct EngineCharaEquipStatsIn {
    uint32_t slot_val;      /* encoded item slot value */
    int32_t initial_sum[4]; /* initial stats sum (atk, def, mag, mdef) */
} EngineCharaEquipStatsIn;

typedef struct EngineCharaEquipStatsOut {
    int32_t sum[4];         /* mutated stats sum */
} EngineCharaEquipStatsOut;

void engine_chara_equip_item_stats(const EngineCharaEquipStatsIn *in,
                                   EngineCharaEquipStatsOut *out);

#endif /* OPENRECET_DIFF_ENTRY_H */
