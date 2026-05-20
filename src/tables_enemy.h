/*
 * tables_enemy.h — parser for `data/enemy.txt` (block #5 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `enemy.txt` configures 64 pre-baked enemy records that ship in the
 * engine's `.data` segment at `&DAT_005c23f0` (stride 0x68 = 104 bytes).
 * Each record's NAME is hard-coded into the binary; the file only
 * fills in stats (HP, EXP, AT, DF, MA, MD) and two drop-item slots.
 *
 * Each data line:
 *
 *     <name>:<HP>#<EXP>#<AT>#<DF>#<MA>#<MD>#<drop_common>#<drop_rare>
 *
 * The leading `<name>` is matched against the 64 pre-baked record
 * names by **longest common prefix** (engine: L867..L918 of
 * docs/decompiled/by-address/475270.c). All 64 records are walked per
 * line; the record whose stored name is the longest prefix of the
 * line wins. Lines whose first byte is `\r`, `\n`, `/`, or ` ` are
 * skipped (comments / blanks).
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_ENEMY_H
#define OPENRECET_TABLES_ENEMY_H

#include <stddef.h>
#include <stdint.h>

/* Total record slots in the engine's fixed array at `&DAT_005c23f0`.
 * The array spans `&DAT_005c23f0`..`&DAT_005c4c90` (0x2a00 bytes /
 * 0x68 stride = 64 records). The parser's outer loop iterates until
 * either reaching the end of the array OR hitting a sentinel record
 * with `flags == 2`; vendor data has no such sentinel, so all 64
 * records are walked on every line. */
#define ENEMY_COUNT 64

/* Engine name buffer width (bytes 0x00..0x1f of each record). SJIS
 * encoded, NUL-padded. Pre-baked into `.data` at link time; the
 * parser never writes here. */
#define ENEMY_NAME_LEN 32

/* Per-line cap matching the engine's `local_14 == 0x100` safety
 * bound. Vendor lines fit well under this; the cap is purely
 * defensive against pathological inputs. */
#define ENEMY_LINE_CAP 0x100

/* Per-drop-name cap matching the engine's `iVar6 == 0x20` write cap
 * (cases 6/7 of the field switch at L965..L973). */
#define ENEMY_DROP_NAME_LEN 33  /* 0x20 chars + NUL */

/*
 * One enemy record. Mirrors the engine layout at `&DAT_005c23f0` +
 * idx*0x68:
 *
 *   +0x00  name[0x20]        — SJIS, NUL-padded (pre-baked in .data)
 *   +0x20  flags    (i32)    — 0 = normal, 1 = boss-class; engine
 *                              treats `flags == 2` as an end-of-table
 *                              sentinel during parsing, but no vendor
 *                              record uses that value.
 *   +0x24  unknown_24 (i32)  — runtime data (e.g. sprite index?);
 *                              not touched by enemy.txt parsing.
 *   +0x28  unknown_28 (f32)  — runtime float (often 1.0); not touched.
 *   +0x2c  hp        (i32)   — file field 1 (HP)
 *   +0x30  exp_reward(i32)   — file field 2 (EXP)
 *   +0x34  at        (i32)   — file field 3 (AT)
 *   +0x38  df        (i32)   — file field 4 (DF)
 *   +0x3c  ma        (i32)   — file field 5 (MA)
 *   +0x40  md        (i32)   — file field 6 (MD)
 *   +0x44  runtime[7](f32)   — collision/render floats; not touched
 *                              by enemy.txt parsing.
 *   +0x60  drop_common(i32)  — file field 7: name → item.txt lookup
 *                              (item id, -1 if blank or not found)
 *   +0x64  drop_rare  (i32)  — file field 8: same lookup
 *
 * Total: 0x68 (104) bytes per record × 64 = 0x1a00 bytes.
 */
typedef struct {
    char     name[ENEMY_NAME_LEN];   /* +0x00 */
    int32_t  flags;                  /* +0x20 */
    int32_t  unknown_24;             /* +0x24 */
    float    unknown_28;             /* +0x28 */
    int32_t  hp;                     /* +0x2c */
    int32_t  exp_reward;             /* +0x30 */
    int32_t  at;                     /* +0x34 */
    int32_t  df;                     /* +0x38 */
    int32_t  ma;                     /* +0x3c */
    int32_t  md;                     /* +0x40 */
    float    runtime_floats[7];      /* +0x44 .. +0x5f */
    int32_t  drop_common;            /* +0x60 */
    int32_t  drop_rare;              /* +0x64 */
} enemy_record_t;                    /* = 0x68 */

