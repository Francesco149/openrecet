/*
 * record_hit.c — see record_hit.h.  Line refs are retail VAs.
 */
#include "record_hit.h"

#include <math.h>
#include <string.h>

#include "actor_ai.h"            /* the popup table DAT_044e28f0 */
#include "audio.h"               /* FUN_00499519 */
#include "chara_equip.h"         /* DAT_0438b7d8 */
#include "dungeon_flow.h"        /* FUN_0043647f, DAT_0438cbe4 */
#include "dungeon_objects.h"     /* the stage objects DAT_0695ef78 */
#include "dungeon_party.h"       /* FUN_00485712 */
#include "floor_layout.h"        /* DAT_073e03ac, the trap spots DAT_0438b200 / b390 / b3e0 */
#include "math3d.h"              /* FUN_004a35ef / FUN_004a3553 / FUN_004a2a10 */
#include "map_collision.h"       /* FUN_00433674 */
#include "party.h"
#include "rng.h"                 /* FUN_00471084 / FUN_00471089 */
#include "save_work.h"
#include "scene1_camera.h"       /* FUN_0044b219 */
#include "scene1_combat_sm.h"    /* DAT_0438b4c8, DAT_0438bed8, DAT_0438b904/908, DAT_06a46f94 */
#include "scene1_per_frame_open.h"   /* FUN_0041331d */
#include "scene1_records.h"
#include "scene1_records_b_spawn.h"   /* DAT_06a46fb8 */
#include "scene1_records_b_tick.h"    /* DAT_06a46f98, the state-machine hook */
#include "scene1_records_c_spawn.h"   /* FUN_0044b0f3, FUN_0044aef0 */
#include "scene1_actor_say.h"        /* FUN_00485413 */
#include "scene1_sim.h"              /* DAT_0438b1c8 */
#include "scene1_spawn.h"        /* FUN_00447f4f */
#include "stage_gate.h"          /* DAT_0438b4cc, FUN_00431990, FUN_004319d6 */
#include "stage_post_load.h"     /* DAT_0438b4d0, DAT_0438bef4, FUN_00435fbb */
#include "tables_enemy.h"
#include "tables_enemylist.h"
#include "tables_item.h"
#include "x87.h"

int32_t g_s_easydisp;
int32_t g_orb_pick_n;
int32_t g_kill_combo;
int32_t g_kill_combo_type;
int32_t g_kill_combo_timer;
int32_t g_dat_06a46f8c, g_dat_06a46f90;

#define AI(a, off)  (*(int32_t *)(void *)((a)->b + (off)))
#define AF(a, off)  (*(float *)(void *)((a)->b + (off)))
#define RI(s, off)  (*(int32_t *)(void *)((uint8_t *)(s) + (off)))
#define RF(s, off)  (*(float *)(void *)((uint8_t *)(s) + (off)))

static const float k0 = 0.0f, k0p01 = 0.009999999776482582f, k0p1 = 0.10000000149011612f;
static const float k0p12 = 0.11999999731779099f, k1 = 1.0f, k2 = 2.0f, k3 = 3.0f, k4 = 4.0f;
static const float k0p2 = 0.20000000298023224f, k0p3 = 0.30000001192092896f, k0p5 = 0.5f;
static const float k0p7 = 0.699999988079071f, k0p8 = 0.800000011920929f;
static const float k0p85 = 0.8500000238418579f, k1p2 = 1.2000000476837158f, k1p5 = 1.5f;
static const float k0p4 = 0.4000000059604645f, k20 = 20.0f, km40 = -40.0f;
static const float k1000 = 1000.0f;
static const float k_pi = 3.1415927410125732f, k_mpi = -3.1415927410125732f;
static const float k_2pi = 6.2831854820251465f;
static const float k_ang54 = 0.9424778819084167f, k_mang54 = -0.9424778819084167f;
static const float k_ang63 = 1.0995573997497559f, k_mang63 = -1.0995573997497559f;
static const float k_pi4 = 0.7853981852531433f, k_mpi4 = -0.7853981852531433f;
static const float k_3pi4 = 2.356194496154785f, k_m3pi4 = -2.356194496154785f;

/* DAT_0052b98c: the drop lists of FUN_0042e1a6's first two branches */
static const int32_t k_drop_list[20] = {
    4008, 4001, 4025, 4009, 4026, 4002, 4003, 4027, 4004, 4028,
    4017, 4012, 4029, 4005, 4013, 4031, 4018, 4014, 4033, 4007,
};

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

static uint8_t *work(void) { return save_work_bank_at(save_work_active_slot()); }

/* the work record's party member of the active chara (+0x2ceb0, 0x6c each):
 * [0] level, [0x44] EXP, [0x4c] the next level's EXP */
static uint8_t *member(void)
{
    uint8_t *w = work();
    return w ? w + 0x2ceb0 + 0x6c * chara_equip_get_current_chara() : NULL;
}

/* a record's owner pointer (+0x10 / +0x14) → the party block or an actor */
static uint8_t *owner_of(int32_t v)
{
    if (v == 0) return NULL;
    if (v == (int32_t)(intptr_t)&g_party) return (uint8_t *)&g_party;
    for (int i = 0; i < ACTOR_COUNT; i++)
        if (v == (int32_t)(intptr_t)&g_actor[i]) return g_actor[i].b;
    return NULL;
}

static const enemy_record_t *enemy_at(int32_t t)
{
    if (t < 0 || t >= ENEMY_COUNT) t = 0;
    return &g_enemy[t];
}

/* the angle loop of 0x438a82: atan2(dx, dz) − yaw + π, into [−π, π] */
static float hit_angle(float dx, float dz, float yaw)
{
    float v = spill32(((long double)spill32(crt_atan2(dx, dz)) - yaw) + k_pi);
    while (v < k_mpi) v = spill32((long double)v + k_2pi);
    while (v > k_pi) v = spill32((long double)v - k_2pi);
    return v;
}

long double enemy_level_mul(int32_t type)
{
    const actor_stage_t st = { g_scene1_combat_stage_id, stage_gate_get_next(),
                               stage_post_load_get_mode_b4d0(), g_ghost_count, 0 };
    const int32_t lv = actor_enemy_level(&st, type);
    const float f = spill32((long double)lv);
    return (long double)f * (lv < 50 ? k0p1 : k0p12) + k1;
}

void popup_push(float x, float y, float z, int32_t value, int32_t kind)
{
    for (int k = 0; k < 32; k++) {
        int32_t *p = g_popup[k];
        if (p[3] != 0) continue;
        memcpy(&p[0], &x, 4);
        memcpy(&p[1], &y, 4);
        memcpy(&p[2], &z, 4);
        p[3] = kind == 5 ? 90 : 60;
        p[4] = p[3];
        p[6] = kind;
        p[5] = value;
        return;
    }
}

int32_t orb_kind(void)
{
    int32_t k = (int32_t)(rng_next15() & 3);
    if (++g_orb_pick_n & 1) {
        if (PW(0x56db09cu).i & 0xff) k = 0;                 /* byte 0x56db09c */
        else if ((PW(0x56db098u).i >> 16) & 0xff) k = 1;    /* byte 0x56db09a */
        else if ((PW(0x56db098u).i >> 24) & 0xff) k = 2;    /* byte 0x56db09b */
        else if ((PW(0x56db09cu).i >> 8) & 0xff) k = 3;     /* byte 0x56db09d */
    }
    return k == 0 ? 1 : k == 1 ? 2 : k == 2 ? 3 : 0;
}

int32_t enemy_drop_pick(int32_t type, int32_t rarity, int32_t var)
{
    if (type == 34) {
        if (rarity == 1) {
            const int32_t d = g_scene1_combat_stage_id;
            return d >= 0 && d < ENEMYLIST_WISP_SLOTS ? g_enemylist.wisp_drops[d] : -1;
        }
        return -1;
    }
    int32_t dg = g_scene1_combat_stage_id, fl = stage_gate_get_next();
    if (stage_post_load_get_mode_b4d0() >= 0) {
        dg = stage_post_load_get_mode_b4d0();
        /* PORT-DEBT(stub, FUN_0043183e): the event floors' floor tables
         * DAT_005adb68 / 74 / 88 / a8 by DAT_0438b4d4 */
    }
    if (dg < 0 || dg >= ENEMYLIST_DUNGEON_SLOTS) return -1;
    for (int s = 0; s < ENEMYLIST_SECTIONS_PER_DUNGEON; s++) {
        const enemylist_section_t *sec = &g_enemylist.sections[dg][s];
        if (sec->floor_lo == -1) return -1;
        if (fl < sec->floor_lo || fl > sec->floor_hi) continue;
        for (int k = 0; k < ENEMYLIST_ENEMY_SLOTS_PER_SECTION; k++) {
            if (sec->enemies[k].enemy_id == -1) break;
            if (sec->enemies[k].enemy_id == type && sec->enemies[k].variant == var)
                return sec->drops[k].item_id[rarity];
        }
        for (int k = 0; k < ENEMYLIST_ENEMY_SLOTS_PER_SECTION; k++) {
            if (sec->enemies[k].enemy_id == -1) break;
            if (sec->enemies[k].enemy_id == type) return sec->drops[k].item_id[rarity];
        }
        return -1;
    }
    return -1;
}

static void drop_item(const actor_t *a, int32_t item, int32_t e1)
{
    scene1_records_c_spawn_world_drop_default((int)(intptr_t)&g_party, AF(a, 0x3e4), AF(a, 0x3e8),
                                              AF(a, 0x3ec), item, 1, 0.0f, e1, 0);
}

