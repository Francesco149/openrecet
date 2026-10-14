/*
 * floor_layout.h — the dungeon floor-layout generator (FUN_0047df4d family).
 *
 * Retail runs FUN_0047df4d from the stage-entry init FUN_00436f97 on EVERY
 * stage entry, the house included: stage class 0 / floor 0 still builds a
 * small maze (house Continue load, buy-40pct-accept: 7x5, 7 cells).  The
 * house never reads the layout, but its rand() draws are real (34 there),
 * so skipping it shifts every later consumer.
 *
 *   FUN_0047df4d @ 0x47df4d  floor_layout_generate   up to 50 maze tries,
 *                 then the per-cell room pattern/type table, the start / exit /
 *                 extra-cell picks and the treasure cells (DAT_0438b200..3e0).
 *   FUN_0047d622 @ 0x47d622  floor_layout_maze       one maze try: W×H pick,
 *                 recursive carve, loop/edge extension, grid compaction.
 *   FUN_0047ddc2 @ 0x47ddc2  (static) carve          recursive 2-step carver.
 *   FUN_0045bc7f @ 0x45bc7f  floor_layout_boss_group boss group of a floor.
 *
 * Every table below is the retail global at the noted VA, value for value
 * (src/state_va.c maps them for `rhost.py portstate`).  The grid stride is
 * 15 regardless of W (retail's fixed 15x15 arrays).
 *
 * Inputs come in floor_layout_in_t (the port has no single home for the
 * stage globals yet); the two gate predicates are function pointers so the
 * rhost shadow can hand retail's own FUN_0043195d / FUN_004319d6.
 */
#ifndef OPENRECET_FLOOR_LAYOUT_H
#define OPENRECET_FLOOR_LAYOUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLOOR_GRID_STRIDE 15
#define FLOOR_GRID_CELLS  (FLOOR_GRID_STRIDE * FLOOR_GRID_STRIDE)   /* 225 */
#define FLOOR_LIST_MAX    100
#define FLOOR_PICK_MAX    20

typedef struct {
    int32_t stage_class;      /* DAT_0438b4c8 — dungeon id (0 = house/1st) */
    int32_t floor;            /* DAT_0438b4cc — floor number */
    int32_t stage_no;         /* DAT_0438b4dc — debug text row only */
    int32_t dungeon;          /* *DAT_068dd2f0 == 1 (stage palette mode) */
    int32_t rec_flag_a;       /* work record byte +0x2bce2 (DAT_0450f47a) */
    int32_t rec_flag_b;       /* work record byte +0x2bcf8 (DAT_0450f490) */
    int   (*is_checkpoint)(void);   /* FUN_0043195d */
    int   (*gate_query)(void);      /* FUN_004319d6 */
} floor_layout_in_t;

typedef struct {
    int32_t type;    /* room type, -1 = no room   (DAT_073e03ac) */
    int32_t pat;     /* pattern row k/5           (DAT_073e03b0) */
    int32_t var;     /* pattern variant k%5(+5)   (DAT_073e03b4) */
    int32_t pad;     /*                           (DAT_073e03b8) */
} floor_room_t;

extern int32_t      g_floor_w;                            /* DAT_005cc2e8 (.data 9) */
extern int32_t      g_floor_h;                            /* DAT_005cc2ec (.data 5) */
extern int32_t      g_floor_maze[FLOOR_GRID_CELLS];       /* DAT_073e11bc [y*15+x] */
extern int32_t      g_floor_ncells;                       /* DAT_073e1540 */
extern int32_t      g_floor_list[FLOOR_LIST_MAX][2];      /* DAT_073e008c (x, y) */
extern int32_t      g_floor_grid[FLOOR_GRID_CELLS];       /* DAT_073dfd08 [y*W+x] */
extern floor_room_t g_floor_rooms[FLOOR_GRID_CELLS];      /* DAT_073e03ac [y*15+x] */
extern int32_t      g_floor_origin[2];                    /* DAT_0438b1f8/1fc */
extern int32_t      g_floor_extra_n;                      /* DAT_0438b430 */
/* DAT_0438b434..0x438b4ab (15 pairs): [0] start cell, [1] exit cell, [2..] the
 * extra_n marked cells (checkpoint floors: list[0..9] copied as-is).
 * FUN_00436f97 zeroes pairs 10..14 (memset 0x438b484, 0x28) before calling the
 * generator. */
#define FLOOR_MARKS_MAX 15
extern int32_t      g_floor_marks[FLOOR_MARKS_MAX][2];
extern float        g_floor_pick_pos[FLOOR_PICK_MAX][3];  /* DAT_0438b200 */
extern int32_t      g_floor_pick_cell[FLOOR_PICK_MAX][2]; /* DAT_0438b2f0 */
extern int32_t      g_floor_pick_type[FLOOR_PICK_MAX];    /* DAT_0438b390 */
extern int32_t      g_floor_pick_flag[FLOOR_PICK_MAX];    /* DAT_0438b3e0 */
extern int32_t      g_floor_rooms_total;                  /* DAT_0438b4b0 */
extern int32_t      g_floor_c148;                         /* DAT_0438c148 */

/* DAT_0438b1ec/f0/f4 — the stage spawn origin.  Defined here (its retail
 * writer); scene1_postload copies it into the player position. */
extern float g_scene1_stage_player_default_pos[3];

void    floor_layout_generate(const floor_layout_in_t *in);   /* FUN_0047df4d */
void    floor_layout_maze(const floor_layout_in_t *in);       /* FUN_0047d622 */
int32_t floor_layout_boss_group(int32_t stage_class, int32_t floor);   /* FUN_0045bc7f */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_FLOOR_LAYOUT_H */