_Static_assert(sizeof(enemy_record_t) == 0x68,
               "enemy_record_t size must be 0x68");
_Static_assert(offsetof(enemy_record_t, name)        == 0x00, "name @ 0x00");
_Static_assert(offsetof(enemy_record_t, flags)       == 0x20, "flags @ 0x20");
_Static_assert(offsetof(enemy_record_t, unknown_24)  == 0x24, "unknown_24 @ 0x24");
_Static_assert(offsetof(enemy_record_t, unknown_28)  == 0x28, "unknown_28 @ 0x28");
_Static_assert(offsetof(enemy_record_t, hp)          == 0x2c, "hp @ 0x2c");
_Static_assert(offsetof(enemy_record_t, exp_reward)  == 0x30, "exp_reward @ 0x30");
_Static_assert(offsetof(enemy_record_t, at)          == 0x34, "at @ 0x34");
_Static_assert(offsetof(enemy_record_t, df)          == 0x38, "df @ 0x38");
_Static_assert(offsetof(enemy_record_t, ma)          == 0x3c, "ma @ 0x3c");
_Static_assert(offsetof(enemy_record_t, md)          == 0x40, "md @ 0x40");
_Static_assert(offsetof(enemy_record_t, runtime_floats) == 0x44, "runtime_floats @ 0x44");
_Static_assert(offsetof(enemy_record_t, drop_common) == 0x60, "drop_common @ 0x60");
_Static_assert(offsetof(enemy_record_t, drop_rare)   == 0x64, "drop_rare @ 0x64");

/* Engine-global array, populated from src/tables.c. */
extern enemy_record_t g_enemy[ENEMY_COUNT];

/*
 * Pre-populate `records` with the 64 enemy names + boss flags that the
 * engine ships in `.data`. Zeroes the rest of each record. Callers
 * MUST invoke this before `tables_parse_enemy`, because the parser
 * matches each line's leading name against records[].name and would
 * find nothing in an all-zero table.
 *
 * The pre-baked values mirror `&DAT_005c23f0` at boot time (extracted
 * from vendor/unpacked/recettear.unpacked.exe). See tables_enemy.c
 * for the source data.
 */
void tables_enemy_init(enemy_record_t records[ENEMY_COUNT]);

/*
 * Parse an enemy.txt buffer into `records`. Records must already
 * carry their pre-baked names (see tables_enemy_init). For each data
 * line, finds the longest-prefix matching record by name and updates
 * its hp/exp/at/df/ma/md stats and drop_common/drop_rare ids.
 *
 * Line dispatch (engine: FUN_00475270 L834..L1026):
 *   '/', ' ', '\r', '\n'  — first byte: comment / blank → skipped
 *   <name>:<6 ints separated by '#'>#<common>#<rare>  — data row
 *
 * Field delimiters recognized: `:`, `,`, `;`, `#`. The first
 * delimiter terminates the name; the next six fields are atoi'd into
 * hp/exp/at/df/ma/md; the last two fields are looked up by name in
 * the item.txt table and stored as item ids (or -1 if not found /
 * empty).
 *
 * Each line resets both drops to -1 before parsing.
 *
 * The line buffer is capped at ENEMY_LINE_CAP characters; per-drop
 * names at ENEMY_DROP_NAME_LEN-1 chars. Vendor data fits well under
 * both caps.
 *
 * **Cross-table dependency:** drop-name → item-id resolution requires
 * `item.txt` to have been parsed first. Until that loader lands in
 * Phase B, drops resolve to -1 unconditionally. See tables_enemy.c
 * for the lookup callback hook. The engine's MessageBoxA on a
 * missing drop name is intentionally suppressed.
 *
 * **Cross-record dependency:** the per-line name match is a
 * **longest common prefix** lookup against records[].name. Records
 * with name strlen == 0 (placeholder slots) and the "all-spaces"
 * placeholder name (" ") cannot match any non-comment data line, so
 * they stay at their pre-init values.
 */
void tables_parse_enemy(const unsigned char *data, size_t size,
                        enemy_record_t records[ENEMY_COUNT]);

#endif /* OPENRECET_TABLES_ENEMY_H */