void actor_kill(actor_t *a)
{
    const int32_t type = AI(a, ACT_TYPE);
    const enemy_record_t *e = enemy_at(type);
    AF(a, ACT_HP) = 0.0f;
    if ((type == 50 || type == 51) && AI(a, 0xadc) != 0) {
        AF(a, ACT_HP) = AF(a, ACT_HP_MAX);
        AF(a, ACT_HP2) = 0.0f;
    }
    AI(a, 0xaf0) = 0;
    AI(a, ACT_INUSE) = 2;
    if (AI(a, 0xadc) != 0) return;
    if (g_kill_combo_type == type) {
        if (AI(a, 0xb40) == 0 && AI(a, 0xb2c) == 0) {
            ++g_kill_combo;
            g_kill_combo_timer = 60;
        }
    } else {
        g_kill_combo = 0;
    }
    g_kill_combo_type = type;
    /* EXP: (exp + level / 2) · 0.8 + the combo, as orbs */
    const int32_t exp = e->exp_reward;
    const float base = spill32((long double)(AI(a, ACT_LEVEL) / 2 + exp));
    int32_t n = x87_ftol((long double)base * k0p8 + spill32((long double)g_kill_combo));
    if (AI(a, 0xb40) == 1 || AI(a, 0xb2c) == 1) n = 1;
    if (AI(a, 0xb40) == 2) n = 0;
    uint8_t *m = member();
    if (n > 0 && m) {
        int32_t lv; memcpy(&lv, m, 4);
        if (lv >= 9 && lv > 2 * AI(a, ACT_LEVEL)) {
            n /= 2;
            if (n < 1) n = 1;
        }
    }
    if (exp == 0) n = 0;
    int straight = 0;
    for (int32_t k = n; k != 0; --k) {
        if (g_s_easydisp != 0) {
            if (m) {
                int32_t x; memcpy(&x, m + 0x44, 4);
                x += 10;
                if (floor_event_active(2)) x += 10;
                memcpy(m + 0x44, &x, 4);
            }
            straight = 1;
        } else {
            const int32_t kind = orb_kind();
            scene1_records_c_spawn_world_drop_default((int)(intptr_t)&g_party, AF(a, 0x3f0),
                                                      spill32((long double)AF(a, 0x3f4) + k0p5),
                                                      AF(a, 0x3f8), kind, 1, 1.0f, 0, 0);
        }
    }
    if (straight && m) {
        int32_t x, nx; memcpy(&x, m + 0x44, 4); memcpy(&nx, m + 0x4c, 4);
        if (x >= nx) {
            /* PORT-DEBT(stub, FUN_0048a383): the level-up of s_easydisp */
        }
    }
    if (AI(a, 0xb40) != 0) { (void)rng_next15(); return; }
    /* the drop roll: rand % 1000 against the five DAT_0438bef4 weights */
    const int32_t r = (int32_t)(rng_next15() % 1000u);
    const int boss = stage_gate_boss_id_allowed(type);
    uint8_t *w = work();
    int once = 0;
    if (g_scene1_combat_stage_id == 1 && w) {
        if (type == 23) {
            if (w[0x2bd38] == 0) w[0x2bd38] = 1;
            else if (w[0x2bd39] == 0) { w[0x2bd39] = 1; once = 1; }
        }
        if (type == 24) {
            if (w[0x2bd3a] == 0) w[0x2bd3a] = 1;
            else if (w[0x2bd3b] == 0) { w[0x2bd3b] = 1; once = 1; }
        }
    }
    const float rf = spill32((long double)r);
    const long double wa = stage_post_load_get_dat_0438bef4_as_float(0);
    const long double wb = stage_post_load_get_dat_0438bef4_as_float(1);
    const long double wc = stage_post_load_get_dat_0438bef4_as_float(2);
    const long double wd = stage_post_load_get_dat_0438bef4_as_float(3);
    const long double we = stage_post_load_get_dat_0438bef4_as_float(4);
    if (wa * k1000 > rf && !once) {
        if (!boss) {
            const float q = spill32((long double)spill32((long double)(stage_gate_get_next() + 2 * g_scene1_combat_stage_id)) / k3);
            int32_t cap = x87_ftol(q) + 4;
            if (cap > 7) cap = 7;
            const uint32_t pick = rng_next15() % (uint32_t)(cap + 1);
            drop_item(a, k_drop_list[pick] + 7, 0);
            stage_post_load_pulse_5fold(0, 0);
        }
    } else if ((wb + wa) * k1000 > rf && !once) {
        if (!boss) {
            const float q = spill32((long double)spill32((long double)(stage_gate_get_next() + 2 * g_scene1_combat_stage_id)) / k3);
            int32_t cap = x87_ftol(q) + 7;
            if (cap > 19) cap = 19;
            const uint32_t pick = rng_next15() % (uint32_t)(cap + 1);
            drop_item(a, k_drop_list[pick] + 7, 0);
            stage_post_load_pulse_5fold(0, 1);
        }
    } else if (!once && !(((wc + wb) + wa) * k1000 > rf)) {
        int32_t v;
        if ((((wd + wc) + wb) + wa) * k1000 > rf) {
            v = enemy_drop_pick(type, 1, AI(a, ACT_GROUP_VAR));
            if (v != -1 && v != 0) { drop_item(a, v + 7, 0); stage_post_load_pulse_5fold(0, 3); goto out; }
        } else if (((((we + wd) + wc) + wb) + wa) * k1000 > rf) {
            v = enemy_drop_pick(type, 2, AI(a, ACT_GROUP_VAR));
            if (v != -1 && v != 0) { drop_item(a, v + 7, 0); stage_post_load_pulse_5fold(0, 4); goto out; }
        }
        stage_post_load_pulse_5fold(0, -1);
    } else {
        const int32_t v = enemy_drop_pick(type, 0, AI(a, ACT_GROUP_VAR));
        if (v != -1 && v != 0) {
            int32_t e1 = (int32_t)(rng_next15() & 1);
            const int32_t slot = tables_item_find_slot_by_id(&g_item, v);
            const int8_t lim = slot >= 0 ? (int8_t)g_item.records[slot].stock_info[7] : 0;
            int32_t have = 0;
            if (w) memcpy(&have, w + 0x2c400, 4);
            if (have + 1 >= lim) e1 = 0;
            drop_item(a, v + 7, e1);
            stage_post_load_pulse_5fold(0, 2);
        } else {
            stage_post_load_pulse_5fold(0, -1);
        }
    }
out:
    (void)rng_next15();
}

void actor_damage(actor_t *a, int32_t dmg, int32_t flags)
{
    const float df = spill32((long double)dmg);
    AF(a, ACT_HP) = spill32((long double)AF(a, ACT_HP) - df);
    AI(a, 0x44c) = 30;
    const int32_t type = AI(a, ACT_TYPE);
    if (type == 25) { /* PORT-DEBT(stub, FUN_0041fa37) */ }
    if (type == 28) { /* PORT-DEBT(stub, FUN_0042460a) */ }
    if (type == 41) { /* PORT-DEBT(stub, FUN_00425bff) */ }
    if (dmg == 0) flags = 0;
    const float hx = AF(a, 0x3e4), hy = AF(a, 0x3e8), hz = AF(a, 0x3ec);
    popup_push(hx, hy, hz, dmg, 0);
    int32_t row = 0;
    if (flags & 1) { popup_push(hx, hy, hz, 0, 10); row = 1; }
    if (flags & 2) popup_push(hx, hy, hz, row++, 11);
    if (flags & 4) popup_push(hx, hy, hz, row++, 12);
    if (flags & 8) popup_push(hx, hy, hz, row, 13);
    if (flags != 0) {
        /* a flagged hit (back / side / staggered) sheds EXP orbs */
        int32_t d = dmg;
        const float hp0 = spill32((long double)df + AF(a, ACT_HP));
        if (df > hp0) d = x87_ftol(hp0);
        int32_t n = d / 2 / 2;
        if (n > 25) n = 25;
        uint8_t *m = member();
        if (n > 0 && m) {
            int32_t lv; memcpy(&lv, m, 4);
            if (lv >= 9 && lv > 2 * AI(a, ACT_LEVEL)) {
                n /= 2;
                if (n < 1) n = 1;
            }
        }
        for (int32_t k = n; k != 0; --k) {
            if (g_s_easydisp != 0) {
                if (m) {
                    int32_t x; memcpy(&x, m + 0x44, 4);
                    ++x;
                    if (floor_event_active(2)) ++x;
                    memcpy(m + 0x44, &x, 4);
                }
            } else {
                const int32_t kind = orb_kind();
                scene1_records_c_spawn_world_drop_default((int)(intptr_t)&g_party, AF(a, 0x3f0),
                                                          spill32((long double)AF(a, 0x3f4) + k0p5),
                                                          AF(a, 0x3f8), kind, 1, 1.0f, 0, 1);
            }
        }
    }
    if (type == 73) { /* PORT-DEBT(stub, FUN_00420591) */ }
    if (AI(a, 0xba0) > 0 && AF(a, ACT_HP) < k1) {
        AF(a, ACT_HP) = 0.0f;
    } else if (AF(a, ACT_HP) < k1) {
        AF(a, ACT_HP) = 0.0f;
        actor_kill(a);
    }
}

