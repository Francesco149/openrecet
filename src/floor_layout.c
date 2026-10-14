/*
 * floor_layout.c — see floor_layout.h.  Ported from the retail disassembly
 * (0x47d622..0x47e709, 0x45bc7f); IDA pseudocode reorders a few reads.
 */
#include "floor_layout.h"

#include <stdio.h>
#include <string.h>

#include "audio_mci.h"   /* FUN_00451874 debug text rows */
#include "call_trace.h"
#include "rng.h"
#include "x87.h"

int32_t      g_floor_w = 9;
int32_t      g_floor_h = 5;
int32_t      g_floor_maze[FLOOR_GRID_CELLS];
int32_t      g_floor_ncells;
int32_t      g_floor_list[FLOOR_LIST_MAX][2];
int32_t      g_floor_grid[FLOOR_GRID_CELLS];
floor_room_t g_floor_rooms[FLOOR_GRID_CELLS];
int32_t      g_floor_origin[2];
int32_t      g_floor_extra_n;
int32_t      g_floor_marks[FLOOR_MARKS_MAX][2];
float        g_floor_pick_pos[FLOOR_PICK_MAX][3];
int32_t      g_floor_pick_cell[FLOOR_PICK_MAX][2];
int32_t      g_floor_pick_type[FLOOR_PICK_MAX];
int32_t      g_floor_pick_flag[FLOOR_PICK_MAX];
int32_t      g_floor_rooms_total;
int32_t      g_floor_c148;
float        g_scene1_stage_player_default_pos[3];

/* rodata 0x5cc2f0: 4 pattern rows x 3 lines x 5 variants x 3 cells — the
 * plus-shaped 3x3 neighbourhood masks (corners unused) a room is matched
 * against.  [k/5][line][k%5][cell]. */
