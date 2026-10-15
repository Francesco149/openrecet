/*
 * dungeon_party.c — see dungeon_party.h.  Float code mirrors the retail
 * disassembly (x87.h): ST0 chains are long double, every fstp m32 is spill32.
 */
#include "dungeon_party.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "actor_table.h"         /* FUN_0041f903, the actor table (hurry-up count) */
#include "audio.h"               /* FUN_00499519 */
#include "audio_mci.h"           /* FUN_00451874 debug text rows */
#include "call_trace.h"
#include "chara_equip.h"         /* DAT_0438b7d8 */
#include "chara_skills.h"        /* DAT_0438b7dc, DAT_0438b7fc */
#include "dungeon_flow.h"
#include "floor_layout.h"        /* g_floor_rooms (DAT_073e03ac), the marks (DAT_0438b434) */
#include "map_collision.h"       /* FUN_00432e50, FUN_00433674 */
#include "news_daily.h"          /* DAT_0438b92c */
#include "party.h"
#include "scene1_overlay.h"
#include "party_move.h"           /* FUN_00483170 */
#include "rng.h"                 /* FUN_00471089 */
#include "save_work.h"
#include "xp_curve.h"             /* FUN_0048a331 */
#include "scene_adv_guild.h"      /* the bag DAT_0438b5ec */
#include "scene1_top_hud.h"       /* DAT_0438b920 */
#include "scene1_records.h"       /* table C DAT_06956cb0 */
#include "table_c.h"              /* DAT_0076b968 */
#include "save_bank.h"           /* FUN_0047a8c0 */
#include "scene1_camera.h"       /* DAT_0438cc68 */
#include "scene1_chr_sprite.h"   /* chr_anim_tick (FUN_00482a71) */
#include "scene1_particles_tick.h" /* g_scene1_camera_yaw (DAT_073de39c) */
#include "scene1_per_frame_open.h" /* FUN_0041331d */
#include "scene1_records_b_spawn.h" /* FUN_0044376a */
#include "scene1_spawn.h"        /* FUN_00447f4f */
#include "sim.h"                 /* DAT_073dddd0 */
#include "scene1_combat_sm.h"    /* DAT_0438b4c8 */
#include "stage_gate.h"          /* FUN_0043195d */
#include "stage_post_load.h"     /* DAT_0438b4d0 */
#include "stage_palette.h"
#include "tables_chara.h"        /* DAT_073ae058 */
#include "x87.h"

/* .rdata m32 operands */
static const float k0 = 0.0f, k0p5 = 0.5f, k1 = 1.0f, k1p5 = 1.5f, k3 = 3.0f, k8 = 8.0f;
static const float k0p01 = 0.009999999776482582f;     /* 0x5193a4 */
static const float k0p1 = 0.10000000149011612f;       /* 0x5193a0 */
static const float k0p15 = 0.15000000596046448f;      /* 0x5198cc */
static const float k0p2 = 0.20000000298023224f;       /* 0x5198d8 */
static const float k0p35 = 0.3499999940395355f;       /* 0x519bc4 */
static const float k0p6 = 0.6000000238418579f;        /* 0x51969c */
static const float k1p1 = 1.100000023841858f;         /* 0x519724 */
static const float k0p04 = 0.03999999910593033f;      /* 0x5198c4 */
static const float k_pi = 3.1415927410125732f;        /* 0x51943c */
static const float k_mpi = -3.1415927410125732f;      /* 0x519a70 */
static const float k_2pi = 6.2831854820251465f;       /* 0x519398 */
static const float k_pi8 = 0.39269909262657166f;      /* 0x519b78 */

/* _sqrt / _atan2 (CRT 0x5031e4 / 0x503dd0): fsqrt / fpatan at CW 0x027F on
 * double arguments, the result left in ST0 */
static long double crt_sqrt(double x) { return (long double)sqrt(x); }
static long double crt_atan2(double y, double x)
{
    unsigned short cw, cw53 = 0x027f;
    long double r;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    __asm__ volatile("fldcw %0" : : "m"(cw53));
    __asm__ volatile("fpatan" : "=t"(r) : "0"((long double)x), "u"((long double)y) : "st(1)");
    __asm__ volatile("fldcw %0" : : "m"(cw));
    return r;
}
/* the facing octant every actor update uses:
 * ftol((a + yaw + π/8) / 2π · 8 + 8) & 7 */
static int32_t octant(float a)
{
    return x87_ftol((((long double)a + g_scene1_camera_yaw + k_pi8) / k_2pi) * k8 + k8) & 7;
}

static const uint8_t *work_rec(void) { return save_work_bank_at(save_work_active_slot()); }
static void party_skill_selected(void);   /* FUN_00489c79 */
static void party_pickup_item(void);      /* FUN_00484e97 */
static void party_skill_direct(void);     /* FUN_00489d52 */
/* the active chara's record: work +0x2ceb0 + 0x6c·DAT_0438b7d8 */
static const uint8_t *chara_rec(void)
{
    const uint8_t *w = work_rec();
    return w ? w + 0x2ceb0 + 0x6c * chara_equip_get_current_chara() : NULL;
}
static int16_t chara_i16(int off)
{
    const uint8_t *c = chara_rec();
    int16_t v = 0;
    if (c) memcpy(&v, c + off, 2);
    return v;
}

/* FUN_004856d7: the active chara has an item with id `id` in its 5 slots
 * (chara +4.., id = slot >> 6) */
static int chara_has_item(int32_t id)
{
    const uint8_t *c = chara_rec();
    if (c == NULL) return 0;
    for (int k = 0; k < 5; k++) {
        int32_t v; memcpy(&v, c + 4 + 4 * k, 4);
        if ((v >> 6) == id) return 1;
    }
    return 0;
}

void party_anim_set(int32_t *rec, int32_t anim)
{
    if (rec[5] != anim) {
        rec[5] = anim;
        rec[4] = 0;
        rec[2] = 0;   /* 0.0f */
        rec[3] = 0;
        rec[0] = anim;
    }
}

/* FUN_0048b6ad: the HP / SP gauges ease 1 % of max per tick toward the values */
static void party_gauge_tick(void)
{
    const float hp = PW(0x56db0bcu).f, sp = PW(0x56db0c0u).f;
    if (PW(0x56db0c4u).f >= hp) {
        if (PW(0x56db0c4u).f <= hp) {
            PW(0x56db0ccu).i = 0;
        } else {
            const float m = spill32((long double)((int32_t)chara_i16(0x3c) + chara_i16(0x3e)));
            PW(0x56db0c4u).f = spill32((long double)PW(0x56db0c4u).f - (long double)m * k0p01);
            if (PW(0x56db0c4u).f < hp) PW(0x56db0c4u).f = hp;
            ++PW(0x56db0ccu).i;
            PW(0x56db0d0u).i = 0;
        }
    } else {
        const float m = spill32((long double)((int32_t)chara_i16(0x3c) + chara_i16(0x3e)));
        PW(0x56db0c4u).f = spill32((long double)m * k0p01 + PW(0x56db0c4u).f);
        if (PW(0x56db0c4u).f > hp) PW(0x56db0c4u).f = hp;
        ++PW(0x56db0ccu).i;
        PW(0x56db0d0u).i = 1;
    }
    if (PW(0x56db0c8u).f < sp) {
        const float m = spill32((long double)((int32_t)chara_i16(0x40) + chara_i16(0x42)));
        PW(0x56db0c8u).f = spill32((long double)m * k0p01 + PW(0x56db0c8u).f);
        if (PW(0x56db0c8u).f > sp) PW(0x56db0c8u).f = sp;
    } else if (PW(0x56db0c8u).f > sp) {
        const float m = spill32((long double)((int32_t)chara_i16(0x40) + chara_i16(0x42)));
        PW(0x56db0c8u).f = spill32((long double)PW(0x56db0c8u).f - (long double)m * k0p01);
        if (PW(0x56db0c8u).f < sp) PW(0x56db0c8u).f = sp;
    }
}

/* FUN_00482ae7: step angle a toward b by at most `step` (both wrapped to ±π) */
static float angle_ease(float a, float b, float step)
{
    while (a < k_mpi) a = spill32((long double)a + k_2pi);
    while (a > k_pi) a = spill32((long double)a - k_2pi);
    while (b < k_mpi) b = spill32((long double)b + k_2pi);
    while (b > k_pi) b = spill32((long double)b - k_2pi);
    const float d = spill32((long double)a - b);
    float ad = spill32(fabsl((long double)d));
    long double r;
    if (ad < step) {
        r = b;
    } else {
        ad = spill32(fabsl((long double)d));
        if ((long double)k_2pi - step < ad) r = b;
        else if (a < b) r = ((long double)b - a >= k_pi) ? (long double)a - step : (long double)a + step;
        else r = (d >= k_pi) ? (long double)a + step : (long double)a - step;
    }
    float v = spill32(r);
    if (v > k_pi) v = spill32((long double)v - k_2pi);
    while (v < k_mpi) v = spill32((long double)v + k_2pi);
    while (v > k_pi) v = spill32((long double)v - k_2pi);
    return v;
}

/* FUN_0048a4d1: Tear (actor 2) spring-follows actor `target` at 1.5 (1.0 during
 * the floor end); her height eases toward ground + 3 + a 0.2 bob */