/* FUN_0043865e's actor arm (0x4386cc..0x439653) */
int record_hit_actors(int32_t *slot)
{
    const int32_t state = RI(slot, 0x04);
    if (state != 0 && state != 3) return 0;
    if (!(PW(0x56db0bcu).f > k0)) return 0;
    const float px = RF(slot, 0x5c), py = RF(slot, 0x60), pz = RF(slot, 0x64);
    const float reach = RF(slot, 0xa8);
    const int32_t rtype = RI(slot, 0x00);
    actor_t *a = NULL;
    const enemy_record_t *e = NULL;
    int32_t idx = 0, m = 0, anchor = 0;
    float hx = 0, hy = 0, hz = 0, r = 0, dx = 0, dy = 0, dz = 0, d = 0;
    for (idx = 0; idx < ACTOR_COUNT; idx++) {
        actor_t *c = &g_actor[idx];
        if (AI(c, 0xaf4) > 0 || AI(c, 0xb08) != 0) continue;
        if (state == 3 && RI(slot, 0x14) != 0 && RI(slot, 0x14) == (int32_t)(intptr_t)c) continue;
        const int32_t inuse = AI(c, ACT_INUSE);
        if (!(inuse == 1 || (AI(c, 0xb98) != 0 && inuse == 2))) continue;
        int h = 0;
        while (h < 10 && RI(slot, 0x11c) != AI(c, 0xb5c + 4 * h)) h++;
        if (h < 10) continue;
        const int32_t t = AI(c, ACT_TYPE);
        const enemy_record_t *ce = enemy_at(t);
        int32_t n = 1;
        if (t == 0x44 || t == 0x45) n = 7;
        if (t == 0x46 || t == 0x47) n = 2;
        for (m = 0; m != n; m++) {
            hx = AF(c, 0x3e4);
            hy = AF(c, 0x3e8);
            hz = AF(c, 0x3ec);
            r = AF(c, 0xabc);
            anchor = 0;
            if (t == 0x46 || t == 0x47 || m != 0) {
                /* PORT-DEBT(stub, FUN_0043865e): the multi-part anchors
                 * DAT_005c530c / DAT_005c5314 (+0x6fc + 12k, radius / 2;
                 * the golem / dragon parts) */
                continue;
            }
            dx = spill32((long double)hx - px);
            dy = spill32((long double)hy - py);
            dz = spill32((long double)hz - pz);
            if (dx == k0 && dz == k0) dz = k0p01;
            d = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
            const float g = spill32((long double)d - reach);
            if (!(((long double)r * ce->runtime_floats[3]) * ce->runtime_floats[0] > g)) continue;
            const float h8 = spill32((long double)reach * k0p8);
            const float hh = spill32(((long double)r * ce->runtime_floats[2]) * ce->runtime_floats[0]);
            if (!((long double)dy - h8 < hh)) continue;
            if (!(-(long double)hh < (long double)h8 + dy)) continue;
            break;
        }
        if (m != n) { a = c; e = ce; break; }
    }
    if (a == NULL) return 0;

    /* 0x438a19: the hit */
    const int32_t t = AI(a, ACT_TYPE);
    int32_t bits = 0, nohit = anchor;
    if (t == 0x48) nohit = 1;
    if (t == 0x44 || t == 0x45) {
        if (AI(a, 0xa54) == 6 && AI(a, 0xa5c) == 1) {
            nohit = 0;
        } else {
            const float v = hit_angle(dx, dz, AF(a, ACT_YAW));
            /* (the retail prints "ANG:%f" to debug row 30 / 33 here) */
            if (!(v < k_ang54) || !(v > k_mang54)) nohit = 1;
        }
    }
    const float K = spill32(((long double)e->runtime_floats[3] * e->runtime_floats[0]) * r);
    const float ex = spill32((long double)hx - (long double)K * dx / d);
    const float ey = spill32((long double)dy * k0p85 + py);
    const float ez = spill32((long double)hz - (long double)K * dz / d);
    const float vx = RF(slot, 0x68), vz = RF(slot, 0x70);
    float kb = spill32(crt_sqrt(spill64((long double)vz * vz + (long double)vx * vx)));
    if (kb > k0) kb = spill32((long double)k0p7 / kb);
    AI(a, 0xb5c + 4 * AI(a, 0xb84)) = RI(slot, 0x11c);
    AI(a, 0xb84) = (AI(a, 0xb84) + 1) % 10;
    const float kb0 = kb;
    int32_t dmg = 0;
    if (rtype == 0x53) {
        if (e->flags == 0 && t != 0x22) {
            int32_t w = stage_gate_query() == 1 ? 120 : 600;
            w -= RI(slot, 0x98);
            AI(a, 0xb18) = w < 0 ? 0 : w;
            g_scene1_combat_dat_0438bed8 = 4;
            kb = 0.0f;
        }
    } else {
        int32_t base, def;
        const float lsc = AF(a, 0xab8);
        if (state == 0) {
            const int32_t atk = PW(0x56db0b4u).i;
            int32_t lv = x87_ftol(((long double)spill32((long double)e->md) * lsc) * lsc);
            if (AI(a, 0xb28) != 0) lv = 0;
            base = -x87_ftol((long double)spill32((long double)lv) / k4 - (long double)spill32((long double)atk) / k2);
        } else {
            const uint8_t *ob = owner_of(RI(slot, 0x14));
            const int32_t ot = ob ? *(const int32_t *)(const void *)(ob + ACT_TYPE) : 26;
            const enemy_record_t *oe = ob ? enemy_at(ot) : &g_enemy[26];
            const float mul = spill32(enemy_level_mul(ot));
            const float ma = spill32((long double)spill32((long double)oe->ma) * mul);
            int32_t lv = oe->md;                 /* the owner's (0x438c9c) */
            if (AI(a, 0xb28) != 0) lv = 0;
            base = -x87_ftol((long double)spill32((long double)lv) / k4 - (long double)ma / k2);
        }
        PW(0x56da1b8u).i |= 2;
        def = x87_ftol(((long double)spill32((long double)e->df) * lsc) * lsc);
        if (AI(a, 0xb28) != 0) def = 0;
        int32_t v;
        if (state == 0) {
            v = x87_ftol((long double)spill32((long double)PW(0x56db0acu).i) / k2 -
                         (long double)spill32((long double)def) / k4);
            if (RI(slot, 0xb8) != 0) bits = 1;
        } else {
            const uint8_t *ob = owner_of(RI(slot, 0x14));
            const int32_t ot = ob ? *(const int32_t *)(const void *)(ob + ACT_TYPE) : 26;
            const enemy_record_t *oe = ob ? enemy_at(ot) : &g_enemy[26];
            const float mul = spill32(enemy_level_mul(ot));
            const float at = spill32((long double)spill32((long double)oe->at) * mul);
            v = x87_ftol((long double)at / k2 - (long double)spill32((long double)def) / k4);
        }
        switch (rtype) {
        case 0x12: case 0x52: case 0x60: case 0x61: case 0x62: case 0x6a: case 0x83:
            v = base;
            break;
        case 0x3e: case 0x51: case 0x5f: case 0x63: case 0x64: case 0x69: case 0x82:
            v = (v + base) / 2;
            break;
        default:
            break;
        }
        dmg = x87_ftol((long double)spill32((long double)v) * RF(slot, 0xb4));
        if (state == 0) {
            if (floor_event_active(5) || floor_event_active(3)) dmg *= 2;
            if (PW(0x56db014u).i > 0 || PW(0x56db01cu).i > 0) dmg *= 2;
        } else if (floor_event_active(4) || floor_event_active(3)) {
            dmg *= 2;
        }
        if (floor_event_active(7) || floor_event_active(6)) dmg /= 2;
        if (AI(a, 0xb38) > 0) dmg /= 2;
        if ((t == 0x44 || t == 0x45) && rtype == 0x12 && m == 0 && AI(a, 0xa54) != 6) {
            AI(a, 0xa58) = 0;
            AI(a, 0xa5c) = 0;
            AI(a, 0xa60) = 0;
            AI(a, 0xa54) = 6;
        }
        if (AI(a, 0xb9c) != 0 && AI(a, 0xb18) == 0) {
            const float v2 = hit_angle(dx, dz, AF(a, ACT_YAW));
            if (v2 < k_ang63 && v2 > k_mang63) {
                nohit = 1;
                actor_anim_set(a, 4);
                AI(a, 0xa58) = 0;
                AI(a, 0xa54) = 4;
            }
        }
        if (AI(a, 0xa54) >= 1 && AI(a, 0xa54) <= 6) {
            bits |= 8;
            dmg = x87_ftol((long double)spill32((long double)dmg) * k1p2);
        }
        {
            const float v3 = hit_angle(dx, dz, AF(a, ACT_YAW));
            if (!(v3 < k_pi4) || !(v3 > k_mpi4)) {
                if (v3 < k_3pi4 && v3 > k_m3pi4) {
                    bits |= 4;
                    dmg = x87_ftol((long double)spill32((long double)dmg) * k1p2);
                } else {
                    bits |= 2;
                    dmg = x87_ftol((long double)spill32((long double)dmg) * k1p5);
                }
            }
        }
        if (state == 0) {
            const uint8_t *oa = owner_of(RI(slot, 0x10));
            if (oa && (*(const int32_t *)(const void *)(oa + 0xce4) != 0 ||
                       *(const int32_t *)(const void *)(oa + 0xcec) != 0))
                dmg = x87_ftol((long double)spill32((long double)dmg) * k1p5);
            if (RI(slot, 0xb8) != 0) dmg *= 2;
        }
        if (nohit == 1) {
            dmg = 0;
        } else if (t == 5) {
            if (dmg < 0) dmg = 0;
        } else {
            if (dmg < 1) dmg = 1;
            if (dmg >= 5) {
                const uint32_t q = (uint32_t)rng_next15();
                dmg += (int32_t)(q % (uint32_t)(dmg / 5));
            } else {
                dmg += (int32_t)(rng_next15() & 1);
            }
        }
        AI(a, 0x440) = rtype == 0x60 ? 60 : 40;
        if (nohit == 0) AI(a, 0x1c) = 6;
        if (AI(a, 0xb98) == 1 && !(AF(a, ACT_HP) > k0)) {
            kb = spill32((long double)kb * k0p3);
        } else {
            if (AI(a, 0xb54) == 1) kb = 0.0f;
            else if (AI(a, 0xb54) == 2) kb = spill32((long double)kb * k0p5);
            if (rtype == 0x82) { kb = 0.0f; AI(a, 0x28) = 1; AI(a, 0x440) = 60; }
            if (rtype == 0x8a) { kb = spill32((long double)kb * k1p5); AI(a, 0x440) = 60; }
            if (rtype == 0x66) kb = spill32((long double)kb * k0p3);
            if (rtype == 0x62) kb = spill32((long double)kb * k0p3);
            if (rtype == 0x72) kb = spill32((long double)kb * k0p1);
            if (rtype == 0x73) kb = spill32((long double)kb * k0p3);
            if (rtype == 0x7e) kb = spill32((long double)kb * k0p3);
            if (rtype == 0x76) kb = spill32((long double)kb * k0p3);
            if (rtype == 0x78) kb = spill32((long double)kb * k0p2);
            if (rtype == 0x7a) kb = spill32((long double)kb * k0p2);
            if (rtype == 0x5b) {
                const uint8_t *oa = owner_of(RI(slot, 0x10));
                kb = (oa && (oa[0] & 1)) ? 0.0f : spill32((long double)kb * k0p2);
            }
        }
        AI(a, 0xba0) = 0;
        if (rtype == 0x86) { kb = 0.0f; AI(a, 0xba0) = 30; }
        if (rtype == 0x85) { kb = 0.0f; AI(a, 0xba0) = 30; }
        g_scene1_combat_dat_0438bed8 = 4;
    }
    /* 0x4392a7: the knock-back */
    {
        const int dies = AI(a, 0xb98) == 1 &&
                         !((long double)AF(a, ACT_HP) - spill32((long double)dmg) > k0);
        if ((AI(a, 0xb20) != 0 || nohit != 0 || AI(a, 0xb54) == 1) && !dies) {
            AI(a, 0x440) = 0;
        } else {
            if (AI(a, 0xb54) == 2) AI(a, 0x440) /= 2;
            if (AI(a, 0xb40) == 0) { AI(a, 0xa54) = 0; AI(a, 0xa58) = 0; AI(a, 0xa5c) = 0; }
            if (AI(a, 0xb18) > 0) {
                AF(a, 0x3fc) = 0.0f;
                AF(a, 0x400) = 0.0f;
                AF(a, 0x404) = 0.0f;
                actor_anim_set(a, 0);
            } else {
                actor_anim_set(a, 2);
                if (dies) {
                    AF(a, 0x3fc) = spill32((long double)kb0 * vx);
                    AF(a, 0x400) = k0p5;
                    AF(a, 0x404) = spill32((long double)kb0 * vz);
                } else {
                    AF(a, 0x3fc) = spill32((long double)kb * vx);
                    AF(a, 0x400) = k0p3;
                    AF(a, 0x404) = spill32((long double)kb * vz);
                }
            }
        }
    }
    /* 0x4393bf: the sparks */
    if (state == 0) {
        const uint8_t *oa = owner_of(RI(slot, 0x10));
        if (oa && *(const int32_t *)(const void *)(oa + 0xcec) != 0) {
            /* PORT-DEBT(stub, FUN_004147d5): the overlay spark (25, 1.0, -1) */
        } else if (nohit == 0) {
            if (RI(slot, 0xb8) != 0) {
                scene1_spawn(0, ex, ey, ez, 3, k0p2, 1);
                scene1_spawn(0, ex, ey, ez, 15, k0p2, 1);
            } else {
                scene1_spawn(0, ex, ey, ez, 1, k0p2, 1);
                scene1_spawn(0, ex, ey, ez, 25, k0p2, 1);
            }
        } else {
            scene1_spawn(0, ex, ey, ez, 41, k0p2, 1);
            scene1_spawn(0, ex, ey, ez, 42, k0p2, 1);
        }
    } else {
        scene1_spawn(0, ex, ey, ez, 1, k0p2, 1);
        scene1_spawn(0, ex, ey, ez, 25, k0p2, 1);
    }
    if ((rtype == 4 || rtype == 0x52) && dmg > 0) scene1_spawn(0, hx, hy, hz, 0x98, k0p2, 1);
    int se = 319;
    if (rtype == 8) se = 377;
    else if (rtype == 0x53) se = 687;
    else if (nohit != 0) se = 359;
    else if (state == 0) {
        if (RI(slot, 0xb8) != 0) se = 328;
        else if (rtype == 0x5b) se = 319;
        else if (rtype == 0x5c || rtype == 0x5f) se = 679;
        else if (rtype == 0x85 || rtype == 0x86 || rtype == 0x87) se = (rng_next15() & 1) ? 319 : 679;
        else if (rtype == 2 || rtype == 3 || rtype == 0x6d || rtype == 0x6f || rtype == 0x70) se = 339;
    }
    audio_play_se_by_id((uint16_t)se);
    g_scene1_combat_dat_0438b904 = idx;
    g_scene1_combat_dat_0438b908 = 180;
    if (state == 0 && RI(slot, 0xb8) != 0) g_scene1_combat_dat_0438bed8 = 8;
    g_scene1_combat_dat_06a46f94 = dmg;
    {
        const int32_t hpi = x87_ftol(AF(a, ACT_HP));
        if (dmg > hpi) g_scene1_combat_dat_06a46f94 = hpi;
    }
    if (rtype != 0x53 && AI(a, ACT_INUSE) == 1) actor_damage(a, dmg, bits);   /* FUN_0042e791 */
    if (AI(a, 0xb54) != 0 && AF(a, ACT_HP) == k0) actor_anim_set(a, 2);
    AF(a, 0xab4) = 1.0f;
    return 1;
}

