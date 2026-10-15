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
#include "party.h"
#include "rng.h"                 /* FUN_00471084 / FUN_00471089 */
#include "save_work.h"
#include "scene1_combat_sm.h"    /* DAT_0438b4c8, DAT_0438bed8, DAT_0438b904/908, DAT_06a46f94 */
#include "scene1_records.h"
#include "scene1_records_b_tick.h"    /* DAT_06a46f98, the state-machine hook */
#include "scene1_records_c_spawn.h"   /* FUN_0044b0f3 */
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

#define AI(a, off)  (*(int32_t *)(void *)((a)->b + (off)))
#define AF(a, off)  (*(float *)(void *)((a)->b + (off)))
#define RI(s, off)  (*(int32_t *)(void *)((uint8_t *)(s) + (off)))
#define RF(s, off)  (*(float *)(void *)((uint8_t *)(s) + (off)))

static const float k0 = 0.0f, k0p01 = 0.009999999776482582f, k0p1 = 0.10000000149011612f;
static const float k0p12 = 0.11999999731779099f, k1 = 1.0f, k2 = 2.0f, k3 = 3.0f, k4 = 4.0f;
static const float k0p2 = 0.20000000298023224f, k0p3 = 0.30000001192092896f, k0p5 = 0.5f;
static const float k0p7 = 0.699999988079071f, k0p8 = 0.800000011920929f;
static const float k0p85 = 0.8500000238418579f, k1p2 = 1.2000000476837158f, k1p5 = 1.5f;
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
        /* PORT-DEBT(stub, FUN_0043865e): the player arm 0x438940..0x439f28
         * (an enemy record hitting the player: guard, damage, knock-back) */
        return 0;
    }
    return scene1_combat_sm_objects_scan(slot);     /* 0x439f28 */
}

void record_sm_install(void)
{
    scene1_records_b_set_state_machine_hook(record_sm_tick);
}