void party_follow(int target, int set_anim)
{
    const float px0 = g_party.pos[2][0], pz0 = g_party.pos[2][2];
    const uint8_t *rec = work_rec();
    const float tx = g_party.pos[target][0], tz = g_party.pos[target][2];
    float dist = k1p5;
    if (g_floor_end_timer > 0) dist = k1;
    const float dx = spill32((long double)g_party.pos[2][0] - tx);
    const float dz = spill32((long double)g_party.pos[2][2] - tz);
    const float len = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
    float gx = g_party.pos[2][0], gz = g_party.pos[2][2];
    if (len > dist) {
        const float a = spill32(crt_atan2(dx, dz));
        const double ad = a;
        gx = spill32((long double)spill32(x87_crt_sin(ad)) * dist + tx);
        gz = spill32((long double)spill32(x87_crt_cos(ad)) * dist + tz);
    }
    const float vx = spill32(((long double)gx - g_party.pos[2][0]) * k0p15);
    const float vz = spill32(((long double)gz - g_party.pos[2][2]) * k0p15);
    const float v = spill32(crt_sqrt(spill64((long double)vz * vz + (long double)vx * vx)));
    if (v > k0p35) {
        g_party.pos[2][0] = spill32((long double)vx * k0p35 / v + g_party.pos[2][0]);
        g_party.pos[2][2] = spill32((long double)vz * k0p35 / v + g_party.pos[2][2]);
    } else {
        g_party.pos[2][0] = spill32((long double)g_party.pos[2][0] + vx);
        g_party.pos[2][2] = spill32((long double)g_party.pos[2][2] + vz);
    }
    const float base = spill32((long double)g_party.ground_y[0] + k3);
    float b = spill32((long double)PW(0x56db054u).i);
    b = spill32((long double)b * k0p04);
    b = spill32(x87_crt_sin(b));
    b = spill32((long double)b * k0p2 + base);
    g_party.pos[2][1] = spill32(((long double)b - g_party.pos[2][1]) * k0p15 + g_party.pos[2][1]);
    if (!set_anim) return;
    const float mx = spill32((long double)g_party.pos[2][0] - px0);
    const float mz = spill32((long double)g_party.pos[2][2] - pz0);
    float m = spill32((long double)mz * mz + (long double)mx * mx);
    if (m > k0) m = spill32(crt_sqrt(m));
    if (m > k0p01) {
        party_anim_set(g_party.rec[2], 1);
        const float a = spill32(crt_atan2(mx, mz));
        g_party.rec[1][6] = octant(a);   /* DAT_056dab2c — retail writes actor 1's facing here */
        g_party.rec[2][6] = g_party.rec[target][6];
    } else {
        party_anim_set(g_party.rec[2], (rec != NULL && rec[0x2bc6d] != 0) ? 5 : 0);
    }
}

/* FUN_0048a833, the paths a dungeon floor takes (*DAT_068dd2f0 >= 1): the
 * partner (actor 1) follows the player at 3 (1 during the floor end), Tear
 * follows the partner and copies its facing, the wing sparkle, the 3 ground
 * probes. */
void dungeon_party_follow_tick(void)
{
    if (PW(0x56da1c8u).i == 1)
        return;
    const uint8_t *rec = work_rec();
    const int32_t mt = mapcol_maptype();
    const int v31 = mt >= 1;
    int v32 = 1;
    if (mt == 0) {
        v32 = rec != NULL && rec[0x2bc6f] == 0;
        if (rec != NULL && rec[0x2bcd8] != 0) v32 = 1;
    }
    if (v32) {
        /* 0x48a899: actor 1 springs toward a point `reach` from the player (or
         * the cutscene point DAT_0438b78c / b794) */
        int stop = 0;
        const float ox = g_party.pos[1][0], oz = g_party.pos[1][2];
        float tx = g_party.pos[0][0], tz = g_party.pos[0][2];
        float thr = k3, reach = k3;
        if (g_floor_end_timer > 0) { thr = k1; reach = k1; }
        if (g_cut_follow != 0) { tx = g_cut_follow_x; tz = g_cut_follow_z; thr = k1; reach = k0p5; }
        const float dx = spill32((long double)g_party.pos[1][0] - tx);
        const float dz = spill32((long double)g_party.pos[1][2] - tz);
        const float len = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
        float gx = g_party.pos[1][0];
        const float gy = g_party.pos[1][1];
        float gz = g_party.pos[1][2];
        if (len > thr) {
            const float a = spill32(crt_atan2(dx, dz));
            const double ad = a;
            gx = spill32((long double)spill32(x87_crt_sin(ad)) * reach + tx);
            gz = spill32((long double)spill32(x87_crt_cos(ad)) * reach + tz);
        } else if (g_cut_follow != 0) {
            stop = 1;
        }
        int32_t yi = x87_ftol(g_party.pos[1][1]);
        const float yf = spill32((long double)yi);
        if ((long double)g_party.pos[0][1] - k1 > yf) yi = x87_ftol(g_party.pos[0][1]);
        mapcol_hit_t hit;
        const float py = spill32((long double)spill32((long double)yi) + k1);
        if (mapcol_ground(g_party.pos[1][0], py, g_party.pos[1][2], &hit) == 1)
            g_party.pos[1][1] = hit.height;
        const float vx = spill32(((long double)gx - g_party.pos[1][0]) * k0p15);
        const float vy = spill32(((long double)gy - g_party.pos[1][1]) * k0p15);
        const float vz = spill32(((long double)gz - g_party.pos[1][2]) * k0p15);
        const float v = spill32(crt_sqrt(spill64((long double)vz * vz + (long double)vx * vx)));
        if (v > k0p35) {
            g_party.pos[1][0] = spill32((long double)vx * k0p35 / v + g_party.pos[1][0]);
            g_party.pos[1][1] = spill32((long double)g_party.pos[1][1] + vy);
            g_party.pos[1][2] = spill32((long double)vz * k0p35 / v + g_party.pos[1][2]);
        } else {
            g_party.pos[1][0] = spill32((long double)g_party.pos[1][0] + vx);
            g_party.pos[1][1] = spill32((long double)g_party.pos[1][1] + vy);
            g_party.pos[1][2] = spill32((long double)g_party.pos[1][2] + vz);
        }
        float mx = spill32((long double)g_party.pos[1][0] - ox);
        float mz = spill32((long double)g_party.pos[1][2] - oz);
        if (stop) {
            mx = spill32((long double)g_party.pos[0][0] - g_party.pos[1][0]);
            mz = spill32((long double)g_party.pos[0][2] - g_party.pos[1][2]);
        }
        float m = spill32((long double)mz * mz + (long double)mx * mx);
        if (m > k0) m = spill32(crt_sqrt(m));
        if (m > k0p01) {
            party_anim_set(g_party.rec[1], stop ? 0 : 1);
            const float a = spill32(crt_atan2(mx, mz));
            g_party.rec[1][6] = octant(a);
        } else {
            party_anim_set(g_party.rec[1], 0);
        }
    }
    int v39 = PW(0x56db048u).i == 14;
    if (rec != NULL && rec[0x2bc6c] != 0) v39 = 1;
    if (PW(0x56db048u).i == 15) v39 = 1;
    int target = v31;
    if (v39) {
        /* PORT-DEBT(stub, FUN_0048a833): the db048 14 / 15 / work +0x2bc6c arm
         * (Tear beside actor v31 at ±1.3) — not reached on dungeon 0's floors. */
    } else {
        party_follow(target, 1);
    }
    if (v32 && mt >= 1)
        g_party.rec[2][6] = g_party.rec[target][6];
    if (g_floor_intro == 0 && g_party.scale_xz[2] > k0 && g_party.scale_y[2] > k0 &&
        PW(0x56db054u).i % 4 == 0) {
        const float a = spill32((long double)spill32((long double)g_party.rec[2][6]) * k_2pi / k8 - g_scene1_camera_yaw);
        const double ad = a;
        const float s = spill32(x87_crt_sin(ad));
        const float c = spill32(x87_crt_cos(ad));
        const float z = spill32((long double)g_party.pos[2][2] - (long double)c * k0p6);
        const float y = spill32((long double)g_party.pos[2][1] + k1p1);
        const float x = spill32((long double)g_party.pos[2][0] - (long double)s * k0p6);
        scene1_spawn(0, x, y, z, 0x1f, k0p1, 1);
    }
    for (int i = 0; i < 3; i++) {
        if (g_party.chr[i] == -1) continue;
        g_party.ground_y[i] = 0.0f;
        if (mapcol_ground(g_party.pos[i][0], g_party.pos[i][1], g_party.pos[i][2], &g_party.hit[i]) == 1) {
            g_party.ground_y2[i] = g_party.hit[i].height;
            g_party.ground_y[i] = g_party.hit[i].height;
        }
    }
}

/* FUN_00483e7b: the stage's ambient particles around the player (stage record
 * +0x1b24 / +0x1b2c / +0x1b30 / +0x1b34 flags) and the floor event 9 aura */
void party_aura_tick(void)
{
    const uint8_t *st = (const uint8_t *)g_stage_palette;
    int32_t f24 = 0, f2c = 0, f30 = 0, f34 = 0;
    if (st) { memcpy(&f24, st + 6948, 4); memcpy(&f2c, st + 6956, 4); memcpy(&f30, st + 6960, 4); memcpy(&f34, st + 6964, 4); }
    const int32_t t = PW(0x56db054u).i;
    const float *p = g_party.pos[0];
    if (f24 != 0 && t % 100 == 0)
        scene1_spawn(0, p[0], spill32((long double)p[1] + 2.0f), p[2], 24, 1.0f, 2);
    if ((floor_event_active(9) || PW(0x56db100u).i > 0) && t % 4 == 0)
        scene1_spawn(0, p[0], spill32((long double)p[1] + 2.0f), p[2], 79, 1.0f, 1);
    if (f2c != 0 && t % 2 == 0)
        scene1_spawn(0, p[0], spill32((long double)p[1] + 2.0f), p[2], 88, 1.0f, 1);
    if (f34 != 0 && t % 15 == 0)
        scene1_spawn(0, p[0], p[1], p[2], 63, 2.0f, 1);
    if (f30 != 0 && t % 32 == 0)
        scene1_spawn(0, p[0], p[1], p[2], 86, 1.0f, 1);
}

/* FUN_004897c6(&DAT_056da1b8): the shown-equipment bits by char, the status
 * timers, the invincibility count, the three actors' anim ticks */