/* the stage objects' kind table (.data 0x5c4c90, 9 dwords a kind): [3] radius,
 * [4] height */
static const float k_obj_r[22] = { 3.0f, 2.0f, 1.0f, 1.0f, 5.0f, 5.0f, 4.0f, 0.30000001192092896f,
                                   0.30000001192092896f, 0.800000011920929f, 0.800000011920929f,
                                   0.800000011920929f, 0.800000011920929f, 0.800000011920929f,
                                   0.800000011920929f, 0.800000011920929f, 0.800000011920929f,
                                   0.0f, 0.0f, 0.800000011920929f, 0.800000011920929f, 0.0f };
static const float k_obj_h[22] = { 10.0f, 2.0f, 10.0f, 10.0f, 6.0f, 6.0f, 6.0f, 2.0f, 2.0f,
                                   10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f,
                                   10.0f, 10.0f, 10.0f, 10.0f, 10.0f };

/* FUN_004458c4: claim the first free table-B slot for a trap's falling body —
 * 44 (a rock: random X / Z tumble, 2 up, scale 0.6) or 140 (a sliding block:
 * random yaw, 0.2 along `ang`, scale 0.3); other types claim nothing */
static void trap_body_spawn(int32_t type, float x, float y, float z, float ang)
{
    static const float k0p6 = 0.6000000238418579f, k0p3s = 0.30000001192092896f;
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        int32_t *s = &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
        if (RI(s, 0x00) != 0) continue;
        RI(s, 0x04) = 3;
        RI(s, 0x98) = 0;
        RF(s, 0x8c) = 0.0f; RF(s, 0x90) = 0.0f; RF(s, 0x94) = 0.0f;
        RI(s, 0x00) = type;
        RF(s, 0x68) = 0.0f; RF(s, 0x6c) = 0.0f; RF(s, 0x70) = 0.0f;
        RF(s, 0x5c) = x; RF(s, 0x60) = y; RF(s, 0x64) = z;
        RI(s, 0x14) = 0;
        RI(s, 0x10) = 0;
        RF(s, 0xa8) = 0.0f;
        RF(s, 0x108) = 1.0f;
        RI(s, 0xa0) = -1;
        RI(s, 0x110) = 0;
        RI(s, 0x9c) = 0;
        RI(s, 0x0c) = -1;
        RI(s, 0x11c) = g_scene1_record_b_seq_counter++;
        RF(s, 0xb4) = 1.0f;
        RI(s, 0xb8) = 0;
        RI(s, 0xc4) = 0;
        float *m = &RF(s, 0xc8);
        if (type == 44) {
            RF(s, 0x8c) = spill32((long double)rng_next_unit() * k_2pi);
            RF(s, 0x94) = spill32((long double)rng_next_unit() * k_2pi);
            float r[16];
            mat4_rotation_x(m, RF(s, 0x8c));        /* FUN_004a35ef */
            mat4_rotation_y(r, RF(s, 0x94));        /* FUN_004a3553 */
            mat4_mul(m, r, m);                      /* FUN_004a2a10 */
            RF(s, 0x60) = spill32((long double)RF(s, 0x60) + k2);
            RF(s, 0x68) = 0.0f; RF(s, 0x6c) = 0.0f; RF(s, 0x70) = 0.0f;
            RF(s, 0x94) = spill32((long double)rng_next_unit() * k_2pi);
            RF(s, 0x108) = k0p6;
            RF(s, 0xa8) = 0.0f;
        } else if (type == 140) {
            const float yaw = spill32((long double)rng_next_unit() * k_2pi);
            RF(s, 0x90) = yaw;
            mat4_rotation_x(m, yaw);                /* FUN_004a35ef */
            const double ad = ang;
            RF(s, 0x68) = spill32((long double)spill32(x87_crt_sin(ad)) * k0p2);
            RF(s, 0x6c) = 0.0f;
            RF(s, 0x70) = spill32((long double)spill32(x87_crt_cos(ad)) * k0p2);
            RF(s, 0x108) = k0p3s;
        } else {
            continue;
        }
        return;
    }
}

/* FUN_004412b6 kinds 6 / 8 / 10: 16 dust puffs (type 14) on a ring of radius
 * 3..6 around (x, z) */
static void trap_dust_ring(float x, float z)
{
    static const float k0p75 = 0.75f;
    for (int32_t k = 0; k != 16; k++) {
        const float a = spill32((long double)spill32((long double)k) * k_2pi / 8.0f + k_pi4);
        const float r = spill32(((long double)rng_next_unit() + k1) * k3);
        const double ad = a;
        const float sx = spill32((long double)spill32(x87_crt_sin(ad)) * r + x);
        const float sz = spill32((long double)spill32(x87_crt_cos(ad)) * r + z);
        const float pz = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + sz);
        const float px = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + sx);
        scene1_spawn(0, px, k0p5, pz, 14, k0p75, 1);
    }
}

