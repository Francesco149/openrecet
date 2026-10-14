/* P1 floor layout — retail FUN_0047df4d (+0047d622/0047ddc2/0045bc7f) and FUN_0041f252
 * vs src/floor_layout.c / src/actor_spawn.c, called directly on identical globals + LCG.
 * Sweeps every stage class 0..5 × floor 0..99 × dungeon flag × work-record flags × seeds,
 * comparing every table the port writes byte for byte.  The gate predicates go to retail's
 * own FUN_0043195d / FUN_004319d6 on both sides (separately verified in p1b_more).
 *
 *   p1_floor_layout.exe recettear.unpacked.exe [seeds-per-case]
 */
#include "retail_map.h"
#include "actor_spawn.h"
#include "audio_mci.h"
#include "floor_layout.h"
#include "rng.h"
#include "scene1_particles_tick.h"
RETAIL_HOLE_DEFINE
#define G32(va) (*(volatile uint32_t *)(uintptr_t)(va))
#define DAT_RNG 0x6023a0u

scene1_people_entry_t g_scene1_people[SCENE1_PEOPLE_COUNT];

static int ret_43195d(void) { return ((int (__cdecl *)(void))0x43195du)(); }
static int ret_4319d6(void) { return ((int (__cdecl *)(void))0x4319d6u)(); }
typedef struct { const char *name; uint32_t va; void *port; size_t n; } region_t;
static const region_t R[] = {
    { "W",          0x5cc2e8u,  &g_floor_w,             4 },
    { "H",          0x5cc2ecu,  &g_floor_h,             4 },
    { "maze",       0x73e11bcu, g_floor_maze,           sizeof g_floor_maze },
    { "ncells",     0x73e1540u, &g_floor_ncells,        4 },
    { "list",       0x73e008cu, g_floor_list,           sizeof g_floor_list },
    { "grid",       0x73dfd08u, g_floor_grid,           sizeof g_floor_grid },
    { "rooms",      0x73e03acu, g_floor_rooms,          sizeof g_floor_rooms },
    { "origin",     0x438b1f8u, g_floor_origin,         8 },
    { "extra_n",    0x438b430u, &g_floor_extra_n,       4 },
    { "marks",      0x438b434u, g_floor_marks,          sizeof g_floor_marks },
    { "pick_pos",   0x438b200u, g_floor_pick_pos,       sizeof g_floor_pick_pos },
    { "pick_cell",  0x438b2f0u, g_floor_pick_cell,      sizeof g_floor_pick_cell },
    { "pick_type",  0x438b390u, g_floor_pick_type,      sizeof g_floor_pick_type },
    { "pick_flag",  0x438b3e0u, g_floor_pick_flag,      sizeof g_floor_pick_flag },
    { "rooms_total",0x438b4b0u, &g_floor_rooms_total,   4 },
    { "c148",       0x438c148u, &g_floor_c148,          4 },
    { "spawn",      0x438b1ecu, g_scene1_stage_player_default_pos, 12 },
    { "dbgtext",    0x6a47aacu, g_audio_mci_buffer,     AUDIO_MCI_BUFFER_SIZE },
};
#define NR ((int)(sizeof R / sizeof R[0]))

static int32_t g_pal_mode;   /* *DAT_068dd2f0 */

int main(int argc, char **argv)
{
    retail_image_t img;
    if (retail_map(argc > 1 ? argv[1] : "recettear.unpacked.exe", &img, 1)) return 1;
    const int seeds = argc > 2 ? atoi(argv[2]) : 40;
    G32(0x68dd2f0u) = (uint32_t)(uintptr_t)&g_pal_mode;
    G32(0x438b1e0u) = 0;
    const uint32_t rec = 0x44e3798u;
    long n = 0, mis = 0, max_ncells = 0, max_extra = 0, w33 = 0, ckpt = 0;
    uint32_t lcg = 0x1234567u;
    for (int cls = 0; cls <= 5; cls++)
    for (int fl = 0; fl <= 99; fl++)
    for (int dg = 0; dg <= 1; dg++)
    for (int rf = 0; rf <= 2; rf++)
    for (int s = 0; s < seeds; s++) {
        lcg = lcg * 1103515245u + 12345u;
        G32(0x438b4c8u) = (uint32_t)cls; G32(0x438b4ccu) = (uint32_t)fl; G32(0x438b4dcu) = (uint32_t)s;
        g_pal_mode = dg;
        *(volatile uint8_t *)(uintptr_t)(rec + 0x2bce2u) = rf == 1;
        *(volatile uint8_t *)(uintptr_t)(rec + 0x2bcf8u) = rf == 2;
        floor_layout_in_t in = { cls, fl, s, dg, rf == 1, rf == 2, ret_43195d, ret_4319d6 };
        for (int i = 0; i < NR; i++) memcpy(R[i].port, (void *)(uintptr_t)R[i].va, R[i].n);
        G32(DAT_RNG) = lcg;
        ((void (__cdecl *)(void))0x47df4du)();
        rng_seed(lcg);
        floor_layout_generate(&in);
        n++;
        int bad = g_rng_seed != G32(DAT_RNG);
        char b[256]; size_t o = 0; b[0] = 0;
        for (int i = 0; i < NR; i++)
            if (memcmp(R[i].port, (void *)(uintptr_t)R[i].va, R[i].n)) { bad = 1; o += snprintf(b + o, sizeof b - o, "%s ", R[i].name); }
        if (bad && mis++ < 10)
            printf("  floor mismatch cls=%d fl=%d dg=%d rf=%d seed=%08x: rng r=%08x p=%08x %s\n",
                   cls, fl, dg, rf, lcg, G32(DAT_RNG), g_rng_seed, b);
        if (g_floor_ncells > max_ncells) max_ncells = g_floor_ncells;
        if (g_floor_extra_n > max_extra) max_extra = g_floor_extra_n;
        w33 += g_floor_w == 3 && g_floor_h == 3;
        ckpt += ret_43195d() != 0;
    }
    printf("floor_layout: %ld/%ld exact (max ncells %ld, max extra_n %ld, 3x3 %ld, checkpoint %ld)\n",
           n - mis, n, max_ncells, max_extra, w33, ckpt);
    long an = 0, amis = 0;
    for (int s = 0; s < 20000; s++) {
        lcg = lcg * 1103515245u + 12345u;
        G32(DAT_RNG) = lcg;
        ((void (__cdecl *)(void))0x41f252u)();
        rng_seed(lcg);
        actor_spawn_reset();
        int bad = g_rng_seed != G32(DAT_RNG);
        for (int i = 0; i < ACTOR_SPAWN_ORDER_N; i++) bad |= g_actor_spawn_order[i] != (int32_t)G32(0x7c8c38u + 4u * i);
        an++; if (bad && amis++ < 5) printf("  actor_spawn_reset mismatch seed=%08x\n", lcg);
    }
    printf("actor_spawn_reset: %ld/%ld exact\n", an - amis, an);
    return mis || amis;
}