void party_status_tick(void)
{
    switch (g_party.chr[0]) {
    case 27: PW(0x56db040u).i |= 0xc9; break;
    case 40: PW(0x56db040u).i |= 0xa2; break;
    case 42: break;
    case 28: case 30: PW(0x56db040u).i |= 1; break;
    case 31: PW(0x56db040u).i |= 0x85; break;
    case 41: PW(0x56db040u).i |= 0x90; break;
    default: PW(0x56db040u).i |= 0x20; break;
    }
    if (PW(0x56daeb8u).i > 0) --PW(0x56daeb8u).i;
    if (PW(0x56daeb4u).i > 0) --PW(0x56daeb4u).i;
    if (g_party.rec[0][7] > 0) --g_party.rec[0][7];        /* DAT_056dab04 */
    if (g_dat_0438b8d8 > 0) --g_dat_0438b8d8;
    int ticked = 0;
    if (PW(0x56db100u).i > 0) { --PW(0x56db100u).i; ticked = 1; }
    if (PW(0x56db104u).i > 0) { --PW(0x56db104u).i; ticked = 1; }
    if (PW(0x56db0fcu).i > 0) { --PW(0x56db0fcu).i; ticked = 1; }
    if (ticked) {
        /* PORT-DEBT(stub, FUN_00485712): the status-guard accessory 2504 (the
         * proc chance draws rand) — returns 0 without it */
    }
    /* PORT-DEBT(stub, FUN_004855e2): the accessory-break follow-up
     * (DAT_074b2ec8 > 0) */
    int32_t end = 0;
    for (int k = 0; k < 4; k++) {
        const uint32_t va = 0x56db010u + 4u * (uint32_t)k;
        if (PW(va).i > 0 && --PW(va).i == 0) end = 262912 + 64 * k;
    }
    if (end != 0) {
        /* PORT-DEBT(stub, FUN_00490c78): the buff-end bubble over Tear */
    }
    if (PW(0x56daea0u).i != 0) --PW(0x56daea0u).i;
    if (PW(0x56dae9cu).i != 0) --PW(0x56dae9cu).i;
    if (PW(0x56daea4u).i != 0) --PW(0x56daea4u).i;
    if (PW(0x56db020u).i > 0) --PW(0x56db020u).i;
    {
        char b[64];
        snprintf(b, sizeof b, "MUTEKI %d  ", PW(0x56db0f4u).i);
        audio_mci_record_command(44, 34, b);
    }
    if (PW(0x56daeb0u).i == 0 && PW(0x56db0bcu).f > k0) {
        if (PW(0x56db0f4u).i > -100) --PW(0x56db0f4u).i;
        if (PW(0x56db118u).i > 0) --PW(0x56db118u).i;
    }
    if (PW(0x56dae38u).i > 0) --PW(0x56dae38u).i;
    PW(0x56db06cu).f = 1.0f;
    for (int i = 0; i < 3; i++) {
        if (g_party.chr[i] == -1) continue;
        float rate = 1.0f;
        if (i == 0 && PW(0x56db034u).i == 1) rate = 2.0f;
        if (PW(0x56daea4u).i > 0) rate = 2.0f;
        chr_anim_tick(g_party.rec[i], g_party.chr[i], rate);
    }
}

/* the party resets of FUN_00436f97 (house and dungeon) */
void party_stage_reset(void)
{
    static const float k_spacing = 1.5f;   /* m32 0x5198e0 */
    static const float k_quad = 4.0f;      /* m32 0x51939c */
    for (int i = 0; i < 3; i++) {
        g_party.vel[i][0] = 0.0f;
        g_party.vel[i][1] = 0.0f;
        g_party.vel[i][2] = 0.0f;
        const float fi = spill32((long double)i);
        g_party.pos[i][0] = spill32((long double)g_scene1_stage_player_default_pos[0] - (long double)fi * k_spacing);
        g_party.pos[i][1] = g_scene1_stage_player_default_pos[1];
        g_party.pos[i][2] = g_scene1_stage_player_default_pos[2];
        if (i == 0) {
            for (int k = 0; k < 5; k++) {
                g_party.pos[0][0] = g_scene1_stage_player_default_pos[0];
                g_party.pos[0][1] = g_scene1_stage_player_default_pos[1];
                g_party.pos[0][2] = g_scene1_stage_player_default_pos[2];
                if (k > 0) {
                    float a = spill32((long double)k);
                    a = spill32(((long double)a + k0p5) * k_2pi / k_quad);
                    const double ad = a;
                    float s = spill32(x87_crt_sin(ad));
                    g_party.pos[0][0] = spill32((long double)s + s + g_party.pos[0][0]);
                    s = spill32(x87_crt_cos(ad));
                    g_party.pos[0][2] = spill32((long double)s + s + g_party.pos[0][2]);
                }
                mapcol_hit_t hit;
                const float py = spill32((long double)g_party.pos[0][1] + k0p5);
                if (mapcol_ground(g_party.pos[0][0], py, g_party.pos[0][2], &hit) == 1 &&
                    spill32(fabsl((long double)hit.height)) < k0p5)
                    break;
            }
        }
        PW(0x56db114u).i = -1;
        PW(0x56db05cu).f = k_pi;
        PW(0x56db110u).i = -1;
        g_party.rec[i][6] = 4;
        g_party.rec[i][10] = 0;
        g_party.scale_xz[i] = 1.0f;
        g_party.scale_y[i] = 1.0f;
        g_party.rec[i][8] = 0;
        g_party.rec[i][9] = 0;
        g_party.rec[i][7] = 0;
    }
    for (int k = 0; k < 40; k++) {
        g_party.pos_hist[k][0] = g_party.pos_hist[k][1] = g_party.pos_hist[k][2] = 0.0f;
        g_party.rec_hist[k][7] = g_party.rec_hist[k][8] = g_party.rec_hist[k][9] = 0;
    }
    PW(0x56daae0u).i = 0;
    for (int k = 0; k < PARTY_RECS; k++) g_party.recs[k][0] = -1;
    for (int k = 0; k < 5; k++) { g_party.trail[k][14] = 0; g_party.burst[k][14] = 0; }
    static const uint32_t zero_i[] = {
        0x56dae14, 0x56dae30, 0x56dae38, 0x56dae34, 0x56db064, 0x56db020, 0x56daea4, 0x56db034,
        0x56db038, 0x56db03c, 0x56db040, 0x56db044, 0x56db008, 0x56db00c, 0x56db010, 0x56db014,
        0x56db018, 0x56db01c, 0x56db000, 0x56db0dc, 0x56db0e4, 0x56db0ec, 0x56db0f0, 0x56da1c0,
        0x56db070, 0x56dafe4, 0x56dafe8, 0x56dafec, 0x56daff0, 0x56daff4, 0x56daff8, 0x56daffc,
        0x56da1c8, 0x56da1bc, 0x56da1b8, 0x56da1c4, 0x56dae9c, 0x56daea0, 0x56daea8, 0x56daeb0,
        0x56daeb4, 0x56daeb8, 0x56db118, 0x56dae8c, 0x56dae90, 0x56db04c, 0x56db054, 0x56db048,
        0x56db0f4, 0x56db0f8, 0x56db0fc, 0x56db100, 0x56db104, 0x56db108, 0x56dae3c, 0x56dae84,
        0x56dae88,
    };
    for (size_t k = 0; k < sizeof zero_i / sizeof zero_i[0]; k++) PW(zero_i[k]).i = 0;
    static const uint32_t zero_f[] = {
        0x56db024, 0x56db028, 0x56db02c, 0x56db030, 0x56db0e8, 0x56db060, 0x56dae94,
    };
    for (size_t k = 0; k < sizeof zero_f / sizeof zero_f[0]; k++) PW(zero_f[k]).f = 0.0f;
    for (int i = 0; i < 3; i++) { g_party.ground_y[i] = 0.0f; g_party.ground_y2[i] = 0.0f; }
    PW(0x56db058u).i = -1;
    PW(0x56db068u).f = 1.0f;
    PW(0x56daae4u).i = 255;
    PW(0x56db0e0u).i = 1000;
    PW(0x56db05cu).f = k_pi;
    PW(0x56dae40u).i = -1;
    PW(0x56db0c4u).f = PW(0x56db0bcu).f;
    PW(0x56db0c8u).f = PW(0x56db0c0u).f;
}

/* FUN_0048407f, *DAT_068dd2f0 >= 1: the actors hold their idle anims during
 * an event; the anim ticks; Tear follows the partner (no anim); the wing
 * sparkle; FUN_004708f7; db054++ */
void dungeon_event_actor_tick(void)
{
    CALL_TRACE_ENTER(0x48407fu);
    party_aura_tick();   /* FUN_00483e7b */
    for (int i = 0; i < 3; i++) {
        if (g_party.chr[i] == -1) continue;
        if (i != 0) party_anim_set(g_party.rec[i], 0);
        else if (PW(0x56db048u).i != 1) party_anim_set(g_party.rec[0], 0);
    }
    /* PORT-DEBT(stub, FUN_0044f078): dungeon 4 floor 99's event helper */
    for (int i = 0; i < 3; i++)
        if (g_party.chr[i] != -1)
            chr_anim_tick(g_party.rec[i], g_party.chr[i], 1.0f);
    if (PW(0x56da1c8u).i != 1)
        party_follow(mapcol_maptype() >= 1, 0);
    if (g_party.scale_xz[2] > k0 && g_party.scale_y[2] > k0 && PW(0x56db054u).i % 4 == 0) {
        const float a = spill32((long double)spill32((long double)g_party.rec[2][6]) * k_2pi / k8 - g_scene1_camera_yaw);
        const double ad = a;
        const float s = spill32(x87_crt_sin(ad));
        const float c = spill32(x87_crt_cos(ad));
        const float z = spill32((long double)g_party.pos[2][2] - (long double)c * k0p6);
        const float y = spill32((long double)g_party.pos[2][1] + k1p1);
        const float x = spill32((long double)g_party.pos[2][0] - (long double)s * k0p6);
        scene1_spawn(0, x, y, z, 0x1f, k0p1, 1);
    }
    /* PORT-DEBT(stub, FUN_004708f7): the event-window pair (chars 0x2f / 0x30)
     * of work flag +0x2bcd8 — house-only */
    ++PW(0x56db054u).i;
}

/* FUN_00489db8: the facing from the pad (8 ways, camera-relative); a dash
 * keeps its direction (db03c) unless the char steers (db040 & 1) */
