/*
 * tables_chara.h — parser for `data/chara.txt` (block #6 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `chara.txt` defines the eight playable adventurer companions Recette
 * can dispatch into dungeons. Each record holds level-1 base stats
 * (HP, SP, four attack/defense ratings, three movement floats) plus
 * the corresponding stat block at level 100 (six ints) used to
 * interpolate per-level growth between the two endpoints.
 *
 * The engine allocates 8 fixed records at `&DAT_073ae058` (stride
 * 0x40 = 64 bytes). The records are contiguous in memory with the
 * 20 `model.txt` records that start immediately after at
 * `&DAT_073ae258` — a 10-iteration parser overrun (engine bug, see
 * tables_chara.c) would corrupt the first two model records. The
 * vendor file never triggers this; the port guards the cap anyway.
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_CHARA_H
#define OPENRECET_TABLES_CHARA_H

#include <stddef.h>
#include <stdint.h>

/* Total number of adventurer records in the engine's fixed array.
 * Engine parse loop iterates 10 times (a 2-record overrun bug); the
 * port caps matching at this value to avoid corrupting g_models. */
#define CHARA_COUNT 8

/*
 * One adventurer definition record. Mirrors the engine layout at
 * `&DAT_073ae058` (stride 0x40 = 64 bytes):
 *
 *   +0x00  level_threshold (i32)  — atoi(field1) - 1; "unlock level minus 1"
 *   +0x04  hp_base         (i32)  — initial HP at level (threshold+1)
 *   +0x08  sp_base         (i32)  — initial SP
 *   +0x0c  at_base         (i32)  — attack
 *   +0x10  df_base         (i32)  — defense
 *   +0x14  mt_base         (i32)  — magic attack
 *   +0x18  mf_base         (i32)  — magic defense
 *   +0x1c  move_speed      (f32)  — walking velocity
 *   +0x20  dash_speed      (f32)  — sprint velocity
 *   +0x24  crit_rate       (f32)  — critical hit probability [0..1]
 *   +0x28  hp_lv100        (i32)  — HP at level 100 (interpolation upper)
 *   +0x2c  sp_lv100        (i32)
 *   +0x30  at_lv100        (i32)
 *   +0x34  df_lv100        (i32)
 *   +0x38  mt_lv100        (i32)
 *   +0x3c  mf_lv100        (i32)
 *
 * Total: 0x40 (64) bytes per record × 8 records = 0x200 bytes.
 *
 * Field ordering on disk does NOT match in-memory layout — the file
 * lists AT/DF/MT/MF before HP/SP for human readability, while the
 * engine stores HP/SP at the lower offsets. The parser handles the
 * permutation; consumers always see this struct order.
 */
typedef struct {
    int32_t level_threshold;  /* +0x00 */
    int32_t hp_base;          /* +0x04 */
    int32_t sp_base;          /* +0x08 */
    int32_t at_base;          /* +0x0c */
    int32_t df_base;          /* +0x10 */
    int32_t mt_base;          /* +0x14 */
    int32_t mf_base;          /* +0x18 */
    float   move_speed;       /* +0x1c */
    float   dash_speed;       /* +0x20 */
    float   crit_rate;        /* +0x24 */
    int32_t hp_lv100;         /* +0x28 */
    int32_t sp_lv100;         /* +0x2c */
    int32_t at_lv100;         /* +0x30 */
    int32_t df_lv100;         /* +0x34 */
    int32_t mt_lv100;         /* +0x38 */
    int32_t mf_lv100;         /* +0x3c */
} chara_def_t;                /* = 0x40 */

_Static_assert(offsetof(chara_def_t, level_threshold) == 0x00,
               "chara_def_t.level_threshold offset must be 0x00");
_Static_assert(offsetof(chara_def_t, hp_base)         == 0x04,
               "chara_def_t.hp_base offset must be 0x04");
_Static_assert(offsetof(chara_def_t, sp_base)         == 0x08,
               "chara_def_t.sp_base offset must be 0x08");
_Static_assert(offsetof(chara_def_t, at_base)         == 0x0c,
               "chara_def_t.at_base offset must be 0x0c");
_Static_assert(offsetof(chara_def_t, df_base)         == 0x10,
               "chara_def_t.df_base offset must be 0x10");
_Static_assert(offsetof(chara_def_t, mt_base)         == 0x14,
               "chara_def_t.mt_base offset must be 0x14");
_Static_assert(offsetof(chara_def_t, mf_base)         == 0x18,
               "chara_def_t.mf_base offset must be 0x18");
_Static_assert(offsetof(chara_def_t, move_speed)      == 0x1c,
               "chara_def_t.move_speed offset must be 0x1c");
_Static_assert(offsetof(chara_def_t, dash_speed)      == 0x20,
               "chara_def_t.dash_speed offset must be 0x20");
_Static_assert(offsetof(chara_def_t, crit_rate)       == 0x24,
               "chara_def_t.crit_rate offset must be 0x24");
_Static_assert(offsetof(chara_def_t, hp_lv100)        == 0x28,
               "chara_def_t.hp_lv100 offset must be 0x28");
_Static_assert(offsetof(chara_def_t, sp_lv100)        == 0x2c,
               "chara_def_t.sp_lv100 offset must be 0x2c");
_Static_assert(offsetof(chara_def_t, at_lv100)        == 0x30,
               "chara_def_t.at_lv100 offset must be 0x30");
_Static_assert(offsetof(chara_def_t, df_lv100)        == 0x34,
               "chara_def_t.df_lv100 offset must be 0x34");
_Static_assert(offsetof(chara_def_t, mt_lv100)        == 0x38,
               "chara_def_t.mt_lv100 offset must be 0x38");
_Static_assert(offsetof(chara_def_t, mf_lv100)        == 0x3c,
               "chara_def_t.mf_lv100 offset must be 0x3c");
_Static_assert(sizeof(chara_def_t) == 0x40,
               "chara_def_t size must be 0x40");

/* Engine-global array, populated from src/tables.c. */
extern chara_def_t g_chara[CHARA_COUNT];

/*
 * Parse a chara.txt buffer into `out[CHARA_COUNT]`. Seeds engine
 * defaults first (level_threshold=1, hp_base=50, sp_base=30,
 * at_base=10, df_base=13, mt_base=5, mf_base=10, move_speed=0.15f,
 * dash_speed=0.20f; the lv100 stats and crit_rate are zeroed).
 *
 * Line dispatch (engine: FUN_00475270 L1050..L1146 + LAB_00477931):
 *   /…, blank   — comment / skipped (first byte '/', '\r', '\n')
 *   NNN:…       — first byte '0': record index NNN ∈ [0, 7], 10 CSV
 *                 fields (7 ints + 3 floats) → base stats
 *   NNN:…       — first byte '1': record index (NNN - 100) ∈ [0, 7],
 *                 6 CSV ints → level-100 stats
 *
 * Lines whose first byte is neither '0' nor '1' (and not '/'/CR/LF)
 * are silently skipped — the engine simply falls through both
 * dispatch branches.
 */
void tables_parse_chara(const unsigned char *data, size_t size,
                        chara_def_t out[CHARA_COUNT]);

#endif /* OPENRECET_TABLES_CHARA_H */