int chest_trap(float x, float z, int32_t kind)
{
    static const float k9 = 9.0f, k10 = 10.0f, k3c = 3.0f;
    const uint8_t *w = work();
    if ((w && w[0x2bd20] == 1) || party_accessory_proc(2508, 4, 30)) return 0;   /* FUN_0048584f */
    const actor_stage_t st = { g_scene1_combat_stage_id, stage_gate_get_next(),
                               stage_post_load_get_mode_b4d0(), g_ghost_count,
                               w ? w[0x2bc57] : 0 };
    switch (kind) {
    case 4:
        for (int32_t k = 0; k != 4; k++) {
            float ang = spill32((long double)spill32((long double)k) * k_2pi / k4 + k_pi4);
            for (int tries = 0; tries != 10; tries++) {
                const double ad = ang;
                float sx = spill32((long double)spill32(x87_crt_sin(ad)) * k9);
                float sz = spill32((long double)spill32(x87_crt_cos(ad)) * k9);
                float frac = 0.0f;
                mapcol_hit_t h;
                h.tri = 0;
                h.slot = 0;
                float ex, ez;
                if (mapcol_ray(x, k0p5, z, sx, 0.0f, sz, &frac, &h) != 0) {
                    ex = spill32(((long double)frac * sx) * k0p8 + x);
                    ez = spill32(((long double)frac * sz) * k0p8 + z);
                } else {
                    ex = spill32((long double)sx + x);
                    ez = spill32((long double)sz + z);
                }
                const float dx = spill32((long double)g_party.pos[0][0] - ex);
                const float dz = spill32((long double)g_party.pos[0][2] - ez);
                const float d = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
                if (!(d < k3c)) {
                    actor_spawn_enemy(&st, ex, k0p5, ez, 2);
                    break;
                }
                ang = spill32((long double)ang + k_pi4);
            }
        }
        break;
    case 5:
        for (int32_t k = 0; k != 8; k++) {
            const float ang = spill32((long double)spill32((long double)k) * k_2pi / 8.0f);
            const double ad = ang;
            const float ex = spill32((long double)spill32(x87_crt_sin(ad)) * k10 + x);
            const float ez = spill32((long double)spill32(x87_crt_cos(ad)) * k10 + z);
            actor_t *a = actor_spawn_enemy(&st, ex, 0.0f, ez, 2);
            if (a) {
                AF(a, 0xb48) = x;
                AF(a, 0xb4c) = 0.0f;
                AI(a, ACT_INUSE) = 1;
                AI(a, 0xb40) = 1;
                AF(a, 0xb50) = z;
                AI(a, 0xb44) = k;
            }
        }
        break;
    case 9: {   /* the rolling rock: enemy 37 rolling out 9 at π/4 */
        const float ang = spill32((long double)k0 / k4 + k_pi4);
        const double ad = ang;
        const float sx = spill32((long double)spill32(x87_crt_sin(ad)) * k9);
        const float sz = spill32((long double)spill32(x87_crt_cos(ad)) * k9);
        float frac = 0.0f;
        mapcol_hit_t h;
        h.tri = 0;
        h.slot = 0;
        float ex, ez;
        if (mapcol_ray(x, k0p5, z, sx, 0.0f, sz, &frac, &h) != 0) {
            ex = spill32((long double)frac * sx + x);
            ez = spill32((long double)frac * sz + z);
        } else {
            ex = spill32((long double)sx + x);
            ez = spill32((long double)sz + z);
        }
        actor_t *a = actor_spawn_enemy(&st, ex, k0p5, ez, 37);
        if (a) {   /* retail does not check */
            AI(a, 0xaf0) = 0;
            AI(a, 0xb98) = 1;
            AF(a, ACT_HP) = 0.0f;
            AI(a, ACT_INUSE) = 2;
            AI(a, 0xb40) = 2;
            actor_anim_set(a, 2);
        }
        break;
    }
    case 1: {   /* three sliding blocks (140) at 15 / 18 / 21, aimed back in */
        static const float k15 = 15.0f;
        for (int32_t k = 0; k != 9; k += 3) {
            const float ang = spill32((long double)rng_next_unit() * k_2pi);
            const float r = spill32((long double)spill32((long double)k) + k15);
            const double ad = ang;
            const float sx = spill32((long double)spill32(x87_crt_sin(ad)) * r + x);
            const float sz = spill32((long double)spill32(x87_crt_cos(ad)) * r + z);
            trap_body_spawn(140, sx, 0.0f, sz, spill32((long double)ang + k_pi));
        }
        break;
    }
    case 10:   /* the bomb: the dust ring + the spin-out (b930) */
        g_dat_0438b930 = 1;
        trap_dust_ring(x, z);
        break;
    case 6:
    case 8: {   /* the gas: the dust ring; 6 stuns (db0fc / actor +0xb14 180
                 * within 12), 8 slows (db100 1800) */
        static const float k12 = 12.0f;
        trap_dust_ring(x, z);
        if (kind == 6) {
            float dx = spill32((long double)g_party.pos[0][0] - x);
            float dz = spill32((long double)g_party.pos[0][2] - z);
            float d = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
            if (d < k12) PW(0x56db0fcu).i = 180;
            for (int i = 0; i < ACTOR_COUNT; i++) {
                actor_t *a = &g_actor[i];
                const int32_t use = AI(a, ACT_INUSE);
                if (use == 0 || AI(a, 0xaf4) > 0 || AI(a, 0xb08) != 0) continue;
                if (use != 1 && (AI(a, 0xb98) == 0 || use != 2)) continue;
                dx = spill32((long double)AF(a, ACT_POS + 0) - x);
                dz = spill32((long double)AF(a, ACT_POS + 8) - z);
                d = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
                if (d < k12) AI(a, 0xb14) = 180;
            }
        } else {
            PW(0x56db100u).i = 1800;
        }
        break;
    }
    case 2:
    case 3: {   /* the falling rocks: ten 44s over a 2..8 ring, 11..15 + k up
                 * (kind 2: 4 higher) */
        static const float k6 = 6.0f, k11 = 11.0f;
        for (int32_t k = 0; k != 20; k += 2) {
            const float ang = spill32((long double)rng_next_unit() * k_2pi);
            const float r = spill32((long double)rng_next_unit() * k6 + k2);
            const double ad = ang;
            const float sx = spill32((long double)spill32(x87_crt_sin(ad)) * r + x);
            const long double u4 = (long double)rng_next_unit() * k4;
            const float fk = spill32((long double)k);
            float sy = spill32(u4 + fk + k11);
            const float sz = spill32((long double)spill32(x87_crt_cos(ad)) * r + z);
            if (kind == 2) sy = spill32((long double)sy + k4);
            trap_body_spawn(44, sx, sy, sz, 0.0f);
        }
        break;
    }
    default:
        break;
    }
    return 1;
}

/* FUN_00441aab: Louie's trap sense (chara 1: a kind-5 chest within 3 pops the
 * "!" — db004 0, db008 1), then the floor's trap spots: a live spot (floor
 * event 21 shows them all, else its own flag) within 6 of the player fires
 * FUN_004412b6 (fired: db00c 60 + SE 00re_btl04) and is spent */
void trap_spots_tick(void)
{
    static const float k6 = 6.0f;
    if (chara_equip_get_current_chara() == 1) {
        for (int i = 0; i < DOBJ_COUNT; i++) {
            const dobj_t *o = &g_dobj[i];
            const int32_t kind = o->i[DOBJ_KIND];
            if (kind == -1 || o->i[DOBJ_STATE] == 3 || kind != 5) continue;
            const float dx = spill32((long double)o->f[DOBJ_POS + 0] - g_party.pos[0][0]);
            const float dz = spill32((long double)o->f[DOBJ_POS + 2] - g_party.pos[0][2]);
            if (!(dx == k0 && dz == k0)) {
                const float d = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
                if (!(d < k3)) continue;
            }
            PW(0x56db004u).i = 0;
            PW(0x56db008u).i = 1;
        }
    }
    for (int i = 0; i < FLOOR_PICK_MAX; i++) {
        if (g_floor_pick_type[i] == 0) continue;
        if (!floor_event_active(21) && g_floor_pick_flag[i] == 0) continue;
        const float *p = g_floor_pick_pos[i];
        const float dx = spill32((long double)p[0] - g_party.pos[0][0]);
        const float dz = spill32((long double)p[2] - g_party.pos[0][2]);
        if (!(dx == k0 && dz == k0)) {
            const float d = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
            if (!(d < k6)) continue;
        }
        if (chest_trap(p[0], p[2], g_floor_pick_type[i]) != 0) {
            PW(0x56db00cu).i = 60;
            audio_play_se_file("bin/se/00re/action/00re_btl04.bin");
        }
        g_floor_pick_type[i] = 0;
    }
}

/* FUN_0043824b's rarity tallies: an item's three dungeon bytes (+0x44) are
 * tier or tier·10 + w (weight 11 - w; a plain tier weighs 1) */
static int32_t roll_weight(int8_t b, int32_t *tier)
{
    int32_t v = b, w = 1;
    if (v >= 10) {
        w = 11 - v % 10;
        v /= 10;
    }
    *tier = v;
    return w;
}

static int item_rollable(const item_record_t *it)
{
    return it->valid == 1 && it->price > 0;
}

/* FUN_0043824b: a treasure chest's item at (x, y + 0.5, z) — dungeon 1
 * floor 14's first chest is 3020; dungeon 5's floors 29 / 59 pick by x
 * (the two fixed lists or a weighted pick of tier >= 1 | 6); else a tier from
 * the weights DAT_0438bedc (rand % 1000), the weights re-carved
 * (FUN_00435dcd(0, tier)), and a weighted item of that tier.  1 = spawned. */