static void party_facing_from_pad(void)
{
    static const float k_a45 = 0.7853981852531433f, k_a315 = 5.4977874755859375f;
    static const float k_a135 = 2.356194496154785f, k_a225 = 3.9269909858703613f;
    static const float k_a90 = 1.5707963705062866f, k_a270 = 4.71238899230957f;
    float a;
    if (PW(0x56db034u).i == 0 || (PW(0x56db040u).i & 1)) {
        const uint16_t c = g_sim_buttons[0].cur;
        if (c & 4) {
            if (c & 2) a = k_a45;
            else if (c & 1) a = k_a315;
            else { PW(0x56db05cu).f = spill32(-(long double)g_scene1_camera_yaw); return; }
        } else if (c & 8) {
            if (c & 2) a = k_a135;
            else if (c & 1) a = k_a225;
            else a = k_pi;
        } else if (c & 2) a = k_a90;
        else if (c & 1) a = k_a270;
        else return;
    } else {
        switch (PW(0x56db03cu).i) {
        case 4: PW(0x56db05cu).f = spill32(-(long double)g_scene1_camera_yaw); return;
        case 8: a = k_pi; break;
        case 2: a = k_a90; break;
        case 1: a = k_a270; break;
        default: return;
        }
    }
    PW(0x56db05cu).f = spill32((long double)a - g_scene1_camera_yaw);
}

/* FUN_00489e66: the free walk — the camera eases to db060, the facing from
 * the pad, the dash (chars with db040 & 0x80), the walk impulse along the
 * facing, the walk anim */
static void party_walk(void)
{
    g_scene1_camera_yaw = angle_ease(g_scene1_camera_yaw, PW(0x56db060u).f, 0.06283185631036758f);
    party_facing_from_pad();
    int moving = 0;
    if (mapcol_maptype() == 0 || !(PW(0x56db040u).i & 0x80)) {
        PW(0x56db034u).i = 0;
    } else {
        /* PORT-DEBT(stub, FUN_00489e66): the dash (double-tap, db034 / db03c /
         * db038, FUN_0044376a 138 / 139) of the chars with db040 & 0x80 */
    }
    if (g_sim_buttons[0].cur & 0xf) {
        float acc = k0p1;
        if ((floor_event_active(9) || PW(0x56db100u).i > 0) && !chara_has_item(0x96b))
            acc = 0.019999999552965164f;
        if (PW(0x56db020u).i > 0) acc = 0.019999999552965164f;
        if (g_party.pos[0][1] > g_party.ground_y[0])
            acc = 0.029999999329447746f;
        else if (spill32(fabsl((long double)g_party.hit[0].n[1])) < k0p5)
            acc = 0.0010000000474974513f;
        const double f = PW(0x56db05cu).f;
        g_party.vel[0][0] = spill32((long double)spill32(x87_crt_sin(f)) * acc + g_party.vel[0][0]);
        g_party.vel[0][2] = spill32((long double)spill32(x87_crt_cos(f)) * acc + g_party.vel[0][2]);
        moving = 1;
    }
    if (PW(0x56db020u).i > 0) {
        const double f = PW(0x56db024u).f;
        g_party.vel[0][0] = spill32((long double)spill32(x87_crt_sin(f)) * 0.029999999329447746f + g_party.vel[0][0]);
        g_party.vel[0][2] = spill32((long double)spill32(x87_crt_cos(f)) * 0.029999999329447746f + g_party.vel[0][2]);
    }
    if (g_party.pos[0][1] > g_party.ground_y[0]) {
        moving = 0;
    } else if (g_party.hit[0].height != -100.0f) {
        const mapcol_hit_t *h = &g_party.hit[0];
        int32_t ty = 0;
        if (h->slot >= 0 && h->slot < MAPCOL_SLOTS && g_mapcol_slot[h->slot].tris != NULL &&
            h->tri >= 0 && h->tri < g_mapcol_slot[h->slot].cap)
            ty = g_mapcol_slot[h->slot].tris[h->tri].type;
        if (ty == 1 || ty == 2) moving = 0;
        if (ty == 0 && spill32(fabsl((long double)h->n[1])) < 0.05000000074505806f) moving = 0;
    }
    party_anim_set(g_party.rec[0], moving);
    if (g_party.pos[0][1] == g_party.ground_y[0]) PW(0x56da1c0u).i = 0;
    else if (PW(0x56da1c0u).i > 0) ++PW(0x56da1c0u).i;
}

/* FUN_0048d5d6 on a floor: the special gauge (char 42), skill select (button
 * 0x40), the skills (0x3e00 → FUN_00489d52), the skill button (0x20 →
 * FUN_00489c79), the attack (A = 0x10 → db048 2) */
static void party_attack_input(void)
{
    const int32_t chr = g_party.chr[0];
    PW(0x56db0e4u).i = chr == 42 ? 10 : 0;
    const int32_t g = PW(0x56db0e4u).i;
    int attack_ok = 0;
    if (PW(0x56db0e0u).i >= 10 * g) PW(0x56db0e0u).i = 10 * g;
    if (g == 0) {
        attack_ok = 1;
    } else {
        /* PORT-DEBT(stub, FUN_0048d5d6): char 42's gauge refill + the random
         * charm pick (j__rand % 13) */
    }
    const uint16_t pr = g_sim_buttons[0].pressed;
    const int a_up = chr == 30 ? !(g_sim_buttons[0].cur & 0x10) : !(pr & 0x10);
    if (a_up) {
        if (pr & 0x40) {
            /* PORT-DEBT(stub, FUN_0048d5d6): the skill cycle (DAT_0438b7dc, SE 326) */
        } else if (pr & 0x3e00) {
            party_skill_direct();                       /* FUN_00489d52 */
        } else if ((pr & 0x20) && PW(0x56dae9cu).i == 0 && PW(0x56daea4u).i == 0) {
            party_skill_selected();                     /* FUN_00489c79 */
        }
    } else {
        if (PW(0x56dae40u).i != -1) party_pickup_item();   /* FUN_00484e97 */
        if (attack_ok) {
            if (!((floor_event_active(9) || PW(0x56db100u).i > 0) && !chara_has_item(0x96b)) && chr != 30) {
                g_party.vel[0][0] = 0.0f;
                g_party.vel[0][2] = 0.0f;
            }
            PW(0x56db064u).i = 0;
            PW(0x56db048u).i = 2;            /* the attack state */
            PW(0x56db04cu).i = 0;
            PW(0x56da1b8u).i = 0;
            PW(0x56daff8u).i = 0;
            PW(0x56daff4u).i = 0;
        }
    }
}

/* the accessory notice of FUN_00485712 (DAT_0438b758..b76c) and the broken
 * accessory (DAT_074b2ec8 / cc) */
int32_t g_notice_item, g_notice_kind, g_notice_side, g_notice_on, g_notice_ticks, g_notice_b76c;
int32_t g_broke_on, g_broke_item;

/* FUN_00485712: the active chara wears item `id`? — then the proc: a
 * 1-in-`brk` chance it breaks (into 0x27140), the burst 9, SE 381, the notice,
 * a hit-stop of `stop`; 1.  Not worn: 0 (no draw). */
int party_accessory_proc(int32_t id, uint32_t brk, int32_t stop)
{
    uint8_t *m = save_work_bank_at(save_work_active_slot());
    if (m == NULL) return 0;
    m += 0x2ceb0 + 0x6c * chara_equip_get_current_chara();
    int k = 0;
    int32_t v;
    for (;;) {
        memcpy(&v, m + 4 + 4 * k, 4);
        if ((v >> 6) == id) break;
        if (++k == 5) return 0;
    }
    const int broke = rng_next15() % brk == 0;
    scene1_pfo_table_a_alloc_passthrough((int)(intptr_t)&g_party, 0.0f, 2.0f, 0.0f, 9, 1.2000000476837158f, -1, 0, 1);
    audio_play_se_by_id(381);
    g_notice_on = 1;
    g_notice_item = v;
    g_notice_kind = 2;
    g_notice_b76c = 4;
    g_notice_ticks = 90;
    /* PORT-DEBT(render-coupled, FUN_00490c78): the notice's side is Tear's
     * screen x < 320 */
    g_notice_side = 1;
    g_scene1_combat_dat_0438bed8 = stop;
    if (broke) {
        g_broke_on = 1;
        g_broke_item = v;
        const int32_t b = 160064;
        memcpy(m + 4 + 4 * k, &b, 4);
    }
    return 1;
}

/* FUN_00489b2c: spend `cost` SP (halved by floor event 1 unless char 42;
 * the half-cost accessory 2507 on a second halving; the refill accessory 2506
 * below half max) — 0 when short */
static int party_spend_sp(int32_t cost)
{
    if (g_party.chr[0] != 42 && floor_event_active(1)) cost /= 2;
    if (cost < 1) cost = 1;
    int32_t half = cost / 2;
    if (half < 1) half = 1;
    const float hf = spill32((long double)half);
    float sp;
    if (!(PW(0x56db0c0u).f < hf) && g_party.chr[0] != 42 && party_accessory_proc(2507, 10, 10)) {
        sp = spill32((long double)PW(0x56db0c0u).f - hf);
    } else {
        const float cf = spill32((long double)cost);
        if (PW(0x56db0c0u).f < cf) return 0;
        sp = spill32((long double)PW(0x56db0c0u).f - cf);
    }
    PW(0x56db0d4u).i = 60;
    PW(0x56db0c0u).f = sp;
    const uint8_t *m = work_rec() + 0x2ceb0 + 0x6c * chara_equip_get_current_chara();
    int16_t a, b;
    memcpy(&a, m + 0x40, 2);
    memcpy(&b, m + 0x42, 2);
    const float thr = spill32((long double)((b + a) / 2));
    if (PW(0x56db0c0u).f < thr && party_accessory_proc(2506, 5, 30)) {
        int32_t add = (b + a) / 4;
        if (add > 100) add = 100;
        PW(0x56db0c0u).f = spill32((long double)spill32((long double)add) + PW(0x56db0c0u).f);
    }
    return 1;
}

/* FUN_00489c79: the selected skill (slot 0x56db0d8): its SP (DAT_005ce520,
 * char 42 the max SP, 1 with work +0x2bc48) → state 4..8 by the skill */
