/*
 * stage_palette — per-stage palette/state record (C7d, 2026-05-23).
 *
 * Engine: a 0x1b3c-byte table entry at `&DAT_068dd2f8`, indexed by the
 * current scene-1 stage index (`DAT_0438b4dc`):
 *
 *   DAT_068dd2f0 = &DAT_068dd2f8 + DAT_0438b4dc * 0x1b3c;
 *
 * `DAT_068dd2f0` is a pointer-to-current-entry; scene-1 code reads
 * fields via `*(T *)(DAT_068dd2f0 + <offset>)`. We mirror that two-
 * level layout exactly:
 *
 *   g_stage_palette         — pointer (analog of DAT_068dd2f0)
 *   g_stage_palette_house   — static record (analog of the
 *                              "stage 0 = HOUSE" slot of the table)
 *
 * Only the HOUSE record exists today; dungeon variants land in C7q
 * (the `DAT_0438b7d8` switch in FUN_00474a9a).
 *
 * Field naming policy:
 *
 *   The record is 0x1b3c bytes and scene-1 reads ~15 distinct field
 *   offsets across 459dfd / 4547ab / 405d70 / 458f67 / 4552d0 /
 *   4597ad / 4597dd / 458bdf / 436f97 / 4176ff. Only the fields
 *   scene1-render.md C7d explicitly calls out get typed here — the
 *   rest stays as opaque padding until its porter (C7e onward)
 *   types it. Padding fields use the `_pad_<offset>` convention with
 *   sizes derived from the next-typed-field offset.
 *
 *   Every typed field carries a `_Static_assert(offsetof)` at the
 *   bottom of this header so the layout can't drift silently as
 *   future chips add fields.
 *
 * HOUSE defaults: all zero. The engine starts from BSS-zero state and
 * mutates these fields during stage entry. For HOUSE specifically the
 * defaults remain zero because:
 *
 *   - `mode == 0`         → scene-1 takes the HOUSE branch
 *   - `lighting_*_1a88/c == 0` → 405d70.c skips the vector path
 *   - `clear_r/g/b == 0`  → 4547ab.c clears to (0,0,0) black
 *
 * `stage_palette_init_house()` is idempotent; calling it multiple
 * times re-zeroes the record and re-points `g_stage_palette` at it.
 * Same overwrite-not-OR contract as `stage_init_house()` (see
 * `stage_state.h`) — important when a future stage transition lands
 * back here mid-run.
 */

#ifndef OPENRECET_STAGE_PALETTE_H
#define OPENRECET_STAGE_PALETTE_H

#include <stddef.h>
#include <stdint.h>

/* Engine per-stage record stride. Confirmed from the assignment
 * `DAT_068dd2f0 = &DAT_068dd2f8 + DAT_0438b4dc * 0x1b3c` in
 * FUN_00474681 / FUN_00436f97. */
#define STAGE_PALETTE_SIZE 0x1b3c

typedef struct stage_palette_s {
    /* +0x0000 — mode flag. 0 = HOUSE (shop interior); non-zero =
     * DUNGEON (sub-mode in DAT_0438b7d8). Read by every scene-1
     * branch: 474a9a / 473c15 / 474681 / 40a765 / 433674 / 453384
     * / 47df4d / 48a833 / 489e66 / 44bd0d / 48cdcc / 483170 /
     * 4161c7 / 409925 / 406584 / 48a833. */
    int32_t mode;

    char _pad_0004[0x1a7c - 0x4];

    /* +0x1a7c/80/84 — XYZ vector. Read by FUN_00405d70 into D3D
     * light/fog locals; doc-aliased "gravity" in scene1-render.md
     * though the actual semantics (light direction vs gravity) is
     * unconfirmed pending the FUN_00405d70 + FUN_0040a765 ports.
     * Gated by `lighting_flag_1a88/8c` below — both zero for HOUSE
     * keeps the vector path dead, so the values don't matter yet. */
    float gravity_x;
    float gravity_y;
    float gravity_z;

    /* +0x1a88, +0x1a8c — lighting-mode flags. FUN_00405d70 gates
     * the vector read on these: when both zero, the vector path
     * is skipped. Values 1 and 2 at +0x1a88 select different
     * branches in DUNGEON. */
    int32_t lighting_flag_1a88;
    int32_t lighting_flag_1a8c;

    char _pad_1a90[0x1aa8 - 0x1a90];

    /* +0x1aa8/ac/b0 — clear-color RGB. Each field is a uint32; only
     * the LOW BYTE is used. FUN_004547ab packs them into one ARGB
     * (alpha = 0xff) and passes to IDirect3DDevice8::Clear. Engine
     * HOUSE default: zero → black. The placeholder navy in
     * scene_ingame is unrelated and goes away once 4547ab ports. */
    int32_t clear_r;
    int32_t clear_g;
    int32_t clear_b;

    char _pad_1ab4[STAGE_PALETTE_SIZE - 0x1ab4];
} stage_palette_t;

_Static_assert(sizeof(stage_palette_t) == STAGE_PALETTE_SIZE,
               "stage_palette_t size must match engine 0x1b3c-byte record");

_Static_assert(offsetof(stage_palette_t, mode)               == 0x0000,
               "stage_palette_t.mode @ +0x0000");
_Static_assert(offsetof(stage_palette_t, gravity_x)          == 0x1a7c,
               "stage_palette_t.gravity_x @ +0x1a7c");
_Static_assert(offsetof(stage_palette_t, gravity_y)          == 0x1a80,
               "stage_palette_t.gravity_y @ +0x1a80");
_Static_assert(offsetof(stage_palette_t, gravity_z)          == 0x1a84,
               "stage_palette_t.gravity_z @ +0x1a84");
_Static_assert(offsetof(stage_palette_t, lighting_flag_1a88) == 0x1a88,
               "stage_palette_t.lighting_flag_1a88 @ +0x1a88");
_Static_assert(offsetof(stage_palette_t, lighting_flag_1a8c) == 0x1a8c,
               "stage_palette_t.lighting_flag_1a8c @ +0x1a8c");
_Static_assert(offsetof(stage_palette_t, clear_r)            == 0x1aa8,
               "stage_palette_t.clear_r @ +0x1aa8");
_Static_assert(offsetof(stage_palette_t, clear_g)            == 0x1aac,
               "stage_palette_t.clear_g @ +0x1aac");
_Static_assert(offsetof(stage_palette_t, clear_b)            == 0x1ab0,
               "stage_palette_t.clear_b @ +0x1ab0");

/* Storage. The pointer is the engine analog of DAT_068dd2f0; the
 * static record is the engine analog of `DAT_068dd2f8 + stage_idx *
 * 0x1b3c` for stage_idx = 0. Tests reach into both. */
extern stage_palette_t g_stage_palette_house;
extern stage_palette_t *g_stage_palette;

/* Reset the HOUSE record to engine fresh-game defaults (all zero)
 * and point `g_stage_palette` at it. Idempotent. */
void stage_palette_init_house(void);

#endif /* OPENRECET_STAGE_PALETTE_H */