static int chest_item_roll(float x, float y, float z)
{
    static const int32_t k_list_a[22] = { 12, 111, 211, 311, 411, 511, 611, 711, 1011, 1012, 1112,
                                          1211, 1311, 1411, 1512, 1611, 1711, 2011, 2107, 2207, 2307, 2407 };
    static const int32_t k_list_b[21] = { 13, 113, 212, 313, 413, 513, 613, 713, 1014, 1113, 1213,
                                          1313, 1413, 1513, 1613, 1713, 2108, 2208, 2308, 2412, 2413 };
    static const float km42 = -42.0f, km38 = -38.0f;
    const int32_t n = g_item.count;
    const int32_t dg = g_scene1_combat_stage_id, fl = stage_gate_get_next();
    uint8_t *w = work();
    int32_t item = -1;
    if (dg == 1) {
        if (fl == 14 && w && w[0x2bc77] == 0) {
            w[0x2bc77] = 1;
            item = 3020;
        }
    } else if (dg == 5 && (fl == 29 || fl == 59)) {
        int high = 0, pick = 0;   /* 0 = the weighted pick, 1 / 2 = list a / b */
        if (!(x < km42)) {
            if (x > km38) {
                high = 1;
            } else if (fl == 29 && g_dat_0438b4d8 == 20) {
                if (rng_next15() & 1) pick = 1;
            } else {
                const uint32_t r = rng_next15() % 3u;
                if (r == 1) pick = 1;
                else if (r == 2) pick = 2;
            }
        }
        if (pick == 1) {
            item = k_list_a[rng_next15() % 22u] + 7;
        } else if (pick == 2) {
            item = k_list_b[rng_next15() % 21u] + 7;
        } else {
            /* two passes over the table: pass 0 tallies the weight of the
             * tiers >= `min` and re-rolls r = rand % tally after every item
             * once the tally is positive; pass 1 walks to r */
            const int32_t min = high ? 6 : 1;
            int32_t r = 0;
            for (int pass = 0; pass != 2; pass++) {
                int32_t acc = 0;
                for (int32_t k = 0; k < n; k++) {
                    const item_record_t *it = &g_item.records[k];
                    if (item_rollable(it)) {
                        for (int j = 0; j != 3; j++) {
                            int32_t t;
                            const int32_t wt = roll_weight((int8_t)it->stock_info[4 + j], &t);
                            if (t < min) continue;
                            if (pass == 1 && acc + wt >= r) {
                                scene1_records_c_spawn_pickup(0, x, spill32((long double)y + k0p5), z,
                                                              it->item_id + 7);
                                return 1;
                            }
                            acc += wt;
                        }
                    }
                    if (pass == 0 && acc > 0) r = (int32_t)(rng_next15() % (uint32_t)acc);
                }
            }
            return 0;
        }
    }
    if (item < 0) {
        static const float k1000 = 1000.0f;
        int32_t tier = 0;
        const float u = spill32((long double)(int32_t)(rng_next15() % 1000u));
        float cum = 0.0f;
        for (int k = 0; k < 6; k++) {
            cum = spill32((long double)stage_post_load_get_dat_0438bedc_as_float(k) * k1000 + cum);
            if (u < cum) { tier = k; break; }
        }
        g_dat_06a46f8c = 1;
        g_dat_06a46f90 = tier;
        stage_post_load_pulse_first_row(0, tier);   /* FUN_00435dcd */
        const int32_t want = tier + 1;
        int32_t total = 0;
        for (int32_t k = 0; k < n; k++) {
            const item_record_t *it = &g_item.records[k];
            if (!item_rollable(it)) continue;
            for (int j = 0; j != 3; j++) {
                int32_t t;
                const int32_t wt = roll_weight((int8_t)it->stock_info[4 + j], &t);
                if (t == want) total += wt;
            }
        }
        if (total <= 0) return 0;
        const int32_t r = (int32_t)(rng_next15() % (uint32_t)total);
        int32_t acc = 0;
        for (int32_t k = 0; k < n && item < 0; k++) {
            const item_record_t *it = &g_item.records[k];
            if (!item_rollable(it)) continue;
            for (int j = 0; j != 3; j++) {
                int32_t t;
                const int32_t wt = roll_weight((int8_t)it->stock_info[4 + j], &t);
                if (t != want) continue;
                if (acc + wt >= r) { item = it->item_id + 7; break; }
                acc += wt;
            }
        }
        if (item < 0) return 0;
    }
    scene1_records_c_spawn_pickup(0, x, spill32((long double)y + k0p5), z, item);
    return 1;
}

/* FUN_0043865e's object arm (0x439f28..0x43a5d4): the first stage object in
 * reach (state 0; not kinds 8..13 / 18 / 19 / 22 / 30) takes the hit — pots
 * break (ATTR hits), chests open (kind 5: the trap; kind 4: the treasure) */
int record_hit_objects(int32_t *slot)
{
    const int32_t state = RI(slot, 0x04);
    if (state == 1 || state == 3) return 0;
    const float px = RF(slot, 0x5c), py = RF(slot, 0x60), pz = RF(slot, 0x64);
    const float reach = RF(slot, 0xa8);
    dobj_t *o = NULL;
    float dx = 0, dy = 0, dz = 0;
    for (int i = 0; i < DOBJ_COUNT; i++) {
        dobj_t *c = &g_dobj[i];
        const int32_t kind = c->i[DOBJ_KIND];
        if (kind == -1) continue;
        const int32_t st = c->i[DOBJ_STATE];
        if (st == 3 || st == 7) continue;
        if (kind == 0x16 || kind == 0x1e || kind == 9 || kind == 10 || kind == 0x12 ||
            kind == 0x13 || kind == 0xc || kind == 0xd || kind == 0xb || kind == 8)
            continue;
        if (st != 0) continue;
        int h = 0;
        while (h < 10 && RI(slot, 0x11c) != c->i[DOBJ_LINKS + h]) h++;
        if (h < 10) continue;
        dx = spill32((long double)c->f[DOBJ_POS + 0] - px);
        dy = spill32((long double)c->f[DOBJ_POS + 1] - py);
        dz = spill32((long double)c->f[DOBJ_POS + 2] - pz);
        if (dx == k0 && dz == k0) dz = k0p01;
        float g = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
        g = spill32((long double)g - reach);
        const int ki = kind >= 0 && kind < 22 ? kind : 0;
        const float rr = spill32((long double)k_obj_r[ki] * c->f[DOBJ_SCALE + 0]);
        const float hh = spill32((long double)k_obj_h[ki] * c->f[DOBJ_SCALE + 0]);
        if (!(g < rr)) continue;
        if (!(dy < reach)) continue;
        if (!(-((long double)hh + reach) < dy)) continue;
        o = c;
        break;
    }
    if (o == NULL) return 0;
    o->i[DOBJ_LINKS + o->i[166]] = RI(slot, 0x11c);
    o->i[166] = (o->i[166] + 1) % 10;
    o->i[9] = 5;
    const int32_t rtype = RI(slot, 0x00);
    if (rtype == 2 || rtype == 0x54 || rtype == 0x6d || rtype == 0x6f || rtype == 0x70)
        PW(0x56da1b8u).i |= 2;
    const int32_t kind = o->i[DOBJ_KIND];
    const float ox = o->f[DOBJ_POS + 0], oy = o->f[DOBJ_POS + 1], oz = o->f[DOBJ_POS + 2];
    if (kind != 2 && kind != 3) {
        audio_play_se_by_id(kind == 0x15 ? 0x180 : 0x169);
    } else {
        o->i[9] = 0;
        scene1_spawn(0, spill32(ox - (long double)dx * k0p5), spill32((long double)o->f[DOBJ_SCALE + 1] * k0p5 + oy),
                     spill32(oz - (long double)dz * k0p5), 0x15, o->f[DOBJ_SCALE + 0], 6);
        audio_play_se_by_id(0x159);
    }
    if (o->i[DOBJ_KIND] == 0)
        scene1_spawn(0, spill32(ox - (long double)dx * k0p5), spill32((long double)o->f[DOBJ_SCALE + 1] * 20.5f + oy),
                     spill32(oz - (long double)dz * k0p5), 0x16, o->f[DOBJ_SCALE + 0], 6);
    int ret = 1;
    if (o->i[DOBJ_ATTR] == -1) {
        if (o->i[DOBJ_KIND] == 2 || o->i[DOBJ_KIND] == 3) return 1;
        scene1_spawn(0, px, py, pz, 1, k0p2, 1);
        return 1;
    }
    if (o->i[DOBJ_ATTR] > 0) --o->i[DOBJ_ATTR];
    if (o->i[DOBJ_ATTR] > 0) {
        scene1_spawn(0, px, py, pz, 1, k0p2, 1);
        return 1;
    }
    o->i[DOBJ_ATTR] = 0;
    uint8_t *w = work();
    switch (o->i[DOBJ_KIND]) {
    case 6:
        o->i[DOBJ_STATE] = 1;
        break;
    case 4: case 5: case 8:
        if (w && w[0x2bc82] != 0) {
            o->i[DOBJ_STATE] = 2;
            g_portal_prompt = 1;
            break;
        }
        if (o->i[DOBJ_KIND] == 5) {
            const int32_t cx = x87_ftol(((long double)ox - k20) / km40);
            const int32_t cz = x87_ftol(((long double)oz - k20) / km40);
            const int32_t var = g_floor_rooms[cz * FLOOR_GRID_STRIDE + cx].var;
            int fired = 0;
            if (g_scene1_combat_stage_id == 0) {
                fired = chest_trap(ox, oz, var >= 5 ? 5 : 4);
            } else if (rng_next15() % 10u == 0) {
                (void)chest_trap(ox, oz, 10);   /* the leftover ecx = 10 */
            } else {
                static const int32_t k_trap[4] = { 8, 9, 6, 4 };
                int32_t t = k_trap[rng_next15() & 3];
                if (t == 4 && var >= 5) t = 5;
                fired = chest_trap(ox, oz, t);
            }
            o->i[DOBJ_STATE] = 2;
            if (fired == 1) {
                const uint32_t v = rng_next15() % 3u;
                if (v == 0) audio_play_se_file("bin/se/01ti/tuika/ti_wana_b.bin");
                else if (v == 1) audio_play_se_file("bin/se/01ti/tuika/ti_wana_a.bin");
                else audio_play_se_file("bin/se/00re/event/re_wana.bin");
                PW(0x56db00cu).i = 60;
            }
            if (w) { int32_t n; memcpy(&n, w + 0x2c3e0, 4); n += 2; memcpy(w + 0x2c3e0, &n, 4); }
        } else {
            PW(0x56db004u).i = 4;
            PW(0x56db008u).i = 1;
            if (party_bag_full()) {                    /* FUN_00484e45 */
                o->i[DOBJ_ATTR] = 1;                   /* stays shut */
                actor_say_set(1, 0, 2, 0, 180);        /* FUN_00485413 */
            } else {
                o->i[DOBJ_STATE] = 2;
                const float iy = spill32((long double)oy + k0p5);
                if (w && w[0x2bd21] == 1) {
                    for (int k = 1000; k != 0; --k)
                        (void)chest_item_roll(ox, iy, oz);   /* misses log to FUN_0048a348 */
                } else {
                    (void)chest_item_roll(ox, iy, oz);
                }
                if (w) { int32_t n; memcpy(&n, w + 0x2c3e0, 4); n += 2; memcpy(w + 0x2c3e0, &n, 4); }
            }
        }
        break;
    case 0x15:
        o->i[DOBJ_KIND] = -1;
        for (int32_t k = 0; k != 5; k++) {
            const float off = spill32((long double)spill32((long double)k) * k4);
            scene1_spawn(0, ox, spill32(((long double)rng_next_unit() + oy) + off), oz, 2, k0p4, 1);
            scene1_spawn(0, ox, spill32(((long double)rng_next_unit() + oy) + off), oz, 0xf, k0p8, 1);
        }
        break;
    default:
        o->i[DOBJ_STATE] = 1;
        scene1_spawn(0, px, py, pz, 2, k0p2, 1);
        break;
    }
    if (o->i[DOBJ_D36] == 0) o->i[DOBJ_D36] = ret;
    return ret;
}