static void party_skill_selected(void)
{
    static const int32_t k_sp_cost[8][5] = {
        { 5, 5, 5, 5, 0 }, { 5, 5, 5, 5, 0 }, { 5, 5, 5, 25, 5 }, { 4, 5, 8, 10, 0 },
        { 5, 20, 5, 5, 0 }, { 5, 8, 10, 5, 0 }, { 10, 20, 30, 5, 0 }, { 5, 5, 5, 5, 0 },
    };
    const int32_t c = chara_equip_get_current_chara();
    const int32_t slot = PW(0x56db0d8u).i;
    const uint8_t *w = work_rec();
    int32_t cost = (c >= 0 && c < 8 && slot >= 0 && slot < 5) ? k_sp_cost[c][slot] : 0;
    if (g_party.chr[0] == 42) {
        int16_t a, b;
        memcpy(&a, w + 0x2ceb0 + 0x6c * c + 0x40, 2);
        memcpy(&b, w + 0x2ceb0 + 0x6c * c + 0x42, 2);
        cost = a + b;
    }
    if (w[0x2bc48] != 0) cost = 1;
    if (!party_spend_sp(cost)) return;
    PW(0x56db064u).i = 0;
    const int32_t sk = chara_skills_get_slot(c, slot);
    switch (sk) {
    case 0: PW(0x56db048u).i = 4; break;
    case 1: PW(0x56db048u).i = 5; break;
    case 2: PW(0x56db048u).i = 6; break;
    case 3: PW(0x56db048u).i = 7; break;
    case 4: PW(0x56db048u).i = 8; break;
    default: break;
    }
    PW(0x56db04cu).i = 0;
}

/* FUN_00489d52: the direct skill keys (0x200..0x2000 → slots 0..4) */
static void party_skill_direct(void)
{
    if (PW(0x56dae9cu).i != 0 || PW(0x56daea4u).i != 0) return;
    const uint16_t pr = g_sim_buttons[0].pressed;
    int32_t k = 0;
    if (pr & 0x2000) k = 4;
    if (pr & 0x1000) k = 3;
    if (pr & 0x800) k = 2;
    if (pr & 0x400) k = 1;
    if (pr & 0x200) k = 0;
    if (chara_skills_get_count(chara_equip_get_current_chara()) > k) {
        PW(0x56db0d8u).i = k;
        party_skill_selected();
    }
}

/* FUN_00499fda: a bag of 6 voice picks (0, 1 three times each);
 * DAT_0964326c / DAT_096432d8 */
static int32_t s_voice_bag2[6], s_voice_bag2_n;
static int32_t voice_bag2(void)
{
    if (s_voice_bag2_n == 0) {
        for (int k = 0; k < 6; k++) s_voice_bag2[k] = k % 2;
        for (int k = 0; k < 6; k++) {
            const unsigned r = (unsigned)rng_next15() % 6u;
            const int32_t t = s_voice_bag2[r];
            s_voice_bag2[r] = s_voice_bag2[k];
            s_voice_bag2[k] = t;
        }
    }
    const int32_t v = s_voice_bag2[s_voice_bag2_n++];
    if (s_voice_bag2_n == 6) s_voice_bag2_n = 0;
    return v;
}

/* FUN_00496fca: Louie's skill state 4 — the spin: the voice, the record kind
 * 34 and a hop at 5, 15 ticks of 0.419-rad turns, the end at 35 */
static void party_skill_louie(void)
{
    if (++PW(0x56db04cu).i == 1) {
        party_anim_set(g_party.rec[0], 0);
        party_anim_set(g_party.rec[0], 3);
        const int32_t v = voice_bag2();
        if (v == 0) audio_play_se_file("bin/se/02li/action/02li_06.bin");
        else if (v == 1) audio_play_se_file("bin/se/02li/action/02li_07.bin");
        if (PW(0x56db0f4u).i < 30) PW(0x56db0f4u).i = 30;
    }
    if (PW(0x56db04cu).i == 5) {
        audio_play_se_by_id(692);
        scene1_record_b_spawn_entity(&g_party, 34, -1);
        g_party.vel[0][1] = 0.30000001192092896f;
    }
    const int32_t t = PW(0x56db04cu).i;
    if (t >= 5 && t < 20)
        PW(0x56db05cu).f = spill32((long double)PW(0x56db05cu).f + 0.41887903213500977f);
    if (t >= 35) {
        PW(0x56db04cu).i = 0;
        PW(0x56db048u).i = 0;
    }
}

/* DAT_00648290 / 648294 / 648250: the "got an item" notice (FUN_0040c923) */
int32_t g_got_item, g_got_item_on, g_got_item_bubble;

/* FUN_00484da3: the item into the first free bag slot of `cap` (0 = full) */
int party_bag_add(int32_t item, int32_t cap)
{
    int *bag = adv_guild_bag();
    if (cap == 0) return 0;
    int k = 0;
    while (bag[k] != -1) if (++k == cap) return 0;
    bag[k] = item;
    return 1;
}

/* FUN_00484e45: the bag is full (its items + the table-C items flying to it) */
int party_bag_full(void)
{
    const int *bag = adv_guild_bag();
    const int cap = adv_guild_bag_size();
    int n = 0;
    for (int k = 0; k < cap; k++) if (bag[k] != -1) n++;
    for (int k = 0; k < g_table_c_bound; k++) {
        const int32_t *r = &g_scene1_records_c[k * SCENE1_RECORDS_C_STRIDE];
        if (r[10] != -1 && r[36] != 1 && r[16] == 2) n++;
    }
    return n == cap;
}

/* FUN_00484dd1: an item (table-C kind >= 7: id + 7, the rare flag, the grade)
 * into the bag: the pickup count, the voice, the notice, SE 332 */
void party_get_item(int32_t kind, int32_t rare, int32_t grade)
{
    if (kind < 7) return;
    const int32_t item = grade | (rare << 4) | ((kind - 7) << 6);
    uint8_t *w = save_work_bank_at(save_work_active_slot());
    if (w) { int32_t n; memcpy(&n, w + 0x2c3e0, 4); ++n; memcpy(w + 0x2c3e0, &n, 4); }
    audio_play_se_file((rng_next15() & 1) ? "bin/se/00re/action/00re_btl05a.bin"
                                          : "bin/se/00re/action/00re_btl05b.bin");
    g_got_item = item;                       /* FUN_0040c923 */
    g_got_item_on = 1;
    g_got_item_bubble = 1;
    if (party_bag_add(item, adv_guild_bag_size())) audio_play_se_by_id(332);
}

/* FUN_00484e97: pick up table-C item 0x56dae40 — a gem (kind < 4) into the
 * 6-slot ring 0x56dae44.., else into the bag (or the "bag full" notice) */
static void party_pickup_item(void)
{
    int32_t *r = &g_scene1_records_c[PW(0x56dae40u).i * SCENE1_RECORDS_C_STRIDE];
    const int32_t kind = r[10];
    if (kind < 4) {
        for (uint32_t va = 0x56dae58u; va != 0x56dae44u; va -= 4) PW(va).i = PW(va - 4).i;
        PW(0x56dae44u).i = kind;
        audio_play_se_by_id(351);
        r[10] = -1;
        return;
    }
    if (!party_bag_full()) {
        party_get_item(r[10], r[14], r[15]);
        r[10] = -1;
        return;
    }
    g_notice_on = 1;
    g_notice_item = 0;
    g_notice_kind = 2;
    g_notice_b76c = 1;
    g_notice_ticks = 90;
    /* PORT-DEBT(render-coupled, FUN_00490c78): the side is Tear's screen x < 320 */
    g_notice_side = 1;
}

/* the level-up burst: FUN_004147d5(party block, 0, 0.5, 0, 33, 0.3, -1, 0, 1) */
static void level_burst(void)
{
    scene1_overlay_spawn(&g_party, 0.0f, 0.5f, 0.0f, 33, 0.30000001192092896f, -1, 0, 1, 0);
}

/* FUN_0048a383: a level-up of party member `m` (at 98: the EXP is capped) */
static void party_level_up(uint8_t *m)
{
    int32_t lv;
    memcpy(&lv, m, 4);
    int16_t a, b, c, d;
    if (lv < 98) {
        int32_t nx;
        memcpy(&nx, m + 0x4c, 4);
        ++lv;
        memcpy(m, &lv, 4);
        memcpy(m + 0x48, &nx, 4);
        const int32_t t = xp_curve_threshold(lv + 1);
        memcpy(m + 0x4c, &t, 4);
        scene1_top_hud_levelup_arm();                       /* DAT_0438b920 = 1 */
        save_bank_apply_chara_interp_at(save_work_bank_at(save_work_active_slot()));   /* FUN_0047a8c0 */
        memcpy(&a, m + 0x3c, 2); memcpy(&b, m + 0x3e, 2);
        memcpy(&c, m + 0x40, 2); memcpy(&d, m + 0x42, 2);
        PW(0x56db0bcu).f = spill32((long double)(b + a));
        PW(0x56db0c0u).f = spill32((long double)(d + c));
        chara_equip_recompute_aggregate();                  /* FUN_004844ef */
        audio_play_se_file("bin/se/00re/system/00re_sys03b.bin");
        chara_skills_init_at_stage_load();                  /* FUN_004360b6 */
        level_burst();                                      /* FUN_004147d5 */
        PW(0x56daeb8u).i = 20;
    } else {
        memcpy(&a, m + 0x3c, 2); memcpy(&b, m + 0x3e, 2);
        memcpy(&c, m + 0x40, 2); memcpy(&d, m + 0x42, 2);
        PW(0x56db0bcu).f = spill32((long double)(b + a));
        PW(0x56db0c0u).f = spill32((long double)(d + c));
        level_burst();
        PW(0x56daeb8u).i = 20;
        memcpy(m + 0x44, m + 0x48, 4);
    }
    chara_skills_init_at_stage_load();
}

/* FUN_0048cbf6: table C around the player — EXP orbs (kinds 0..3) older than
 * 30 ticks within 1.5 are taken (+10, +1 for a hit's orb, +100 with work
 * +0x2bc55; doubled on floor event 2); the nearest item marks 0x56dae40 (taken
 * at once with the auto-pickup 0x56db084) */