static const int32_t k_room_pattern[4][3][5][3] = {
    { { {0,1,0}, {0,0,0}, {0,1,0}, {0,1,0}, {0,0,0} },
      { {1,1,1}, {1,1,1}, {1,1,1}, {0,1,1}, {0,1,1} },
      { {0,1,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    { { {0,1,0}, {0,1,0}, {0,1,0}, {0,0,0}, {0,0,0} },
      { {1,1,1}, {0,1,0}, {0,1,1}, {0,1,1}, {0,1,0} },
      { {0,1,0}, {0,1,0}, {0,1,0}, {0,1,0}, {0,1,0} } },
    { { {0,1,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} },
      { {1,1,1}, {1,1,1}, {1,1,1}, {1,1,0}, {1,1,0} },
      { {0,1,0}, {0,0,0}, {0,1,0}, {0,1,0}, {0,0,0} } },
    { { {0,1,0}, {0,1,0}, {0,1,0}, {0,1,0}, {0,1,0} },
      { {1,1,1}, {0,1,0}, {1,1,0}, {1,1,0}, {0,1,0} },
      { {0,1,0}, {0,1,0}, {0,1,0}, {0,0,0}, {0,0,0} } },
};
/* rodata 0x5cc5c0: room type per variant (k%5, +5 on dungeon floors). */
static const int32_t k_room_type[10] = { 0, 2, 3, 4, 8, 1, 6, 7, 5, 10 };

/* rodata 0x5c6460 (60 dwords).  [24..29] is the dungeon floors/5 table
 * DAT_005c64c0 (5 / 15 / 30 / 50 / 100 / 60 floors); [30..59] is the class-5
 * boss group per floor that FUN_0045bc7f indexes with the floor itself. */
static const int32_t k_5c6460[60] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 3, 6, 10, 20, 12,
    6, 10, 6, 7, 9, 11, 12, 15, 13, 12, 10, 5, 9, 10, 13, 19, 12, 10, 9, 11,
    15, 6, 7, 5, 13, 9, 16, 14, 17, 5,
};
#define k_floors_div5 (&k_5c6460[24])
/* rodata 0x5c5ae8: boss group per (stage class, floor/5). */
static const int32_t k_boss_group[6][30] = {
    { 5, 5, -1 },
    { 6, 10, 6, 6, -1 },
    { 7, 9, 14, 11, 12, 15, 15, -1 },
    { 13, 12, 10, 5, 14, 9, 16, 10, 13, 9, 19, -1 },
    { 12, 10, 9, 11, 15, 16, 6, 7, 11, 5, 13, 9, 10, 16, 13, 15, 11, 14, 10, 5, 17, 17, -1 },
    { 12, 10, 9, 11, 15, 5, 6, 7, 11, 5, 13, 9, 10, -1 },
};

static const float k_cell_size = 40.0f;   /* m32 0x5194d4 */

static float cell_to_world(int32_t c)
{
    /* fild (-c) → fstp m32 → fld → fmul m32 40.0 → fstp m32 */
    return spill32((long double)spill32((long double)-c) * k_cell_size);
}

static void dbg_row(int col, int row, const char *fmt, int32_t v)
{
    char b[256];
    snprintf(b, sizeof b, fmt, (int)v);
    audio_mci_record_command(col, row, b);
}

int32_t floor_layout_boss_group(int32_t stage_class, int32_t floor)
{
    /* E.2 probe — FUN_0045bc7f @ 0x45bc7f. */
    CALL_TRACE_ENTER(0x45bc7fu);

    int32_t g = floor / 5;
    if (floor == 5 * k_floors_div5[stage_class] - 1)
        g++;
    if (stage_class == 5) {
        if (floor >= 30)
            return k_5c6460[floor];
        /* dead in retail too (floor < 30 here): */
        if (floor == 37) return 15;
        if (floor == 45) return 19;
        if (floor == 58) return 17;
        if (floor == 59) return 5;
    }
    return k_boss_group[stage_class][g];
}

/* FUN_0047ddc2 — carve two steps from (x,y) toward (dx,dy), then recurse in
 * a shuffled direction order.  Stops at the cell budget, the bounds or an
 * already-open target. */
static void carve(int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    /* E.2 probe — FUN_0047ddc2 @ 0x47ddc2. */
    CALL_TRACE_ENTER(0x47ddc2u);

    const int32_t a = (g_floor_w - 2) * (g_floor_h - 2);
    const int32_t limit = a + a / -15 - (g_floor_w - 3) * ((g_floor_h - 2) / 2) - 1;
    dbg_row(0x1e, 0xc, "%d  ", limit);
    int32_t n = g_floor_ncells;
    if (n >= limit)
        return;
    const int32_t nx = x + 2 * dx, ny = y + 2 * dy;
    if (nx < 0 || nx >= g_floor_w || ny < 0 || ny >= g_floor_h)
        return;
    if (g_floor_maze[ny * FLOOR_GRID_STRIDE + nx] == 1)
        return;
    g_floor_maze[ny * FLOOR_GRID_STRIDE + nx] = 1;
    if (n < FLOOR_LIST_MAX) {
        g_floor_list[n][0] = nx;
        g_floor_list[n][1] = ny;
    }
    n++;
    g_floor_maze[(y + dy) * FLOOR_GRID_STRIDE + x + dx] = 1;
    if (n < FLOOR_LIST_MAX) {
        g_floor_list[n][0] = x + dx;
        g_floor_list[n][1] = y + dy;
    }
    g_floor_ncells = n + 1;

    int32_t dir[4] = { 0, 1, 2, 3 };
    for (int i = 0; i < 4; i++) {
        int r = rng_next15() & 3;
        int32_t t = dir[r];
        dir[r] = dir[i];
        dir[i] = t;
    }
    for (int i = 0; i < 4; i++) {
        switch (dir[i]) {
        case 0: carve(nx, ny, 1, 0); break;
        case 1: carve(nx, ny, -1, 0); break;
        case 2: carve(nx, ny, 0, 1); break;
        case 3: carve(nx, ny, 0, -1); break;
        }
    }
}

static void list_push(int32_t x, int32_t y)
{
    int32_t n = g_floor_ncells++;
    g_floor_list[n][0] = x;   /* unguarded in retail (100-entry list) */
    g_floor_list[n][1] = y;
}

/* rand() & 1 — retail tests `bl(=1) & al` */
static int coin(void) { return (rng_next15() & 1) != 0; }

void floor_layout_maze(const floor_layout_in_t *in)
{
    /* E.2 probe — FUN_0047d622 @ 0x47d622. */
    CALL_TRACE_ENTER(0x47d622u);

    const int32_t cls = in->stage_class, fl = in->floor;
    int32_t *W = &g_floor_w, *H = &g_floor_h;

    /* ── W×H pick ── */
    if ((fl == 49 && cls == 3) || (cls == 5 && fl >= 29)) {
        *W = 3; *H = 3;
    } else if (cls == 5) {
        switch (rng_next15() & 3) {
        case 0: *W = 5; *H = 9; break;
        case 1: *W = 9; *H = 5; break;
        case 2: *W = 7; *H = 7; break;
        case 3: *W = 7; *H = 9; break;
        }
    } else if (fl % 5 == 4 || (cls == 0 && fl == 4)) {
        if (in->gate_query()) { *W = 3; *H = 3; }
        else                  { *W = 3; *H = 5; }
    } else if (in->rec_flag_a || in->rec_flag_b) {
        *W = 3; *H = 3;
    } else if (cls == 0) {
        if (fl > 2) { *W = 5; *H = 5; }
        else if (coin()) { *W = 3; *H = 5; }
        else { *W = 5; *H = 3; }
    } else if (cls == 1) {
        if (fl <= 2) {
            if (coin()) { *W = 3; *H = 5; }
            else { *W = 5; *H = 3; }
        } else if (fl <= 10) {
            *W = 5; *H = 5;
        } else if (coin()) {
            *W = 7; *H = 5;
        } else {
            *W = 5; *H = 7;
        }
    } else if (fl <= 10) {
        if (coin()) { *W = 5; *H = 2 * (rng_next15() & 1) + 5; }
        else { *W = 2 * (rng_next15() & 1) + 5; *H = 5; }
    } else if (fl <= 20) {
        *W = 7; *H = 7;
    } else if (fl <= 30) {
        if (coin()) { *W = 7; *H = 2 * (rng_next15() & 1) + 7; }
        else { *W = 2 * (rng_next15() & 1) + 7; *H = 7; }
    } else {
        *W = 2 * (rng_next15() & 1) + 7;
        *H = 2 * (rng_next15() & 1) + 7;
    }

    /* ── origin + first carve ── */
    memset(g_floor_maze, 0, sizeof g_floor_maze);
    g_floor_origin[0] = 2 * (int32_t)((uint32_t)rng_next15() % (uint32_t)(*W / 2)) + 1;
    g_floor_origin[1] = 2 * (int32_t)((uint32_t)rng_next15() % (uint32_t)(*H / 2)) + 1;
    const int32_t ox = g_floor_origin[0], oy = g_floor_origin[1];
    g_floor_maze[oy * FLOOR_GRID_STRIDE + ox] = 1;
    list_push(ox, oy);
    int32_t dir[4] = { 0, 1, 2, 3 };
    for (int i = 0; i < 4; i++) {
        int r = rng_next15() & 3;
        int32_t t = dir[r];
        dir[r] = dir[i];
        dir[i] = t;
    }
    for (int i = 0; i < 4; i++) {   /* the first direction that fits, once */
        if (dir[i] == 0 && ox < *W - 2) { carve(ox, oy, 1, 0); break; }
        if (dir[i] == 1 && ox > 2)      { carve(ox, oy, -1, 0); break; }
        if (dir[i] == 2 && oy < *H - 2) { carve(ox, oy, 0, 1); break; }
        if (dir[i] == 3 && oy > 2)      { carve(ox, oy, 0, -1); break; }
    }
    dbg_row(0x1e, 0xa, "%d  ", *W * *H);
    dbg_row(0x1e, 0xb, "MOV %d  ", g_floor_ncells);

    /* ── loop / edge extension (up to 3 passes while the floor is 3x5/5x3) ── */
    if (!(*W == 3 && *H == 3)) {
        const int32_t w0 = *W, h0 = *H;
        int32_t added = 0;
        for (int pass = 0; pass != 3; pass++) {
            for (int j = 0; j != 2; j++) {
                const int32_t st = j == 1 ? 1 : 2;
                for (int32_t y = st; y < *H - 1; y += st) {
                    for (int32_t x = st; x < *W - 1; x += st) {
                        int32_t *c = &g_floor_maze[y * FLOOR_GRID_STRIDE + x];
                        if (*c != 0)
                            continue;
                        int skip = j != 0 ? (rng_next15() & 1) == 0 : (rng_next15() & 3) == 0;
                        if (skip)
                            continue;
                        const int32_t up = c[-FLOOR_GRID_STRIDE], dn = c[FLOOR_GRID_STRIDE];
                        const int32_t lf = c[-1], rt = c[1];
                        int join = (up == 0 && dn == 0) ? (lf == 1 && rt == 1)
                                 : (up == 1 && dn == 1 && lf == 0 && rt == 0);
                        if (join) {
                            list_push(x, y);
                            added++;
                            *c = 1;
                        }
                    }
                }
                ++*W;                                  /* new column x = W-2 */
                for (int32_t y = 2, row = 2 * FLOOR_GRID_STRIDE; y < *H - 1;
                     y++, row += FLOOR_GRID_STRIDE) {
                    const int32_t x = *W - 2;
                    if ((uint32_t)rng_next15() % 5u != 0 && g_floor_maze[row + x - 1] == 1) {
                        g_floor_maze[row + x] = 1;
                        list_push(x, y);
                        added++;
                        if (!coin()) { y++; row += FLOOR_GRID_STRIDE; }
                    }
                }
                ++*H;                                  /* new row y = H-2 */
                for (int32_t x = 2; x < *W - 1; x++) {
                    const int32_t y = *H - 2;
                    if ((uint32_t)rng_next15() % 5u != 0 &&
                        g_floor_maze[(y - 1) * FLOOR_GRID_STRIDE + x] == 1) {
                        g_floor_maze[y * FLOOR_GRID_STRIDE + x] = 1;
                        list_push(x, y);
                        added++;
                        if (!coin()) x++;
                    }
                }
            }
            if (added == 0) { *W = w0; *H = h0; }
            if (!((*W == 3 && *H == 5) || (*W == 5 && *H == 3)) || added >= 2)
                break;
        }
    }

    /* ── spawn origin, debug grid, compaction ── */
    dbg_row(0x1e, 0xc, "MOV %d  ", g_floor_ncells);
    g_scene1_stage_player_default_pos[0] = cell_to_world(ox);
    g_scene1_stage_player_default_pos[1] = 0.0f;
    g_scene1_stage_player_default_pos[2] = cell_to_world(oy);
    for (int i = 0; i < FLOOR_GRID_CELLS; i++)
        dbg_row(i % 15 + 0x3c, i / 15 + 1, "-", g_floor_maze[i]);
    for (int32_t y = 0; y < *H; y++)
        for (int32_t x = 0; x < *W; x++) {
            dbg_row(x + 0x3c, y + 1, "%d", g_floor_maze[y * FLOOR_GRID_STRIDE + x]);
            g_floor_grid[x + y * *W] = g_floor_maze[y * FLOOR_GRID_STRIDE + x];
        }
}

void floor_layout_generate(const floor_layout_in_t *in)
{
    /* E.2 probe — FUN_0047df4d @ 0x47df4d. */
    CALL_TRACE_ENTER(0x47df4du);

    const int32_t cls = in->stage_class, fl = in->floor;
    const int rec_flag = in->rec_flag_a || in->rec_flag_b;

    dbg_row(0x1e, 7, "STAGENO:%4d", in->stage_no);
    for (int i = 0; i != 50; i++) {
        g_floor_ncells = 0;
        floor_layout_maze(in);
        if (g_floor_w == 3 && g_floor_h == 3)
            break;
        if (g_floor_ncells > 6)
            break;
    }
    dbg_row(0x1e, 0xd, "MOV:%4d", g_floor_w);
    dbg_row(0x1e, 0xe, "MOV:%4d", g_floor_h);

    for (int i = 0; i < FLOOR_GRID_CELLS; i++)
        g_floor_rooms[i] = (floor_room_t){ -1, 0, 0, 0 };
    memset(g_floor_pick_type, 0, sizeof g_floor_pick_type);
    memset(g_floor_pick_flag, 0, sizeof g_floor_pick_flag);

    /* ── room pattern + type per open interior cell ── */
    for (int32_t y = 1; y < g_floor_h - 1; y++) {
        for (int32_t x = 1; x != g_floor_w - 1; x++) {
            const int32_t W = g_floor_w;
            if (g_floor_grid[W * y + x] != 1)
                continue;
            int32_t k;
            for (k = 0; k != 20; k++) {
                int match = 0;
                for (int r = 0; r != 3; r++)
                    for (int m = 0; m != 3; m++) {
                        if ((r == 0 || r == 2) && (m == 0 || m == 2))
                            continue;
                        /* DAT_073dfd04[(y-1+r)*W + x + m] = grid[.. - 1] */
                        if (g_floor_grid[(y - 1 + r) * W + x + m - 1] == k_room_pattern[k / 5][r][k % 5][m])
                            match++;
                    }
                if (match == 5)
                    break;
            }
            /* k == 20 (no match) keeps going with pat 4 / var 0, as retail */
            int32_t var = k % 5;
            if (in->dungeon && coin())
                var += 5;
            floor_room_t *rm = &g_floor_rooms[y * FLOOR_GRID_STRIDE + x];
            rm->type = k_room_type[var];
            if (g_floor_w == 3 && g_floor_h == 3)
                rm->type = 9;
            if (cls == 0) {
                if (fl != 4) goto boss;
                rm->type = 12;
            }
            if (cls == 1) {
                if (fl != 9) goto boss;
                rm->type = 12;
            }
            if (cls == 5) {
                if (fl != 31) goto boss;
                rm->type = 12;
            }
            if (cls == 2) {
                if (fl == 19) rm->type = 9;
                if (fl == 29) { rm->type = 12; g_floor_c148 = 1; }
            }
        boss:
            if (g_floor_w == 3 && g_floor_h == 3 && floor_layout_boss_group(cls, fl) == 15)
                g_floor_c148 = 1;
            if (cls == 3) {
                if (fl != 49) goto rec;
                rm->type = 0; g_floor_c148 = 1;
            }
            if (cls == 4) {
                if (fl != 99) goto rec;
                rm->type = 0; g_floor_c148 = 1;
            }
            if (cls == 5) {
                if (fl == 29) { rm->type = 12; g_floor_c148 = 1; }
                if (fl == 37) { rm->type = 12; g_floor_c148 = 1; }
                if (fl == 45) { rm->type = 0;  g_floor_c148 = 1; }
                if (fl == 58) { rm->type = 0;  g_floor_c148 = 1; }
                if (fl == 59) { rm->type = 12; g_floor_c148 = 1; }
            }
        rec:
            if (rec_flag)
                rm->type = 0;
            rm->pat = k / 5;
            rm->var = var;
        }
    }

    /* ── start / exit / marks / treasure picks ── */
    g_floor_extra_n = 0;
    const int32_t n = g_floor_ncells;
    if (rec_flag) {
        /* no picks */
    } else if (in->is_checkpoint()) {
        if (!in->gate_query()) {
            const int32_t m = n > 10 ? 10 : n;
            for (int32_t i = 0; i != m; i++) {
                g_floor_marks[i][0] = g_floor_list[i][0];
                g_floor_marks[i][1] = g_floor_list[i][1];
            }
            g_scene1_stage_player_default_pos[0] = cell_to_world(g_floor_marks[0][0]);
            g_scene1_stage_player_default_pos[1] = 0.0f;
            g_scene1_stage_player_default_pos[2] = cell_to_world(g_floor_marks[0][1]);
        }
    } else if (n > 2) {
        const int32_t half = n / 2;
        const int32_t ei = (int32_t)((uint32_t)rng_next15() % (uint32_t)half) + half;
        g_floor_marks[1][0] = g_floor_list[ei][0];
        g_floor_marks[1][1] = g_floor_list[ei][1];
        const int32_t si = (int32_t)((uint32_t)rng_next15() % (uint32_t)(g_floor_ncells / 2));
        g_floor_marks[0][0] = g_floor_list[si][0];
        g_floor_marks[0][1] = g_floor_list[si][1];
        g_scene1_stage_player_default_pos[0] = cell_to_world(g_floor_marks[0][0]);
        g_scene1_stage_player_default_pos[1] = 0.0f;
        g_scene1_stage_player_default_pos[2] = cell_to_world(g_floor_marks[0][1]);

        /* the other cells' indices (retail: a 36-dword stack array whose
         * overflow runs into the unused sprintf buffer — same values) */
        int32_t rest[FLOOR_LIST_MAX];
        int32_t nr = 0;
        for (int32_t i = 0; i != n; i++)
            if (i != ei && i != si)
                rest[nr++] = i;
        int32_t extra = (rng_next15() & 1) + n / 8 + 1;
        if (extra > n - 2) {
            extra = n - 2;
            if (extra < 0) extra = 0;
        }
        g_floor_extra_n = extra;
        for (int32_t i = 0; i != n - 2; i++) {
            int32_t r = (int32_t)((uint32_t)rng_next15() % (uint32_t)(g_floor_ncells - 2));
            int32_t t = rest[r];
            rest[r] = rest[i];
            rest[i] = t;
        }
        for (int32_t i = 0; i != extra; i++) {
            g_floor_marks[2 + i][0] = g_floor_list[rest[i]][0];
            g_floor_marks[2 + i][1] = g_floor_list[rest[i]][1];
        }

        /* every other list cell that is not start / exit / marked */
        int32_t np = 0;
        for (int32_t i = 0; i < g_floor_ncells; i += 2) {
            const int32_t cx = g_floor_list[i][0], cy = g_floor_list[i][1];
            int ok = 1;
            if (g_floor_marks[0][0] == cx && g_floor_marks[0][1] == cy) ok = 0;
            if (g_floor_marks[1][0] == cx && g_floor_marks[1][1] == cy) ok = 0;
            for (int32_t e = 0; e != g_floor_extra_n; e++)
                if (g_floor_marks[2 + e][0] == cx && g_floor_marks[2 + e][1] == cy)
                    ok = 0;
            if (!ok || np >= FLOOR_PICK_MAX)
                continue;
            g_floor_pick_flag[np] = rng_next15() & 1;
            g_floor_pick_cell[np][0] = cx;
            g_floor_pick_cell[np][1] = cy;
            g_floor_pick_pos[np][0] = cell_to_world(cx);
            g_floor_pick_pos[np][1] = 0.0f;
            g_floor_pick_pos[np][2] = cell_to_world(cy);
            const int32_t v = g_floor_rooms[cy * FLOOR_GRID_STRIDE + cx].var;
            if (cls == 0) {
                g_floor_pick_type[np] = v < 5 ? 2 : 4;
            } else if (v < 5) {
                g_floor_pick_type[np] = 2;
            } else {
                switch ((uint32_t)rng_next15() % 3u) {
                case 0: g_floor_pick_type[np] = 4; break;
                case 1: g_floor_pick_type[np] = 1; break;
                case 2: g_floor_pick_type[np] = 9; break;
                }
            }
            np++;
        }
    }
    g_floor_rooms_total = g_floor_ncells;
    dbg_row(0x1e, 6, "MOV:%4d", g_floor_ncells);
}