/* FUN_00485979: the active chara's hurt voice (chara 6 has a KO set) */
void party_hurt_voice(int down)
{
    static const char *const k_li[4] = {
        "bin/se/02li/action/02li_10.bin", "bin/se/02li/action/02li_11a.bin",
        "bin/se/02li/action/02li_11b.bin", "bin/se/02li/action/02li_11c.bin" };
    static const char *const k_sy[4] = {
        "bin/se/03sy/action/03sy_09a.bin", "bin/se/03sy/action/03sy_09b.bin",
        "bin/se/03sy/action/03sy_10a.bin", "bin/se/03sy/action/03sy_10b.bin" };
    static const char *const k_ca[5] = {
        "bin/se/04ca/damage/04ca_14a.bin", "bin/se/04ca/damage/04ca_15a.bin",
        "bin/se/04ca/damage/04ca_15b.bin", "bin/se/04ca/damage/04ca_16a.bin",
        "bin/se/04ca/damage/04ca_16b.bin" };
    static const char *const k_ti[6] = {
        "bin/se/05ti/damage/05ti_15a.bin", "bin/se/05ti/damage/05ti_16.bin",
        "bin/se/05ti/damage/05ti_16a.bin", "bin/se/05ti/damage/05ti_17.bin",
        "bin/se/05ti/damage/05ti_17c.bin", "bin/se/05ti/damage/05ti_18.bin" };
    static const char *const k_el[7] = {
        "bin/se/06el/damage/dmg01.bin", "bin/se/06el/damage/dmg02.bin",
        "bin/se/06el/damage/dmg03.bin", "bin/se/06el/damage/dmg04.bin",
        "bin/se/06el/damage/dmg05.bin", "bin/se/06el/damage/dmg06.bin",
        "bin/se/06el/damage/dmg07.bin" };
    static const char *const k_na[6] = {
        "bin/se/07na/damage/n_14_a.bin", "bin/se/07na/damage/n_14_b.bin",
        "bin/se/07na/damage/n_15_a.bin", "bin/se/07na/damage/n_15_b.bin",
        "bin/se/07na/damage/n_16_a.bin", "bin/se/07na/damage/n_16_b.bin" };
    static const char *const k_gr_ko[3] = {
        "bin/se/08gr/damage/08gr_16.bin", "bin/se/08gr/damage/08gr_16b.bin",
        "bin/se/08gr/damage/08gr_16c.bin" };
    static const char *const k_gr[7] = {
        "bin/se/08gr/damage/08gr_13.bin", "bin/se/08gr/damage/08gr_13b.bin",
        "bin/se/08gr/damage/08gr_14.bin", "bin/se/08gr/damage/08gr_14a.bin",
        "bin/se/08gr/damage/08gr_14b.bin", "bin/se/08gr/damage/08gr_15.bin",
        "bin/se/08gr/damage/08gr_15a.bin" };
    static const char *const k_ar[3] = {
        "bin/se/09ar/damage/09ar_06.bin", "bin/se/09ar/damage/09ar_07.bin",
        "bin/se/09ar/damage/09ar_08.bin" };
    const char *f = NULL;
    switch (chara_equip_get_current_chara()) {
    case 0: f = k_li[rng_next15() & 3]; break;
    case 1: f = k_sy[rng_next15() & 3]; break;
    case 2: f = k_ca[rng_next15() % 5u]; break;
    case 3: f = k_ti[rng_next15() % 6u]; break;
    case 4: f = k_el[rng_next15() % 7u]; break;
    case 5: f = k_na[rng_next15() % 6u]; break;
    case 6: f = down == 1 ? k_gr_ko[rng_next15() % 3u] : k_gr[rng_next15() % 7u]; break;
    case 7: f = k_ar[rng_next15() % 3u]; break;
    default: break;
    }
    if (f) audio_play_se_file(f);
}

/* FUN_00485c74: the player takes `dmg` — the hurt voice, the revive (2510)
 * and the low-HP heal (2505) accessories, the HP bar flash */
void party_take_damage(int32_t dmg)
{
    PW(0x56db0bcu).f = spill32((long double)PW(0x56db0bcu).f - spill32((long double)dmg));
    const uint8_t *m = member();
    int16_t hp_a = 0, hp_b = 0;
    if (m) { memcpy(&hp_a, m + 0x3c, 2); memcpy(&hp_b, m + 0x3e, 2); }
    if (PW(0x56dae9cu).i == 0 && PW(0x56daea4u).i == 0)
        party_hurt_voice(PW(0x56db0bcu).f > k0 ? 0 : 1);
    if (!(PW(0x56db0bcu).f > k0) && party_accessory_proc(2510, 1, 30))
        PW(0x56db0bcu).f = spill32((long double)(hp_b + hp_a));
    const float thr = spill32((long double)(hp_a / 2));
    if (PW(0x56db0bcu).f < thr && PW(0x56db0bcu).f > k0 && party_accessory_proc(2505, 5, 30)) {
        int32_t v = (hp_b + hp_a) / 2;
        if (v > 100) v = 100;
        PW(0x56db0bcu).f = spill32((long double)spill32((long double)v) + PW(0x56db0bcu).f);
    }
    if (PW(0x56db0bcu).f > k0) {
        if (g_dat_0438b8d8 == 0) g_dat_0438b8d4 = 30;
        g_dat_0438b8d8 = 120;
    }
    PW(0x56daff4u).i = 0;
}

/* FUN_0043865e's player arm (0x438933..0x439f23): an enemy record (state 1 /
 * 3) within 2 of the player: the guard (Louie / Nagi facing it; Charme's
 * counter), the damage roll (AT or MA vs the party's DF / MD), the knock-back,
 * the popup and SE.  -1 = no contact (the object scan runs), else the
 * state-machine result */