static void party_pickups(void)
{
    static const float k10 = 10.0f, k1p5 = 1.5f;
    float best = k10;
    PW(0x56dae40u).i = -1;
    uint8_t *w = save_work_bank_at(save_work_active_slot());
    uint8_t *m = w ? w + 0x2ceb0 + 0x6c * chara_equip_get_current_chara() : NULL;
    int got = 0;
    for (int k = 0; k < g_table_c_bound; k++) {          /* DAT_0076b968 */
        int32_t *r = &g_scene1_records_c[k * SCENE1_RECORDS_C_STRIDE];
        if (r[10] == -1 || r[16] == 2) continue;
        if (!((uint32_t)r[10] >= 4u || PW(0x56dae58u).i == -1)) continue;
        float fx, fy, fz;
        memcpy(&fx, &r[0], 4); memcpy(&fy, &r[1], 4); memcpy(&fz, &r[2], 4);
        const float dx = spill32((long double)g_party.pos[0][0] - fx);
        const float dy = spill32((long double)g_party.pos[0][1] - fy);
        const float dz = spill32((long double)g_party.pos[0][2] - fz);
        const float d = spill32((long double)sqrt(spill64(((long double)dz * dz + (long double)dy * dy) + (long double)dx * dx)));
        if (!(d < k1p5) || !(d < best)) continue;
        if (r[10] <= 3) {
            if (r[11] > 30) {
                audio_play_se_by_id(332);
                r[10] = -1;
                if (PW(0x56db0bcu).f > 0.0f && m) {
                    int32_t x;
                    memcpy(&x, m + 0x44, 4);
                    if (w[0x2bc55] != 0) x += 100;
                    else if (r[17] == 1) { ++x; if (floor_event_active(2)) ++x; }
                    else { x += 10; if (floor_event_active(2)) x += 10; }
                    memcpy(m + 0x44, &x, 4);
                    got = 1;
                }
            }
        } else {
            best = d;
            PW(0x56dae40u).i = k;
        }
    }
    if (PW(0x56dae40u).i != -1 && PW(0x56db084u).i != 0) {
        party_pickup_item();
        PW(0x56dae40u).i = -1;
    }
    if (got && m) {
        int32_t x, nx;
        memcpy(&x, m + 0x44, 4);
        memcpy(&nx, m + 0x4c, 4);
        if (x >= nx) party_level_up(m);
    }
}

/* FUN_0049a052: a bag of 9 voice picks (0, 1, 2 three times each), shuffled
 * when dealt from the top; DAT_096432b4 / DAT_096432dc */
static int32_t s_voice_bag[9], s_voice_bag_n;
static int32_t voice_bag3(void)
{
    if (s_voice_bag_n == 0) {
        for (int k = 0; k < 9; k++) s_voice_bag[k] = k % 3;
        for (int k = 0; k < 9; k++) {
            const unsigned r = (unsigned)rng_next15() % 9u;
            const int32_t t = s_voice_bag[r];
            s_voice_bag[r] = s_voice_bag[k];
            s_voice_bag[k] = t;
        }
    }
    const int32_t v = s_voice_bag[s_voice_bag_n++];
    if (s_voice_bag_n == 9) s_voice_bag_n = 0;
    return v;
}

/* FUN_0049831c: Louie's swing (attack state 2 of the default char): the
 * crit roll at 1, the crit flash at 3, the voice + the hit record at 4
 * (FUN_0044376a kind 2, 84 on a crit), the skill keys from 5, the end at
 * 25 (30 after a crit roll) */
static void party_attack_louie(void)
{
    static const char *const k_voice[2][3] = {
        { "bin/se/02li/action/02li_01.bin", "bin/se/02li/action/02li_02.bin",
          "bin/se/02li/action/02li_03.bin" },
        { "bin/se/02li/action/02li_04.bin", "bin/se/02li/action/02li_05a.bin",
          "bin/se/02li/action/02li_05b.bin" },
    };
    static const float k1p5 = 1.5f;
    if (++PW(0x56db04cu).i == 1) {
        party_anim_set(g_party.rec[0], 0);
        party_anim_set(g_party.rec[0], 3);
    }
    if (PW(0x56db04cu).i == 1) {
        PW(0x56daff8u).i = 1;
        int32_t c = chara_equip_get_current_chara();
        if (c < 0 || c >= CHARA_COUNT) c = 0;
        if (rng_next_unit() < g_chara[c].crit_rate) PW(0x56db064u).i = 1;
    }
    if (PW(0x56db04cu).i == 3 && PW(0x56db064u).i != 0) {
        if (PW(0x56db0f4u).i < 8) PW(0x56db0f4u).i = 8;
        g_scene1_combat_dat_0438bed8 = 30;
        audio_play_se_by_id(342);
        scene1_spawn(0, g_party.pos[0][0], spill32((long double)g_party.pos[0][1] + k1p5),
                     g_party.pos[0][2], 44, 1.0f, 1);
    }
    if (PW(0x56db04cu).i == 4) {
        audio_play_se_by_id(343);
        const int crit = PW(0x56db064u).i != 0;
        const int32_t v = voice_bag3();
        if (v >= 0 && v <= 2) audio_play_se_file(k_voice[crit][v]);
        scene1_record_b_spawn_entity(&g_party, crit ? 84 : 2, -1);
    }
    if (PW(0x56db04cu).i >= 5 && (PW(0x56da1b8u).i & 2)) {
        const uint16_t pr = g_sim_buttons[0].pressed;
        if (pr & 0x3e00) party_skill_direct();          /* FUN_00489d52 */
        else if (pr & 0x20) party_skill_selected();     /* FUN_00489c79 */
    }
    if (PW(0x56db04cu).i >= (PW(0x56daff8u).i != 0 ? 30 : 25)) {
        PW(0x56db04cu).i = 0;
        PW(0x56db048u).i = 0;
    }
}

/* FUN_0048cdcc: the player state machine */
static void party_state_tick(void)
{
    const uint8_t *rec = work_rec();
    int idle = spill32(fabsl((long double)g_party.vel[0][0])) < k0p01 &&
               spill32(fabsl((long double)g_party.vel[0][2])) < k0p01;
    if (idle && PW(0x56db048u).i == 0) {
        if (PW(0x56db0d4u).i != 0) --PW(0x56db0d4u).i;
    } else {
        PW(0x56db0d4u).i = 60;
    }
    if (PW(0x56db0d4u).i == 0) {
        const float m = spill32((long double)((int32_t)chara_i16(0x40) + chara_i16(0x42)));
        if (PW(0x56db0c0u).f > m) PW(0x56db0c0u).f = m;   /* SP stays at most max */
    }
    if (PW(0x56dae30u).i > 0) {
        if (PW(0x56dae30u).i >= 60) PW(0x56dae30u).i = 0;
        else ++PW(0x56dae30u).i;
    }
    if (PW(0x56db048u).i != 0) {
        PW(0x56db034u).i = 0;
        PW(0x56db038u).i = 0;
        PW(0x56db03cu).i = 0;
    }
    if (PW(0x56db034u).i != 0) {
        /* PORT-DEBT(stub, FUN_0048cdcc): the dash's wall bonk (a ray 1.4 ahead) */
    } else if (PW(0x56db040u).i & 0x10) {
        PW(0x56daae4u).i += 64;
        if (PW(0x56daae4u).i > 255) PW(0x56daae4u).i = 255;
    }
    (void)rec;
    switch (PW(0x56db048u).i) {
    case 0:
        party_walk();                        /* FUN_00489e66 */
        if (mapcol_maptype() >= 1) party_attack_input();   /* FUN_0048d5d6 */
        if (PW(0x56dae30u).i == 0) {
            if (PW(0x56dae34u).i > 30) PW(0x56dae30u).i = (g_sim_buttons[0].pressed & 0xf) != 0;
            if (g_sim_buttons[0].cur != 0) PW(0x56dae34u).i = 0;
            else ++PW(0x56dae34u).i;
        }
        break;
    case 15:
        party_anim_set(g_party.rec[0], 6);
        break;
    case 14:
        ++PW(0x56db04cu).i;
        PW(0x56db05cu).f = g_party.pos[0][0] <= g_party.pos[2][0] ? 1.5707963705062866f : -1.5707963705062866f;
        break;
    case 12:
        party_anim_set(g_party.rec[0], 4);
        if (++PW(0x56db04cu).i >= 26) { PW(0x56db04cu).i = 0; PW(0x56db048u).i = 0; }
        break;
    case 1:
        PW(0x56da1bcu).i = 0;
        if (PW(0x56daeb0u).i > 0) { --PW(0x56daeb0u).i; break; }
        if (PW(0x56db0bcu).f <= k0) break;
        PW(0x56db04cu).i = 0;
        PW(0x56db048u).i = 0;
        break;
    case 11:
        if (++PW(0x56db04cu).i == 6 && PW(0x56dae40u).i != -1) party_pickup_item();
        if (PW(0x56db04cu).i >= 12) { PW(0x56db04cu).i = 0; PW(0x56db048u).i = 0; }
        break;
    case 2:
        switch (g_party.chr[0]) {
        case 27: case 28: case 40: case 41: case 42: case 30: case 31:
            /* PORT-DEBT(stub, FUN_0048cdcc): the other chars' swings (FUN_004977df,
             * FUN_00497e3a, FUN_00494ca4, FUN_004984bf, FUN_004986ee, FUN_0049791f,
             * FUN_00497c83) */
            break;
        default:
            party_attack_louie();            /* FUN_0049831c */
            break;
        }
        break;
    case 4:
        switch (g_party.chr[0]) {
        case 27: case 40: case 28: case 30: case 31: case 41: case 42:
            /* PORT-DEBT(stub, FUN_0048cdcc): the other chars' skill 4
             * (FUN_00494f87, FUN_00495305, FUN_00495a32, FUN_004964bf,
             * FUN_00496a63, FUN_0049708e, FUN_0049755c) */
            break;
        default:
            party_skill_louie();             /* FUN_00496fca */
            break;
        }
        break;
    default:
        /* PORT-DEBT(stub, FUN_0048cdcc): the skill states 3, 5..8 by char
         * (FUN_00496f12, FUN_00494dc5, …) */
        break;
    }
    if ((floor_event_active(9) || PW(0x56db100u).i > 0) && !chara_has_item(0x96b)) {
        PW(0x56db028u).f = spill32(((long double)g_party.vel[0][0] - PW(0x56db028u).f) * 0.4000000059604645f + PW(0x56db028u).f);
        PW(0x56db030u).f = spill32(((long double)g_party.vel[0][2] - PW(0x56db030u).f) * 0.4000000059604645f + PW(0x56db030u).f);
        g_party.vel[0][0] = PW(0x56db028u).f;
        g_party.vel[0][2] = PW(0x56db030u).f;
    }
}

/* the hurry-up spawn: every time the floor timer runs out (HP > 0), up to 5
 * type-34 ghosts, each at a random point 12 from the player with no wall
 * between and ground ±0.5 */
static void party_hurry_up(void)
{
    static const float k15 = 15.0f, k0p8 = 0.800000011920929f, km0p5 = -0.5f;
    g_floor_timer[0] = 240;
    int n = 0;
    for (int i = 0; i < ACTOR_COUNT; i++)
        if (act_geti(&g_actor[i], ACT_INUSE) != 0 && act_geti(&g_actor[i], ACT_TYPE) == 34) n++;
    if (n >= 5) return;
    for (int tries = 0; tries < 20; tries++) {
        float a = spill32((long double)rng_next_unit() * k_2pi);
        const double ad = a;
        const float sx = spill32((long double)spill32(x87_crt_sin(ad)) * k15);
        const float sz = spill32((long double)spill32(x87_crt_cos(ad)) * k15);
        float frac = 0.0f;
        mapcol_hit_t hit;
        hit.tri = 0; hit.slot = 0;   /* retail zeroes these two outputs first */
        const float *p = g_party.pos[0];
        if (mapcol_ray(p[0], spill32((long double)p[1] + k0p2), p[2], sx, 0.0f, sz, &frac, &hit) != 0)
            continue;
        const float gx = spill32((long double)sx * k0p8 + p[0]);
        const float gz = spill32((long double)sz * k0p8 + p[2]);
        if (mapcol_ground(gx, spill32((long double)p[1] + k0p2), gz, &hit) != 1) continue;
        if (!(hit.height >= km0p5) || !(hit.height < k0p5)) continue;
        const actor_stage_t st = { g_scene1_combat_stage_id, stage_gate_get_next(),
                                   stage_post_load_get_mode_b4d0(), g_ghost_count,
                                   work_rec() ? work_rec()[0x2bc57] : 0 };
        actor_spawn_enemy(&st, spill32((long double)sx * k0p8 + p[0]), p[1],
                          spill32((long double)sz * k0p8 + p[2]), 34);
        if (g_ghost_timer == 0) g_ghost_timer = 1;
        ++g_ghost_count;
        return;
    }
}

/* FUN_004960a4: the warp trap — a random room at least 20 away from the player
 * on either axis (pass 0 counts them, rand % n picks, pass 1 finds it), then a
 * random right angle (rand & 3) tried 4 ways for a clear 5-ray from (x, 0.3, z):
 * the party lands 5 along the first clear one (else on the room's origin), the
 * partner and Tear 0.5 / 1 to its +x. */
static void party_warp_random_room(void)
{
    static const float k40 = 40.0f, k20 = 20.0f, k0p3 = 0.30000001192092896f, k5 = 5.0f;
    static const float k_pi2 = 1.5707963705062866f;
    uint32_t pick = 0;
    for (int pass = 0; pass != 2; pass++) {
        uint32_t count = 0;
        for (int32_t r = 0; r < g_floor_h; r++) {
            for (int32_t c = 0; c < g_floor_w; c++) {
                if (g_floor_rooms[r * FLOOR_GRID_STRIDE + c].type == -1) continue;
                const float dx = spill32((long double)spill32((float)-c) * k40 - g_party.pos[0][0]);
                const float dz = spill32((long double)spill32((float)-r) * k40 - g_party.pos[0][2]);
                const float adx = dx < 0.0f ? -dx : dx, adz = dz < 0.0f ? -dz : dz;
                if (adx < k20 && adz < k20) continue;
                if (pass == 1 && count == pick) {
                    float x = spill32((long double)spill32((float)-c) * k40);
                    float z = spill32((long double)spill32((float)-r) * k40);
                    float ang = spill32((long double)spill32((float)(int32_t)(rng_next15() & 3u)) * k_pi2);
                    for (int k = 0; k != 4; k++) {
                        const double X = ang;
                        const float s5 = spill32((long double)spill32(x87_crt_sin(X)) * k5);
                        const float c5 = spill32((long double)spill32(x87_crt_cos(X)) * k5);
                        float frac = 0.0f;
                        mapcol_hit_t h;
                        if (mapcol_ray(x, k0p3, z, s5, 0.0f, c5, &frac, &h) == 0 || frac > k1) {
                            x = spill32((long double)s5 + x);
                            z = spill32((long double)c5 + z);
                            break;
                        }
                        ang = spill32((long double)ang + k_pi2);
                    }
                    g_party.pos[0][0] = x;
                    g_party.pos[0][2] = z;
                    g_party.pos[1][0] = spill32((long double)x + k0p5);
                    g_party.pos[1][2] = z;
                    g_party.pos[2][0] = spill32((long double)x + k1);
                    g_party.pos[2][2] = z;
                    return;
                }
                count++;
            }
        }
        if (pass == 0 && count > 0) pick = (uint32_t)rng_next15() % count;
    }
}