static int record_hit_player(int32_t *slot)
{
    static const float k5 = 5.0f, km5 = -5.0f, km0p2 = -0.20000000298023224f;
    const float px = RF(slot, 0x5c), py = RF(slot, 0x60), pz = RF(slot, 0x64);
    const float reach = RF(slot, 0xa8);
    const int32_t rtype = RI(slot, 0x00);
    float dx = spill32((long double)g_party.pos[0][0] - px);
    const float dy = spill32((long double)g_party.pos[0][1] - py);
    float dz = spill32((long double)g_party.pos[0][2] - pz);
    if (dx == k0 && dz == k0) dz = k0p01;
    const float d = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
    const float g = spill32((long double)d - reach);
    if (rtype == 0x53) {
        if (g < k2 && dy < reach && -((long double)reach + k5) < dy) {
            g_party.vel[0][0] = 0.0f;
            g_party.vel[0][2] = 0.0f;
            return 1;
        }
        return 0;
    }
    if (rtype == 0x27) {
        if (g < k2 && dy < k0p1 && dy > km5 && PW(0x56db108u).i == 0) {
            PW(0x56db108u).i = 500;
            PW(0x56db10cu).i = RI(slot, 0x08);
        }
        return 1;
    }
    if (!(g < k2) || !(dy < reach) || !(-((long double)reach + k5) < dy)) return -1;
    const float ex = spill32((long double)g_party.pos[0][0] - ((long double)dx + dx) / d);
    const float ey = py;
    const float ez = spill32((long double)g_party.pos[0][2] - ((long double)dz + dz) / d);
    if (RF(slot, 0x68) != k0 || RF(slot, 0x6c) != k0 || RF(slot, 0x70) != k0) {
        dx = RF(slot, 0x68);
        dz = RF(slot, 0x70);
    }
    if (dx == k0 && dz == k0) dz = k0p01;
    float kl = spill32(crt_sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
    if (kl > k0) kl = spill32((long double)(rtype == 0x24 ? k1 : k0p3) / kl);
    int guard = 0;
    if (PW(0x56db048u).i == 0) {
        const float v = hit_angle(dx, dz, PW(0x56db05cu).f);
        if (v < k_pi4 && v > k_mpi4) {
            const int32_t c = chara_equip_get_current_chara();
            if (c == 0 || c == 5) {
                if (rtype == 0x56 || rtype == 0x96) { guard = 1; audio_play_se_by_id(361); }
                if (c == 0 && (rtype == 0x1f || rtype == 0x51 || rtype == 0x6c)) {
                    guard = 2;
                    audio_play_se_by_id(361);
                }
            } else if (c == 1 && PW(0x56dae30u).i > 0 && PW(0x56dae30u).i < 30) {
                guard = 1;
                PW(0x56dae38u).i = 30;
                party_anim_set(g_party.rec[0], 6);
                audio_play_se_by_id(379);
            }
        }
    }
    if (guard) {
        PW(0x56db0f4u).i = 15;
        g_scene1_combat_dat_0438bed8 = 4;
        PW(0x56dab04u).i = 4;
    } else {
        PW(0x56dab04u).i = 6;
        PW(0x56db0f4u).i = 30;
        g_scene1_combat_dat_0438bed8 = 4;
    }
    const uint8_t *ob = owner_of(RI(slot, 0x14));
    const int32_t ot = ob ? *(const int32_t *)(const void *)(ob + ACT_TYPE) : 26;
    const enemy_record_t *oe = ob ? enemy_at(ot) : &g_enemy[26];
    int magic = 0;
    switch (rtype) {
    case 18: case 20: case 31: case 33: case 38: case 42: case 43: case 70: case 90:
    case 107: case 108: case 137: case 152: case 153: case 154:
        magic = 1;
        break;
    default:
        break;
    }
    int32_t dmg;
    int proc;
    if (magic) {
        const float x = spill32(enemy_level_mul(ot) * spill32((long double)oe->ma));
        const float df = spill32((long double)PW(0x56db0b8u).i);
        dmg = x87_ftol((long double)x / k2 - (long double)df / k4);
        proc = party_accessory_proc(2503, 10, 5);
    } else {
        const float x = spill32(enemy_level_mul(ot) * spill32((long double)oe->at));
        const float df = spill32((long double)PW(0x56db0b0u).i);
        dmg = x87_ftol((long double)x / (rtype == 0xf ? k4 : k2) - (long double)df / k4);
        proc = party_accessory_proc(2502, 10, 5);
    }
    if (proc) dmg /= 4;
    if (PW(0x56dae9cu).i != 0 || PW(0x56daea4u).i != 0) {
        const int32_t fl = PW(0x56daeacu).i & 0xff;
        if (fl & 0x20) dmg /= 4;
        else if (fl & 0x10) dmg /= 3;
        else dmg /= 2;
    }
    if (floor_event_active(4) || floor_event_active(3)) dmg *= 2;
    if (floor_event_active(8) || floor_event_active(6)) dmg /= 2;
    if (PW(0x56db018u).i > 0 || PW(0x56db01cu).i > 0) dmg /= 2;
    int32_t hit = 0;
    if (!guard) {
        if (rtype == 0x11 && PW(0x56db0fcu).i < 120) PW(0x56db0fcu).i = 120;
        hit = x87_ftol((long double)spill32((long double)dmg) * RF(slot, 0xb4));
        if (hit < 1) hit = 1;
        if (hit >= 5) {
            const uint32_t q = (uint32_t)rng_next15();
            hit += (int32_t)(q % (uint32_t)(hit / 5));
        } else {
            hit += (int32_t)(rng_next15() & 1);
        }
        if (ob && *(const int32_t *)(const void *)(ob + 0xb40) == 2) {
            const int32_t cap = x87_ftol(PW(0x56db0bcu).f) - 1;
            if (hit > cap) hit = cap;
        }
        party_take_damage(hit);                       /* FUN_00485c74 */
    }
    g_scene1_combat_dat_06a46f94 = hit;
    if ((PW(0x56dae9cu).i != 0 || PW(0x56daea4u).i != 0 || PW(0x56daea0u).i != 0) &&
        PW(0x56db0bcu).f > k0) {
        PW(0x56daeb0u).i = 0;
    } else if (guard) {
        PW(0x56daeb0u).i = 0;
        const float t = spill32((long double)kl * dx);
        g_party.vel[0][0] = spill32((long double)t * k0p5);
        const float u = spill32((long double)kl * dz);
        g_party.vel[0][2] = spill32((long double)u * k0p5);
        if (rtype == 0x56 || rtype == 0x96) {
            RF(slot, 0x68) = spill32((long double)t * km0p2);
            RF(slot, 0x70) = spill32((long double)u * km0p2);
        }
    } else if (rtype == 0xa0) {
        PW(0x56daeb0u).i = 0;
        PW(0x56db0f4u).i = 4;
        g_scene1_combat_dat_0438bed8 = 2;
        PW(0x56db048u).i = 1;
        party_anim_set(g_party.rec[0], 2);
        g_party.vel[0][0] = spill32((long double)dx * k0p1);
        g_party.vel[0][1] = 0.0f;
        g_party.vel[0][2] = spill32((long double)dz * k0p1);
    } else if (rtype == 0x13 && PW(0x56db0bcu).f > k0) {
        uint8_t *o2 = owner_of(RI(slot, 0x14));
        if (o2) *(int32_t *)(void *)(o2 + 0xa54) = 1;
    } else {
        PW(0x56daeb0u).i = (rtype == 0xe || rtype == 0x12 || rtype == 0xa1) ? 70 : 30;
        PW(0x56db048u).i = 1;
        party_anim_set(g_party.rec[0], 2);
        g_party.vel[0][0] = spill32((long double)kl * dx);
        g_party.vel[0][1] = k0p4;
        g_party.vel[0][2] = spill32((long double)kl * dz);
    }
    if (hit == 0 && PW(0x56daeb0u).i > 2) PW(0x56daeb0u).i = 2;
    if (!(PW(0x56db0bcu).f > k0)) {
        g_floor_end_kind = 1;
        PW(0x56db0bcu).f = 0.0f;
        g_party.vel[0][1] = k0p5;
    }
    if (rtype == 0x13 && ob)
        scene1_spawn(0, ex, ey, ez, 0x75, k0p2, *(const int32_t *)(const void *)(ob + 0xb3c));
    if (guard == 0) {
        popup_push(g_party.pos[0][0], g_party.pos[0][1], g_party.pos[0][2], hit, 5);
        int se = 319;
        if (rtype == 0xb || rtype == 0x13 || rtype == 0x14) se = 331;
        else if (rtype == 0xd || rtype == 0x10 || rtype == 0x11 || rtype == 0x16 || rtype == 0x17) se = 341;
        else if (rtype == 0x1f) se = 350;
        audio_play_se_by_id((uint16_t)se);
        if (magic) {
            scene1_pfo_table_a_alloc_passthrough(0, ex, ey, ez, 11, 1.0f, -1, 0, 0);
        } else {
            scene1_spawn(0, ex, ey, ez, 1, k0p2, 1);
            scene1_spawn(0, ex, ey, ez, 25, k0p2, 1);
        }
        return 1;
    }
    scene1_spawn(0, ex, ey, ez, 1, k0p1, 1);
    scene1_spawn(0, ex, ey, ez, 25, k0p1, 1);
    if (guard == 2) RI(slot, 0x00) = 0;
    return 2;
}

/* FUN_0043865e: the record state machine — the pause gates, the actor arm,
 * the player arm (records in state 1 / 3), the object scan */
int record_sm_tick(int32_t *slot)
{
    if (g_floor_end_kind > 0 || g_portal_prompt > 0 || g_equip_prompt > 0 ||
        g_scene1_ingame_paused_flag != 0)
        return 0;
    g_scene1_records_b_tick_flag = 1;
    if (record_hit_actors(slot)) return 1;
    const int32_t state = RI(slot, 0x04);
    if ((state == 1 || state == 3) && PW(0x56db0f4u).i <= 0) {
        const int r = record_hit_player(slot);
        if (r >= 0) return r;
    }
    return record_hit_objects(slot);                /* 0x439f28 */
}

/* the table-B tick's wall probe: FUN_00433674 (frac, +0x10 tri, +0x14 slot,
 * +0x18 object) and the hit triangle's type */
static int wall_ray(float ox, float oy, float oz, float dx, float dy, float dz,
                    scene1_b_wall_ray_result_t *out)
{
    mapcol_hit_t h;
    memset(&h, 0, sizeof h);
    float frac = 0.0f;
    const int r = mapcol_ray(ox, oy, oz, dx, dy, dz, &frac, &h);
    out->t = frac;
    out->wall_x = h.tri;
    out->wall_z = h.slot;
    out->wall_id = h.obj;
    return r;
}

static int wall_type(int32_t tri, int32_t slot)
{
    if (slot < 0 || slot >= MAPCOL_SLOTS || g_mapcol_slot[slot].tris == NULL ||
        tri < 0 || tri >= g_mapcol_slot[slot].cap)
        return 0;
    return g_mapcol_slot[slot].tris[tri].type;
}

/* the table-B tick's ground probe: FUN_00432e50 into the caller's hit record */
static int ground_hit(float x, float y, float z, int32_t *out)
{
    return mapcol_ground(x, y, z, (mapcol_hit_t *)(void *)out);
}

void record_sm_install(void)
{
    scene1_records_b_set_ground_query_hook(ground_hit);
    scene1_records_b_set_wall_raycast_hook(wall_ray);
    scene1_records_b_set_wall_flag_at_hook(wall_type);
    scene1_records_b_set_state_machine_hook(record_sm_tick);
    scene1_records_b_set_notify_queue_hook(camera_shake);   /* FUN_0044b219 */
}