/* FUN_0048b850 on a dungeon floor */
void dungeon_party_tick(void)
{
    CALL_TRACE_ENTER_STUB(0x48b850u);
    struct sim_player_buttons *pad = &g_sim_buttons[0];
    const uint8_t *rec = work_rec();
    PW(0x56dafe0u).i = 0x73dddd0;            /* &DAT_073dddd0 */
    if (g_dat_0438b8b4 == 0) {
        pad->cur = 0; pad->pressed = 0; pad->held = 0;
        g_dat_0438b8b4 = 1;
    } else if (g_ctrl_lock != -1) {
        pad->cur = (uint16_t)g_ctrl_lock; pad->pressed = 0; pad->held = 0;
    }
    party_gauge_tick();                      /* FUN_0048b6ad */
    if (PW(0x56db00cu).i > 0) --PW(0x56db00cu).i;
    {
        int32_t v = PW(0x56db008u).i;
        if (v > 0) {
            PW(0x56db008u).i = ++v;
            if (v >= 30) { if (PW(0x56db000u).i > 0) --PW(0x56db000u).i; }
            else if (PW(0x56db000u).i < 10) ++PW(0x56db000u).i;
            if (v > 60) PW(0x56db008u).i = 0;
        }
    }
    if (mapcol_maptype() != 0) {
        if (g_dat_0438b550 == 1) {
            g_dat_0438b550 = 0;
            /* PORT-DEBT(stub, FUN_0044b12f): the queued b554 item effects */
        }
        if (g_ctrl_lock == -1 && g_news_ticker_timer == 0 && g_dat_0438b950 == 0) {
            floor_event_pick();              /* FUN_004364bc */
            if (!stage_gate_floor_is_checkpoint() && g_floor_event_n > 0) ++g_news_ticker_timer;
        }
        if (g_dat_0438b954 > 0 && ++g_dat_0438b954 > 200) g_dat_0438b954 = 200;
    }
    if (g_dat_0438b930 > 0) {
        /* the warp trap's spin (b930): the player turns 0.5 a tick, SE 709 at 35,
         * the warp FUN_004960a4 at 60, done at 120; b8b8 eases up from 30 and
         * back down from 60, the camera snaps over 60..109 */
        PW(0x56db05cu).f = spill32((long double)PW(0x56db05cu).f + k0p5);
        int32_t v = ++g_dat_0438b930;
        if (v == 35) {
            audio_play_se_by_id(709);
            v = g_dat_0438b930;
        }
        if (v == 60) {
            party_warp_random_room();        /* FUN_004960a4 */
            v = g_dat_0438b930;
        }
        if (v == 120) {
            v = 0;
            g_dat_0438b930 = 0;
        }
        if (v >= 60) {
            if (g_dat_0438b8b8 > 0) --g_dat_0438b8b8;
        } else if (v >= 30 && g_dat_0438b8b8 < 30) {
            ++g_dat_0438b8b8;
        }
        if (v >= 60 && v < 110) scene1_camera_snap_next();   /* DAT_0438cc68 = 1 */
        if (PW(0x56db0f4u).i <= 60) PW(0x56db0f4u).i = 60;
        g_party.rec[0][6] = x87_ftol(((long double)PW(0x56db05cu).f + g_scene1_camera_yaw + k_pi8)
                                     / k_2pi * k8 + k8) & 7;
        return;
    }
    if (PW(0x56dafecu).i > 0 && --PW(0x56dafecu).i == 0) {
        PW(0x56daeb4u).i = 20;   /* PORT-DEBT(stub, FUN_004846e8) */
    }
    if (PW(0x56daff0u).i > 0 && --PW(0x56daff0u).i == 0) {
        PW(0x56daeb4u).i = 20;   /* PORT-DEBT(stub, FUN_004847b4) */
    }
    party_status_tick();                     /* FUN_004897c6 */
    party_state_tick();                      /* FUN_0048cdcc */
    party_pickups();                         /* FUN_0048cbf6 */
    if (mapcol_maptype() != 0 && !stage_gate_floor_is_checkpoint()) {
        if (g_ghost_timer > 0) ++g_ghost_timer;
        if (PW(0x56db0bcu).f > k0 && g_ghost_timer >= 60 && rec != NULL && rec[0x2bd16] == 0) {
            /* FUN_0044ba2c(19, 3, 0) — the first hurry-up dialogue;
             * PORT-DEBT(stub, FUN_0044ba2c 19/3) */
        }
        char b[32];
        snprintf(b, sizeof b, "T %d   ", g_floor_timer[0]);
        audio_mci_record_command(40, 29, b);
        snprintf(b, sizeof b, "T %d   ", g_floor_rooms_total);
        audio_mci_record_command(40, 30, b);
        if (PW(0x56db0bcu).f > k0) {
            --g_floor_timer[0];
            if (floor_event_active(25)) --g_floor_timer[0];
        }
        if (g_floor_timer[0] <= -1) party_hurry_up();
    }

    /* gravity */
    static const float k_grav = 0.029999999329447746f, km2 = -2.0f;
    PW(0x56daac0u).f = spill32((long double)PW(0x56daac0u).f - k_grav);
    if (PW(0x56daac0u).f < km2) PW(0x56daac0u).f = km2;
    /* the speed cap */
    const float vz0 = g_party.vel[0][2], vx0 = g_party.vel[0][0];
    float sp = spill32((long double)vx0 * vx0 + (long double)vz0 * vz0);
    sp = spill32(crt_sqrt(sp));
    float cap = 0.17499999701976776f;
    if (mapcol_maptype() != 0) {
        const chara_def_t *cd = &g_chara[chara_equip_get_current_chara()];
        cap = cd->move_speed;
        if (PW(0x56db034u).i == 1) cap = cd->dash_speed;
    }
    if (chara_has_item(0x968)) cap = spill32((long double)cap + 0.02);
    if (chara_has_item(0x969)) cap = spill32((long double)cap + 0.08);
    if (floor_event_active(11) || floor_event_active(12) || PW(0x56db010u).i > 0 || PW(0x56db01cu).i > 0)
        cap = spill32((long double)cap * 1.2999999523162842f);
    if (g_ctrl_lock == -1) cap = spill32((long double)PW(0x56db074u).f + cap);
    if (PW(0x56dae9cu).i != 0) {
        const uint8_t fl = (uint8_t)PW(0x56daeacu).i;
        if (fl & 2) cap = spill32((long double)cap + 0.06000000238418579f);
        else if (fl & 1) cap = spill32((long double)cap + 0.030000001192092896f);
    }
    if (PW(0x56db048u).i == 1) cap = k0p5;
    if (PW(0x56db048u).i == 4 || PW(0x56db048u).i == 5) cap = g_party.chr[0] == 41 ? k1 : k0p5;
    if (g_party.hit[0].flag == 1 && PW(0x56db07cu).i == 0) {
        cap = spill32((long double)g_party.hit[0].slope_h - g_party.pos[0][1]);
        if (cap > k1) cap = 1.0f;
        if (cap < k0) cap = 0.0f;
        cap = spill32(0.30000001192092896f - (long double)cap * k0p1);
    }
    if (sp >= cap) {
        g_party.vel[0][0] = spill32((long double)g_party.vel[0][0] * cap / sp);
        g_party.vel[0][2] = spill32((long double)g_party.vel[0][2] * cap / sp);
    }
    /* the facing octant, snapped to the cardinals (the sticky axis) */
    int32_t o = octant(PW(0x56db05cu).f);
    g_party.rec[0][6] = o;
    if (o == 2 || o == 6) PW(0x56dae3cu).i = 1;
    if (o == 0 || o == 4) PW(0x56dae3cu).i = 0;
    if (PW(0x56dae3cu).i == 0) {
        if (o == 1) g_party.rec[0][6] = o = 0;
        if (o == 7) g_party.rec[0][6] = o = 0;
        if (o == 3) g_party.rec[0][6] = o = 4;
        if (o == 5) g_party.rec[0][6] = 4;
    } else {
        if (o == 1) g_party.rec[0][6] = o = 2;
        if (o == 7) g_party.rec[0][6] = o = 6;
        if (o == 3) g_party.rec[0][6] = o = 2;
        if (o == 5) g_party.rec[0][6] = 6;
    }
    if (PW(0x56daea4u).i != 0) {
        /* PORT-DEBT(stub, FUN_004147d5): the boost aura (types 24 / 22) */
    }
    if (PW(0x56db048u).i != 1) {
        /* PORT-DEBT(stub, FUN_0048b850 da1bc): the attack lunge (da1bc 1..ccb8) */
    }
    party_move();                            /* FUN_00483170 */
    /* PORT-DEBT(stub, FUN_0048b850): the water splash after the move (hit
     * flag 1: SE 322, types 145 / 17) */
    /* damping */
    if (PW(0x56da1bcu).i != 0) {
        g_party.vel[0][0] = spill32((long double)g_party.vel[0][0] * 0.9700000286102295f);
        g_party.vel[0][2] = spill32((long double)g_party.vel[0][2] * 0.9700000286102295f);
    } else if (g_party.pos[0][1] != g_party.ground_y[0]) {
        g_party.vel[0][0] = spill32((long double)g_party.vel[0][0] * 0.9900000095367432f);
        g_party.vel[0][2] = spill32((long double)g_party.vel[0][2] * 0.9900000095367432f);
    } else {
        const uint8_t *st = (const uint8_t *)g_stage_palette;
        int32_t ice = 0;
        if (st) memcpy(&ice, st + 0x1b28, 4);
        float f;
        if (ice != 0 && !chara_has_item(0x96b)) f = 0.949999988079071f;
        else if ((floor_event_active(9) || PW(0x56db100u).i > 0) && !chara_has_item(0x96b)) f = 0.9980000257492065f;
        else if (PW(0x56db048u).i == 2) f = 0.8199999928474426f;
        else if (PW(0x56db048u).i == 3) f = 0.949999988079071f;
        else f = (g_party.pos[0][1] == g_party.ground_y[0]) ? 0.8199999928474426f : 0.9800000190734863f;
        g_party.vel[0][0] = spill32((long double)g_party.vel[0][0] * f);
        g_party.vel[0][2] = spill32((long double)g_party.vel[0][2] * f);
    }
    PW(0x56daac0u).f = spill32((long double)PW(0x56daac0u).f * 0.949999988079071f);
    if (PW(0x56db048u).i >= 53 && PW(0x56db048u).i <= 58) PW(0x56db0f4u).i = 5;
    dungeon_party_follow_tick();             /* FUN_0048a833 */
    party_aura_tick();                       /* FUN_00483e7b */
    if (g_party.pos[0][1] == g_party.ground_y[0]) {
        const float vz = g_party.vel[0][2], vx = g_party.vel[0][0];
        float s = spill32((long double)vx * vx + (long double)vz * vz);
        s = spill32(spill32(crt_sqrt(s)));
        if (s > k0p1) {
            static const float k0p125 = 0.125f;
            const float *p = g_party.pos[0];
            if ((PW(0x56db054u).i & 0xf) == 0) {
                const float z = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + p[2]);
                const float y = spill32((long double)p[1] + k0p5);
                const float x = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + p[0]);
                scene1_spawn(0, x, y, z, 0xe, k0p125, 1);
            }
            if (PW(0x56db034u).i == 1) {
                const float z = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + p[2]);
                const float y = spill32((long double)p[1] + k0p5);
                const float x = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + p[0]);
                scene1_spawn(0, x, y, z, 0xe, k0p125, 1);
            }
            if (g_party.rec[0][5] == 1 && (g_party.rec[0][3] & 0xf) == 0xa)
                audio_play_se_by_id(0x166);
        }
    }
    PW(0x56db06cu).f = 1.0f;
    PW(0x56db068u).f = spill32((long double)PW(0x56db068u).f + 0.05000000074505806f);
    if (PW(0x56db068u).f > k1) PW(0x56db068u).f = 1.0f;
    g_scene1_camera_yaw = angle_ease(g_scene1_camera_yaw, PW(0x56db060u).f, 0.06283185631036758f);
    ++PW(0x56db054u).i;
    /* the history rings: newest first */
    memmove(&g_party.pos_hist[1], &g_party.pos_hist[0], sizeof g_party.pos_hist[0] * 39);
    memmove(&g_party.rec_hist[1], &g_party.rec_hist[0], sizeof g_party.rec_hist[0] * 39);
    g_party.pos_hist[0][0] = g_party.pos[0][0];
    g_party.pos_hist[0][1] = g_party.pos[0][1];
    g_party.pos_hist[0][2] = g_party.pos[0][2];
    memcpy(g_party.rec_hist[0], g_party.rec[0], sizeof g_party.rec_hist[0]);
    if (PW(0x56daae0u).i > 0) {
        for (int k = 0; k < 5; k++) {
            int32_t *b = g_party.burst[k];
            b[14] = 20;
            memcpy(&b[11], g_party.pos_hist[3 + 2 * k], 12);
            memcpy(b, g_party.rec_hist[3 + 2 * k], 44);
        }
        if (--PW(0x56daae0u).i == 0)
            for (int k = 0; k < 5; k++) g_party.burst[k][14] = 0;
    }
    if (PW(0x56dae14u).i > 0) --PW(0x56dae14u).i;   /* PORT-DEBT(stub, FUN_0044376a): its 0 edge */
    for (int k = 0; k < 5; k++) {
        if (g_party.trail[k][14] > 0) {
            /* PORT-DEBT(stub, FUN_0048b850 trail): the orbiting after-images
             * (DAT_005ce5c0 angles, FUN_0041331d at 600) */
            --g_party.trail[k][14];
        }
    }
    /* the auto-map: rooms within 20 of the player are visited */
    for (int row = 0; row < g_floor_h; row++) {
        for (int col = 0; col < g_floor_w; col++) {
            floor_room_t *r = &g_floor_rooms[row * FLOOR_GRID_STRIDE + col];
            if (r->type == -1 || r->pad != 0) continue;
            if (floor_event_active(19)) r->pad = 1;
            float dx = spill32((long double)spill32((long double)-col) * 40.0f - g_party.pos[0][0]);
            float dz = spill32((long double)spill32((long double)-row) * 40.0f - g_party.pos[0][2]);
            dx = spill32(fabsl((long double)dx));
            if (dx < 20.0f) {
                dz = spill32(fabsl((long double)dz));
                if (dz < 20.0f && !floor_event_active(24)) r->pad = 1;
            }
        }
    }
    /* a marked cell's room once visited */
    int32_t *seen = &g_floor_marks[10][1];   /* DAT_0438b488 */
    for (int k = 1; k != g_floor_extra_n + 2; k++) {
        const int32_t *m = g_floor_marks[k];
        if (seen[k - 1] == 0 && g_floor_rooms[m[1] * FLOOR_GRID_STRIDE + m[0]].pad != 0)
            seen[k - 1] = 1;
    }
}
