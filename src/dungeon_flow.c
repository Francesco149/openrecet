/*
 * dungeon_flow.c — see dungeon_flow.h.
 */
#include "dungeon_flow.h"

#include <string.h>

#include "actor_table.h"         /* the actor table (FUN_0044d0a4's count) */
#include "audio.h"               /* audio_play_se_file (FUN_0049933c) */
#include "record_hit.h"          /* DAT_0438b900 / b90c..b914 */
#include "call_trace.h"
#include "chara_equip.h"         /* DAT_0438b7d8 */
#include "floor_layout.h"        /* DAT_0438b1ec (the floor origin) */
#include "map_collision.h"       /* FUN_00433674 */
#include "dungeon_party.h"       /* FUN_0048b850 (the dungeon path) */
#include "dungeon_objects.h"     /* DAT_0695ef78, DAT_06a47054 */
#include "dungeon_results.h"     /* DAT_0438b718.. */
#include "fade.h"                /* FUN_004526f5 / 0045281c / 004528b3 */
#include "scene1_intro_dialogue.h" /* FUN_0044ba2c */
#include "party.h"
#include "tables_snews.h"        /* DAT_073b2108 / DAT_073d8ee0 */
#include <stdio.h>
#include "rng.h"                 /* j__rand (0x471084) */
#include "save_work.h"
#include "scene1_actor_say.h"    /* FUN_00485861 */
#include "scene1_camera.h"       /* DAT_0438cc68 */
#include "scene1_combat_sm.h"    /* DAT_0438b4c8 */
#include "scene1_maplight.h"     /* scene1_current_stage_record (DAT_068dd2f0) */
#include "scene1_particles_tick.h" /* g_scene1_actor_pos (DAT_056da1d8) */
#include "scene1_player_ctrl.h"  /* db048, the actor records */
#include "stage_gate.h"          /* DAT_0438b4cc, FUN_0043195d */
#include "stage_palette.h"
#include "stage_post_load.h"     /* DAT_0438b4d0, HP DAT_056db0bc */
#include "scene1_spawn.h"
#include "sim.h"
#include "x87.h"

int32_t g_floor_intro;
int32_t g_floor_frames;
int32_t g_floor_time;
int32_t g_floor_timer[225];
int32_t g_dat_0438b4d4;
int32_t g_dat_0438b4d8;
int32_t g_floor_end_timer;
int32_t g_floor_end_kind;
int32_t g_portal_prompt;
int32_t g_portal_return;
int32_t g_portal_cooldown;
int32_t g_equip_prompt;
int32_t g_ctrl_lock;
int32_t g_fall_timer;
int32_t g_input_hold;
int32_t g_event_tick;
int32_t g_event_stage;
int32_t g_ghost_timer;
int32_t g_ghost_count;
int32_t g_dat_0438beb0;
int32_t g_dat_0438beb4;
int32_t g_dat_0438beb8;
int32_t g_dat_09643128;
int32_t g_dat_0438bebc;
int32_t g_dat_0438becc;
int32_t g_dat_0438b8d4;
int32_t g_dat_0438b8d8;
int32_t g_cut_follow;
int32_t g_cam_blend;
float   g_cam_blend_x, g_cam_blend_y, g_cam_blend_z;
float   g_cut_follow_x, g_cut_follow_y, g_cut_follow_z;
int32_t g_floor_event_n;
int32_t g_floor_event_id[5];
int32_t g_floor_event_len;
int32_t g_floor_event_ofs[5];
char    g_floor_event_text[5][256];
int32_t g_dat_0438b930;
int32_t g_dat_0438b950;
int32_t g_dat_0438b954;
int32_t g_dat_0438b8b4;
int32_t g_dat_0438b8b8;
int32_t g_dat_0438b550;
int32_t g_dat_0438b554[20];
int32_t g_dat_0064e818;
int32_t g_dat_06a46fa4;
int32_t g_dat_06a46fa0;            /* the cinematic camera counter (FUN_004426a7) */
float   g_dat_0438bec4 = 1.0f;     /* eased to 0.5 by the cinematic (no port reader) */
float   g_floor_spawn_yaw;

int32_t g_title_banner;
int32_t g_title_banner_len;
int32_t g_title_banner_kind;

void dungeon_title_banner_open(int32_t len, int32_t kind)
{
    g_title_banner = 1;
    g_title_banner_len = len;
    g_title_banner_kind = kind;
}

void dungeon_title_banner_tick(void)
{
    if (g_title_banner > 0 && ++g_title_banner > g_title_banner_len)
        g_title_banner = 0;
}

int dungeon_flow_is_dungeon(void)
{
    const stage_record_t *sr = scene1_current_stage_record();
    return sr != NULL && (uint32_t)sr->dungeon_id > 4u;
}

void dungeon_flow_stage_reset(void)
{
    /* FUN_00436f97's head, in its order (the scalars owned here) */
    g_floor_event_n = 0;
    g_portal_cooldown = 0;
    g_input_hold = 0;
    g_floor_frames = 0;
    g_dat_0438b8d4 = 0;
    g_floor_intro = 0;
    g_dat_0438b954 = 0;
    g_dat_0438b950 = 0;
    g_dat_0438b930 = 0;
    g_floor_end_timer = 0;
    g_floor_end_kind = 0;
    g_portal_prompt = 0;
    g_dat_0438beb0 = 0;
    g_dat_0438beb4 = 0;
    g_dat_0438bebc = 0;
    g_dat_0438becc = 0;
    g_dat_0438bec4 = 1.0f;
    g_fall_timer = 0;
    g_orb_pick_n = 0;
    g_scene1_combat_dat_0438b904 = 0;
    g_scene1_combat_dat_0438b908 = 0;
    g_kill_combo = 0;
    g_kill_combo_type = -1;
    g_kill_combo_timer = 0;
    g_results_kept[1] = g_results_kept[2] = g_results_kept[3] = -1;   /* DAT_0438b71c..724 */
    g_results_row = 1;
    g_results_picking = 0;
    g_cam_blend = 0;
    g_cut_follow = 0;
    g_ctrl_lock = -1;
    g_dat_0438b8b4 = 0;
    g_dat_0438b8b8 = 0;
    g_event_stage = 1;
    g_event_tick = 0;
    g_dat_0438b550 = 0;
    memset(g_dat_0438b554, 0xff, sizeof g_dat_0438b554);
    /* b1e8 restarts at a run's first floor: b4d4 == 0, or a regular run
     * (b4d4 < 0) on a floor ≡ 0 mod 5 (dungeon 5: mod 10, below floor 30) */
    int restart = g_dat_0438b4d4 == 0;
    if (g_dat_0438b4d4 < 0) {
        const int32_t floor = stage_gate_get_next();
        if (g_scene1_combat_stage_id == 5) {
            if (floor < 30) restart = floor % 10 == 0;
        } else {
            restart = floor % 5 == 0;
        }
    }
    if (restart) g_floor_time = 0;
}

/* FUN_0044bb1a */
void dungeon_fall_tick(void)
{
    if (g_floor_end_kind == 0) {
        int in_water = 0;
        const uint8_t *st = (const uint8_t *)g_stage_palette;
        int32_t fall_y = 0;
        if (st != NULL) memcpy(&fall_y, st + 0x1b1c, 4);
        if (fall_y == -1000) in_water = g_party.hit[0].flag == 1;
        const float lim = spill32((long double)(fall_y - 1));
        if (!((long double)g_scene1_actor_pos[0][1] >= lim) || in_water) {
            if (g_fall_timer == 0) {
                const uint8_t *rec = save_work_bank_at(save_work_active_slot());
                const uint8_t *cr = rec ? rec + 0x2ceb0 + 0x6c * chara_equip_get_current_chara() : NULL;
                int16_t a = 0, b = 0;
                if (cr) { memcpy(&a, cr + 0x3c, 2); memcpy(&b, cr + 0x3e, 2); }
                const float max_hp = spill32((long double)((int32_t)a + (int32_t)b));
                int32_t dmg = x87_ftol((long double)max_hp * 0.10000000149011612f);
                if (stage_post_load_get_mode_b4d0() < 0 && g_scene1_combat_stage_id == 5)
                    dmg = x87_ftol((long double)max_hp * 0.20000000298023224f);
                if (g_floor_end_timer > 0 || g_portal_prompt > 0) dmg = 0;
                if (PW(0x56db0f4u).i > 0) dmg = 0;
                /* PORT-DEBT(stub, FUN_00485c74): the party damage (HP bar +
                 * the hurt voice FUN_00485979) — HP drops by `dmg` here. */
                stage_post_load_set_dat_056db0bc(spill32((long double)stage_post_load_get_dat_056db0bc() - dmg));
                if (stage_post_load_get_dat_056db0bc() <= 0.0f)
                    stage_post_load_set_dat_056db0bc(0.0f);
                g_fall_timer = 60;
            }
        }
    }
    if (g_fall_timer > 0) {
        --g_fall_timer;
        player_ctrl_set_db048(1);
        PW(0x56daeb0u).i = 30;
        party_anim_set(g_party.rec[0], 2);   /* FUN_00482a51(&DAT_056daae8, 2) */
        if (g_fall_timer == 0) {
            if (stage_post_load_get_dat_056db0bc() > 0.0f) {
                scene1_camera_snap_next();  /* DAT_0438cc68 = 1 */
                player_ctrl_set_db048(0);
                g_scene1_actor_pos[0][0] = g_scene1_stage_player_default_pos[0];
                PW(0x56daeb0u).i = 0;
                g_scene1_actor_pos[0][1] = g_scene1_stage_player_default_pos[1];
                g_scene1_actor_pos[0][2] = g_scene1_stage_player_default_pos[2];
                player_ctrl_set_facing_angle(g_floor_spawn_yaw);
            } else {
                PW(0x56daeb0u).i = 30;
                player_ctrl_set_db048(1);
                stage_post_load_set_dat_056db0bc(0.0f);
                if (g_floor_end_kind == 0) g_floor_end_kind = 1;
                party_anim_set(g_party.rec[0], 2);
            }
        }
    }
}

/* FUN_0044b2e6: raise the special object `role` (DAT_06a47054) if still spent:
 * 120 ticks of FUN_0044b38c (DAT_0438beb4), state 0, sunk 5 below its home */
void dobj_raise_special(int role)
{
    static const float k5 = 5.0f;
    dobj_t *o = &g_dobj[g_dobj_special[role]];
    if (o->i[DOBJ_STATE] != 3) return;
    g_dat_0438beb4 = 120;
    o->i[DOBJ_STATE] = 0;
    g_dat_0438beb8 = 0;
    g_dobj_special_up = role != 0;
    o->f[DOBJ_POS + 1] = spill32((long double)o->f[DOBJ_HOME + 1] - k5);
}

/* FUN_0044b38c: the special objects rising (while DAT_0438beb4 counts down,
 * DAT_0438beb8 counts up; the first tick starts the fade sub-counter): 5 below
 * their home, at 30 SE 364 + a puff (type 46), 30..50 shaking up into place
 * (two rand per object a tick), a kind-8 one scaling in instead; the camera
 * circles their mean (eye 20 out, sinking 0.01 a tick) */
void dobj_rise_tick(void)
{
    static const float k5 = 5.0f, k0p11 = 0.10999999940395355f, k0p1 = 0.10000000149011612f;
    static const float k2 = 2.0f, k2p5 = 2.5f, k0p5 = 0.5f, k20 = 20.0f, k3 = 3.0f, k10 = 10.0f;
    static const float k0p002 = 0.0020000000949949026f, k0p01 = 0.009999999776482582f;
    static const float k_pi = 3.1415927410125732f;
    int32_t t = g_dat_0438beb8;
    if (t == 0) {
        sim_set_counter_990(1);              /* FUN_004532b1 */
        t = g_dat_0438beb8;
    }
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    if (g_dobj_special_up != -1) {
        for (int k = 0; k != g_dobj_special_up + 1; k++) {
            dobj_t *o = &g_dobj[g_dobj_special[k]];
            g_scene1_camera_stage_class = 0;
            if (t == 0) o->f[DOBJ_POS + 1] = spill32((long double)o->f[DOBJ_HOME + 1] - k5);
            if (t == 30) {
                audio_play_se_by_id(364);
                scene1_spawn(0, o->f[DOBJ_POS + 0], o->f[DOBJ_POS + 1], o->f[DOBJ_POS + 2], 46, k0p11, 1);
                t = g_dat_0438beb8;
            }
            if (t >= 30 && t <= 50) {
                const float u = spill32((long double)(t - 30));
                o->f[DOBJ_POS + 1] = spill32((long double)o->f[DOBJ_HOME + 1] - ((long double)k2 - (long double)u * k0p1) * k2p5);
                o->f[DOBJ_POS + 0] = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + o->f[DOBJ_HOME + 0]);
                o->f[DOBJ_POS + 2] = spill32(((long double)rng_next_unit() - k0p5) * k0p5 + o->f[DOBJ_HOME + 2]);
                t = g_dat_0438beb8;
            }
            if (t == 50) {
                o->f[DOBJ_POS + 0] = o->f[DOBJ_HOME + 0];
                o->f[DOBJ_POS + 1] = o->f[DOBJ_HOME + 1];
                o->f[DOBJ_POS + 2] = o->f[DOBJ_HOME + 2];
            }
            if (k != 0) {
                cx = spill32(((long double)o->f[DOBJ_HOME + 0] - cx) * k0p5 + cx);
                cy = spill32(((long double)o->f[DOBJ_HOME + 1] - cy) * k0p5 + cy);
                cz = spill32(((long double)o->f[DOBJ_HOME + 2] - cz) * k0p5 + cz);
            } else {
                cx = o->f[DOBJ_HOME + 0];
                cy = o->f[DOBJ_HOME + 1];
                cz = o->f[DOBJ_HOME + 2];
            }
            if (o->i[DOBJ_KIND] == 8) {
                if (t < 30) {
                    o->f[DOBJ_SCALE + 0] = 0.0f;
                } else if (t <= 50) {
                    const float u = spill32((long double)(t - 30));
                    o->f[DOBJ_SCALE + 0] = spill32((long double)u * o->f[DOBJ_SCALE + 1] / k20);
                }
                o->f[DOBJ_POS + 0] = o->f[DOBJ_HOME + 0];
                o->f[DOBJ_POS + 1] = o->f[DOBJ_HOME + 1];
                o->f[DOBJ_POS + 2] = o->f[DOBJ_HOME + 2];
            }
        }
    }
    if (t > 0) {
        const float a = spill32((long double)spill32((long double)g_dat_0438beb8) * k0p002 + k_pi);
        const double ad = a;
        const float look[3] = { cx, spill32((long double)cy + k3), cz };
        const float fb = spill32((long double)g_dat_0438beb8);
        const float eye[3] = {
            spill32((long double)spill32(x87_crt_sin(ad)) * k20 + cx),
            spill32(((long double)cy + k10) - (long double)fb * k0p01),
            spill32((long double)spill32(x87_crt_cos(ad)) * ((long double)k20 - (long double)fb * k0p01) + cz),
        };
        scene1_camera_set_targets(eye, look);
        g_scene1_camera_stage_class = 1;
        t = g_dat_0438beb8;
    }
    g_dat_0438beb8 = t + 1;
    if (g_dat_0438beb4 == 0) {
        g_scene1_camera_stage_class = 0;
        scene1_camera_snap_next();           /* DAT_0438cc68 = 1 */
    }
}

/* FUN_0044c9ba: dungeon 0's events — floor 4 (the tutorial boss) before the
 * story reaches 2: a pending iv2_11 (work +0x2bc84) or raising the exit portal
 * (+0x2bc80); until b8a4 471 (+0x2bc81 clear) the intro: the controls locked,
 * the party facing 0 and scaling in from b8a4 350, the camera circling in to
 * the player, iv2_9 at 470.  Every floor counts b8a4. */
static void dungeon0_event_tick(void)
{
    static const float k0p1 = 0.10000000149011612f, k2 = 2.0f, k1 = 1.0f;
    static const float k40 = 40.0f, k1_14 = 0.0714285746216774f, k15 = 15.0f, k5 = 5.0f;
    static const float k_pi2 = 1.5707963705062866f, k350 = 350.0f;
    uint8_t *rec = save_work_bank_at(save_work_active_slot());
    int32_t v = 0;
    if (rec) memcpy(&v, rec + 187748, 4);
    if (stage_gate_get_next() == 4 && v != 2 && rec != NULL) {
        if (g_dat_0438beb4 == 0) {
            if (rec[0x2bc84] != 0) {
                rec[0x2bc84] = 0;
                rec[0x2bc80] = 0;
                scene1_intro_dialogue_start_single(2, 11);   /* FUN_0044ba2c(2, 11, 0) */
            } else if (rec[0x2bc80] != 0) {
                rec[0x2bc84] = 1;
                dobj_raise_special(0);                       /* FUN_0044b2e6(0) */
            }
        }
        g_ctrl_lock = -1;
        g_scene1_camera_stage_class = 0;                     /* DAT_0438b4e8 */
        if (rec[0x2bc81] == 0) {
            if (g_event_tick == 470)
                scene1_intro_dialogue_start_single(2, 9);    /* FUN_0044ba2c(2, 9, 0) */
            const int32_t t = g_event_tick;
            if (t < 471) {
                g_ctrl_lock = 0;
                g_scene1_camera_stage_class = 1;
                for (int i = 0; i < 3; i++) {
                    g_party.rec[i][6] = 0;
                    g_party.scale_xz[i] = 0.0f;
                    g_party.scale_y[i] = 1.0f;
                    if (t >= 350) {
                        const float s = spill32((long double)spill32((long double)(t - 350)) * k0p1);
                        g_party.scale_xz[i] = s;
                        g_party.scale_y[i] = spill32((long double)k2 - s);
                        if (g_party.scale_xz[i] > k1) g_party.scale_xz[i] = 1.0f;
                        if (g_party.scale_y[i] < k1) g_party.scale_y[i] = 1.0f;
                    }
                }
                const float ft = spill32((long double)t);
                float h = spill32((long double)k40 - (long double)ft * k1_14);
                if (h < k15) h = k15;
                float a = spill32((long double)ft * k_pi2 / k350 - k_pi2);
                if (a > 0.0f) a = 0.0f;
                const double ad = a;
                const float lx = g_party.pos[0][0], lz = g_party.pos[0][2];
                const float eye[3] = {
                    spill32((long double)spill32(x87_crt_sin(ad)) * k15 + lx), h,
                    spill32((long double)lz - (long double)spill32(x87_crt_cos(ad)) * k15),
                };
                const float look[3] = { lx, k5, lz };
                scene1_camera_set_targets(eye, look);        /* DAT_0438cc38.. / cc50.. */
            }
        }
    }
    ++g_event_tick;
}

/* FUN_0044cbe4: the hired adventurer's (DAT_0438b7d8) line as a boss shows. */
static void boss_intro_voice(void)
{
    const char *v = NULL;
    switch (chara_equip_get_current_chara()) {
    case 0: v = "bin/se/02li/action/02li_09.bin"; break;
    case 1: v = (rng_next15() & 1u) ? "bin/se/03sy/action/03sy_13b.bin"
                                     : "bin/se/03sy/action/03sy_13a.bin"; break;
    case 2: v = (rng_next15() & 1u) ? "bin/se/04ca/start/04ca_13e.bin"
                                     : "bin/se/04ca/start/04ca_13a.bin"; break;
    case 3: v = (rng_next15() & 1u) ? "bin/se/05ti/start/05ti_14.bin"
                                     : "bin/se/05ti/start/05ti_13a.bin"; break;
    case 4: v = "bin/se/06el/start/06el_15.bin"; break;
    case 6: v = "bin/se/08gr/start/08gr_12a.bin"; break;
    case 7: v = "bin/se/09ar/start/09ar_19a.bin"; break;
    default: break;
    }
    if (v != NULL)
        audio_play_se_file(v);               /* FUN_0049933c */
}

/* FUN_0044d0a4: a checkpoint floor's boss (event stage 0).  With a boss here
 * (FUN_004319d6): the title at 30, the fixed arena camera (yaw 0, look
 * (-40, 3.5, -40) from (-40, 12, -65)) for 240 ticks with the controls locked,
 * the adventurer's line at 240, then control back with the partner held
 * (b788) and the camera class 2; else just the title at 30.  From 60 on, once
 * no actor is in use: `raise` → both special objects (the portals), b788 /
 * 09643128 cleared, event stage 1. */
static void boss_floor_event(int raise)
{
    if (g_event_stage != 0) return;
    if (stage_gate_query()) {
        if (g_event_tick == 30) {
            dungeon_title_banner_open(180, 0);   /* FUN_00406551 */
            g_dat_09643128 = 1;
        }
        if ((uint32_t)g_event_tick < 240u) {
            static const float eye[3] = { -40.0f, 12.0f, -65.0f };
            static const float look[3] = { -40.0f, 3.5f, -40.0f };
            g_scene1_camera_stage_class = 1;     /* DAT_0438b4e8 */
            g_scene1_camera_yaw = 0.0f;          /* DAT_073de39c */
            scene1_camera_set_targets(eye, look);
        }
        g_ctrl_lock = 0;
        if (g_event_tick == 240)
            boss_intro_voice();                  /* FUN_0044cbe4 */
        if (g_event_tick >= 240) {
            g_ctrl_lock = -1;
            g_cut_follow = 1;
            g_scene1_camera_stage_class = 2;
        }
    } else if (g_event_tick == 30) {
        dungeon_title_banner_open(180, 0);
    }
    if (g_event_tick > 60) {
        int n = 0;
        for (int i = 0; i < ACTOR_COUNT; i++)
            if (*(const int32_t *)(const void *)(g_actor[i].b + ACT_INUSE) != 0)
                n++;
        if (n == 0) {
            if (raise) {
                dobj_raise_special(0);           /* FUN_0044b2e6 */
                dobj_raise_special(1);
            }
            g_cut_follow = 0;
            g_dat_09643128 = 0;
            g_event_tick = 0;
            g_event_stage = 1;
        }
    }
}

/* FUN_0045c025: a boss run's floor count by its mode (DAT_0438b4d0). */
static int32_t boss_run_floors(void)
{
    switch (stage_post_load_get_mode_b4d0()) {
    case 1: return 3;
    case 2: return 5;
    case 3: return 8;
    case 4: return 13;
    default: return 0;
    }
}

/* FUN_0044cc80: a checkpoint floor's arena.  With a boss here (FUN_004319d6):
 * the partner's fixed point b78c steps -22, -23, … (x) at z -41 until the
 * floor ray from (-40, 0.3, -40) is blocked short of it (10 tries); the camera
 * blend on (-40, 0, -40), class 2; the party lined up from the origin 5 back,
 * 1 apart, facing 0.  `portals` 1: the next-floor portal (kind 8, (-35, 0, -40),
 * scale 0 / 0.5 / 0.5) and the return portal (6, (-45, 0, -40), 0.35), both
 * spent until the boss falls; else one at (-40, 0, -40) — the return portal on
 * a boss run's last floor.  Without a boss: the pair beside the first variant-5
 * room (x 3.5 − col·40, the return 7 left), else the next-floor portal in the
 * first room and the return portal in the second. */
static void boss_floor_setup(int portals)
{
    static const float km22 = -22.0f, km41 = -41.0f, km40 = -40.0f, k0p3 = 0.30000001192092896f;
    static const float km1 = -1.0f, k1 = 1.0f, k5 = 5.0f, k3p5 = 3.5f, k7 = 7.0f, k40 = 40.0f;
    static const float km35 = -35.0f, km45 = -45.0f, k0p5 = 0.5f, k0p35 = 0.3499999940395355f;
    if (stage_gate_query()) {
        float frac = 0.0f;
        for (int i = 0; i != 10; i++) {
            const float fi = spill32((float)i);
            g_cut_follow_x = spill32((long double)km22 - fi);
            g_cut_follow_y = 0.0f;
            g_cut_follow_z = km41;
            const float dx = spill32((long double)g_cut_follow_x - km40);
            mapcol_hit_t h;
            if (mapcol_ray(km40, k0p3, km40, dx, 0.0f, km1, &frac, &h) == 0) break;
            if (frac >= k1) break;
        }
        g_cam_blend = 1;
        g_scene1_camera_stage_class = 2;     /* DAT_0438b4e8 */
        g_cam_blend_x = km40;
        g_cam_blend_y = 0.0f;
        g_cam_blend_z = km40;
        PW(0x56db05cu).f = 0.0f;
        for (int i = 0; i < 3; i++) {
            const float fi = spill32((float)i);
            g_party.pos[i][0] = spill32((long double)fi + g_scene1_stage_player_default_pos[0]);
            g_party.pos[i][1] = g_scene1_stage_player_default_pos[1];
            g_party.pos[i][2] = spill32(((long double)g_scene1_stage_player_default_pos[2] - k5) - fi);
            party_anim_set(g_party.rec[i], 0);   /* FUN_00482a51 */
            g_party.rec[i][6] = 0;
        }
    }
    if (portals == 0) return;
    const float sc8[3] = { 0.0f, k0p5, k0p5 };
    const float sc6[3] = { k0p35, k0p35, k0p35 };
    if (stage_gate_query()) {
        if (portals == 1) {
            float p[3] = { km35, 0.0f, km40 };
            dobj_spawn_special(0, 8, p, sc8, 0.0f);   /* FUN_0044b257 */
            p[0] = km45;
            dobj_spawn_special(1, 6, p, sc6, 0.0f);
        } else {
            const float p[3] = { km40, 0.0f, km40 };
            if (stage_post_load_get_mode_b4d0() >= 0 && g_dat_0438b4d4 == boss_run_floors() - 1)
                dobj_spawn_special(0, 6, p, sc6, 0.0f);
            else
                dobj_spawn_special(0, 8, p, sc8, 0.0f);
        }
        return;
    }
    for (int32_t row = 0; row < g_floor_h; row++) {
        for (int32_t col = 0; col < g_floor_w; col++) {
            const floor_room_t *rm = &g_floor_rooms[row * FLOOR_GRID_STRIDE + col];
            if (rm->type == -1 || rm->var < 5) continue;
            float p[3] = { spill32((long double)k3p5 - (long double)spill32((float)col) * k40), 0.0f,
                           spill32((long double)spill32((float)-row) * k40) };
            dobj_spawn_special(0, 8, p, sc8, 0.0f);
            p[0] = spill32((long double)p[0] - k7);
            dobj_spawn_special(1, 6, p, sc6, 0.0f);
            return;
        }
    }
    int found = 0;
    for (int32_t row = 0; row < g_floor_h; row++) {
        for (int32_t col = 0; col < g_floor_w; col++) {
            if (g_floor_rooms[row * FLOOR_GRID_STRIDE + col].type == -1) continue;
            const float p[3] = { spill32((long double)spill32((float)-col) * k40), 0.0f,
                                 spill32((long double)spill32((float)-row) * k40) };
            if (found) {
                dobj_spawn_special(1, 6, p, sc6, 0.0f);
                return;
            }
            dobj_spawn_special(0, 8, p, sc8, 0.0f);
            found = 1;
        }
    }
}

int32_t g_charme_portal;          /* DAT_06a47098: the floor-14 portal object */
actor_t *g_dungeon_boss;          /* DAT_06a47094: the scripted boss (floor 14, dungeon 2) */
int32_t g_dat_0438b8a8;           /* DAT_0438b8a8: Tielle's phase (FUN_00423b58 reads it) */

static actor_t *charme_spawn(float x, float y, float z)
{
    const uint8_t *w = save_work_bank_at(save_work_active_slot());
    const actor_stage_t st = { g_scene1_combat_stage_id, stage_gate_get_next(),
                               stage_post_load_get_mode_b4d0(), g_ghost_count,
                               w ? w[0x2bc57] : 0 };
    actor_t *a = actor_spawn_enemy(&st, x, y, z, 25);    /* FUN_0041f903 */
    if (a != NULL) {
        act_seti(a, ACT_INUSE, 1);
        act_seti(a, 0xa54, 8);
    }
    return a;
}

/* the work record's +0x2dd64 (2 skips the story beats) */
static int32_t work_mode(const uint8_t *rec)
{
    int32_t v = 0;
    if (rec) memcpy(&v, rec + 0x2dd64, 4);
    return v;
}

/* FUN_0044d1e4's floor-14 arm: the town portal at (-35, 0, -30) and the
 * floor portal at (-40, 0, -30) (DAT_06a47098) — raised at once (dobj_spawn)
 * once Charme was beaten (+0x2bc8d), else the spent pair (FUN_0044b257) the
 * timeline raises.  Beaten → event stage 6.  The first visit (+0x2bc8c) lines
 * the party up 1 apart (stage 0: Charme comes at 60); a later one 1.3 apart,
 * 5 back, with Charme waiting at (-40, 10, -32) (stage 2). */
static void charme_floor_setup(void)
{
    static const float km35 = -35.0f, km30 = -30.0f, km40 = -40.0f, k0p35 = 0.3499999940395355f;
    static const float k1p3 = 1.2999999523162842f, k5 = 5.0f, k10 = 10.0f, km32 = -32.0f;
    uint8_t *rec = save_work_bank_at(save_work_active_slot());
    const int32_t mode = work_mode(rec);
    const int beaten = rec != NULL && rec[0x2bc8d] != 0 && mode != 2;
    float p[3] = { km35, 0.0f, km30 };
    const float sc[3] = { k0p35, k0p35, k0p35 };
    if (beaten) dobj_spawn(p, sc, 0.0f, 6);
    else dobj_spawn_special(0, 6, p, sc, 0.0f);
    p[0] = km40;
    p[2] = km30;
    g_charme_portal = dobj_spawn(p, sc, 0.0f, 4);
    if (!beaten) dobj_spawn_special(1, 4, p, sc, 0.0f);
    if (beaten) {
        g_event_stage = 6;
        return;
    }
    if (rec != NULL && rec[0x2bc8c] == 0 && mode != 2) {
        rec[0x2bc8c] = 1;
        for (int i = 0; i < 3; i++) {
            const float fi = spill32((long double)i);
            g_party.pos[i][0] = spill32((long double)fi + g_scene1_stage_player_default_pos[0]);
            g_party.pos[i][1] = g_scene1_stage_player_default_pos[1];
            g_party.pos[i][2] = spill32((long double)g_scene1_stage_player_default_pos[2] - fi);
            party_anim_set(g_party.rec[i], 0);   /* FUN_00482a51 */
            g_party.rec[i][6] = 0;
        }
        PW(0x56db05cu).f = 0.0f;
        return;
    }
    for (int i = 0; i < 3; i++) {
        const float fi = spill32((long double)spill32((long double)i) * k1p3);
        g_party.pos[i][0] = spill32((long double)fi + g_scene1_stage_player_default_pos[0]);
        g_party.pos[i][1] = g_scene1_stage_player_default_pos[1];
        g_party.pos[i][2] = spill32(((long double)g_scene1_stage_player_default_pos[2] - k5) - fi);
        party_anim_set(g_party.rec[i], 0);
        g_party.rec[i][6] = 0;
    }
    PW(0x56db05cu).f = 0.0f;
    g_event_stage = 2;
    g_dungeon_boss = charme_spawn(km40, k10, km32);
}

/* FUN_0044d47d's floor-14 timeline (DAT_0438b8a0 / b8a4; the controls locked
 * (b8b0 0) in stages 0..6, free in 7):
 *   0 at 60: iv3_1, Charme at (-40, 5, -20) (type 25, state 8) → 1;
 *   1 the event camera (class 1) swings in from the floor's end over 40 ticks
 *     (look (-40, ≤4, ≤-22), yaw → -π/4, 15 out) and back from 240; at 300
 *     iv3_2 → 2;
 *   2 at 1: the floor portal sinks a step (spent), the partner holds at
 *     (-27, 0, -41), the camera blends to (-40, 0, -40), the title banner,
 *     her voice; at 120 the adventurer's line → 3;
 *   3 the fight: no enemy left → 4 (mode 2: 6);
 *   4 at 60 the fade-out; once done the party back at the start facing -x,
 *     the fade-in → 5;
 *   5 at 30 iv3_3 → 6 at 31;
 *   6 at 30 iv3_8 the first time (+0x2bc8d); at 31 both portals rise → 7. */
static void charme_floor_event(void)
{
    static const float k4 = 4.0f, k40 = 40.0f, k12 = 12.0f, k34 = 34.0f, km22 = -22.0f;
    static const float km_pi4 = -0.7853981852531433f, k240 = 240.0f, k0p05 = 0.05000000074505806f;
    static const float k0p25 = 0.25f, km34 = -34.0f, k15 = 15.0f, km40 = -40.0f, k5 = 5.0f;
    static const float km20 = -20.0f, k0p5 = 0.5f, km10 = -10.0f, km27 = -27.0f, km41 = -41.0f;
    static const float k1p5 = 1.5f, km_pi2 = -1.5707963705062866f;
    uint8_t *rec = save_work_bank_at(save_work_active_slot());
    g_ctrl_lock = -1;
    switch (g_event_stage) {
    case 0:
        g_ctrl_lock = 0;
        if (g_event_tick == 60) {
            scene1_intro_dialogue_start_single(3, 1);    /* FUN_0044ba2c(3, 1, 0) */
            charme_spawn(km40, k5, km20);
            g_event_stage = 1;
            g_event_tick = 0;
        }
        break;
    case 1: {
        g_ctrl_lock = 0;
        if (g_event_tick >= 0) {
            g_scene1_camera_stage_class = 1;              /* DAT_0438b4e8 */
            float ft = spill32((long double)g_event_tick);
            float ly = spill32((long double)ft * k4 / k40);
            if (ly > k4) ly = k4;
            float lz = spill32((long double)ft * k12 / k40 - k34);
            if (lz > km22) lz = km22;
            float yaw = spill32((long double)ft * km_pi4 / k40);
            if (yaw < km_pi4) yaw = km_pi4;
            if (g_event_tick >= 240) {
                ft = spill32((long double)ft - k240);
                ly = spill32((long double)k4 - (long double)ft * k0p05);
                if (ly < 0.0f) ly = 0.0f;
                lz = spill32((long double)km22 - (long double)ft * k0p25);
                if (lz < km34) lz = km34;
            }
            const float look[3] = { km40, ly, lz };
            const double X = yaw;
            const float eye[3] = {
                spill32((long double)spill32(x87_crt_sin(X)) * k15 + look[0]),
                spill32((long double)look[1] + k15),
                spill32((long double)look[2] - (long double)spill32(x87_crt_cos(X)) * k15),
            };
            scene1_camera_set_targets(eye, look);         /* DAT_0438cc38.. / cc50.. */
        }
        if (g_event_tick == 300) {
            g_event_tick = 0;
            g_event_stage = 2;
            scene1_intro_dialogue_start_single(3, 2);
        }
        break;
    }
    case 2:
        g_scene1_camera_stage_class = 0;
        g_ctrl_lock = 0;
        if (g_event_tick == 1) {
            dobj_t *o = &g_dobj[g_charme_portal];
            if (o->i[DOBJ_KIND] != -1) {
                o->i[DOBJ_STATE] = 3;
                const float y = spill32((long double)o->f[DOBJ_POS + 1] - k0p5);
                o->f[DOBJ_POS + 1] = y;
                if (y < km10) o->i[DOBJ_KIND] = -1;
            }
            g_cut_follow_x = km27;
            g_cut_follow = 1;
            g_cam_blend = 1;
            g_cut_follow_y = 0.0f;
            g_cut_follow_z = km41;
            g_cam_blend_x = km40;
            g_cam_blend_y = 0.0f;
            g_cam_blend_z = km40;
            dungeon_title_banner_open(180, 0);           /* FUN_00406551 */
            g_dat_09643128 = 1;
            audio_play_se_file("bin/se/03sy/action/03sy_13a.bin");   /* FUN_0049933c */
        }
        if (g_event_tick == 120) {
            boss_intro_voice();                          /* FUN_0044cbe4 */
            g_event_stage = 3;
            g_event_tick = 0;
        }
        break;
    case 3:
        if (g_event_tick > 0) {
            int n = 0;
            for (int i = 0; i < ACTOR_COUNT; i++) {
                const int32_t u = act_geti(&g_actor[i], ACT_INUSE);
                if (u != 0 && u != 2) n++;
            }
            if (n == 0) {
                g_event_stage = 4;
                g_event_tick = 0;
                g_dat_09643128 = 0;
                if (work_mode(rec) == 2) g_event_stage = 6;
            }
        }
        break;
    case 4:
        g_ctrl_lock = 0;
        if (g_event_tick == 60) fade_phase1_start(0, 120);   /* FUN_004526f5 */
        if (g_event_tick > 60 && fade_is_done()) {            /* FUN_004528b3 */
            g_cam_blend = 0;
            g_cut_follow = 0;
            for (int i = 0; i < 3; i++) {
                g_party.pos[i][0] = g_scene1_stage_player_default_pos[0];
                g_party.pos[i][1] = g_scene1_stage_player_default_pos[1];
                const float fi = spill32((long double)i);
                g_party.pos[i][2] = spill32((long double)g_scene1_stage_player_default_pos[2] - (long double)fi * k1p5);
                party_anim_set(g_party.rec[i], 0);
                g_party.rec[i][6] = 6;
            }
            PW(0x56db05cu).f = km_pi2;
            g_event_tick = 0;
            g_event_stage = 5;
            fade_phase_out_start(0, 30);                  /* FUN_0045281c */
        }
        break;
    case 5:
        g_ctrl_lock = 0;
        if (g_event_tick == 30) scene1_intro_dialogue_start_single(3, 3);
        if (g_event_tick == 31) {
            g_event_stage = 6;
            g_event_tick = 0;
        }
        break;
    case 6:
        g_ctrl_lock = 0;
        if (g_event_tick == 30) {
            if (rec == NULL || rec[0x2bc8d] != 0 || work_mode(rec) == 2) break;
            rec[0x2bc8d] = 1;
            scene1_intro_dialogue_start_single(3, 8);
        }
        if (g_event_tick == 31) {
            dobj_raise_special(0);                       /* FUN_0044b2e6 */
            dobj_raise_special(1);
            g_event_stage = 7;
            g_event_tick = 0;
        }
        break;
    case 7:
        g_ctrl_lock = -1;
        break;
    default:
        break;
    }
}

/* FUN_0044d1e4: dungeon 1's floor setup (FUN_00436f97's dungeon arm): floor 14
 * (Charme's), or a checkpoint floor's arena FUN_0044cc80(1); b8a4 = 0. */
void dungeon1_stage_setup(void)
{
    g_dat_09643128 = 0;
    g_scene1_camera_stage_class = 0;
    const int32_t floor = stage_gate_get_next();
    if (floor == 14) {
        charme_floor_setup();
    } else if (floor % 5 == 4) {
        boss_floor_setup(1);
    }
    g_event_tick = 0;
}

/* FUN_0044d47d: dungeon 1's events — floor 14 (Charme), the checkpoint floors
 * (b4cc % 5 == 4) through FUN_0044d0a4(1); every floor counts b8a4. */
static void dungeon1_event_tick(void)
{
    const int32_t floor = stage_gate_get_next();
    if (floor == 14) {
        charme_floor_event();
        ++g_event_tick;
        return;
    } else if (floor % 5 == 4) {
        boss_floor_event(1);
    }
    ++g_event_tick;
}

/* FUN_0044d9d3's floor-29 arm: the spent town portal at (-45, 0, 65), the party
 * in a column at x -40 from z -54 (1.3 apart) facing +z; until Tielle was beaten
 * (+0x2bd12 1, or story mode 2) event stage 0 with Tielle (type 27, state 8) at
 * (-40, 0, -40), else stage 10.  (Her timeline FUN_0044db3c is a stub.) */
static void tielle_floor_setup(void)
{
    static const float km45 = -45.0f, k65 = 65.0f, km40 = -40.0f, km54 = -54.0f;
    static const float k0p35 = 0.3499999940395355f, k1p3 = 1.2999999523162842f;
    const uint8_t *rec = save_work_bank_at(save_work_active_slot());
    const float p[3] = { km45, 0.0f, k65 };
    const float sc[3] = { k0p35, k0p35, k0p35 };
    dobj_spawn_special(0, 6, p, sc, 0.0f);              /* FUN_0044b257 */
    for (int i = 0; i < 3; i++) {
        g_party.pos[i][0] = km40;
        g_party.pos[i][1] = 0.0f;
        g_party.pos[i][2] = spill32((long double)km54 - (long double)spill32((long double)i) * k1p3);
        party_anim_set(g_party.rec[i], 0);              /* FUN_00482a51 */
        g_party.rec[i][6] = 0;
    }
    PW(0x56db05cu).f = 0.0f;
    if (rec == NULL || rec[0x2bd12] != 1 || work_mode(rec) == 2) {
        g_event_stage = 0;
        const actor_stage_t st = { g_scene1_combat_stage_id, stage_gate_get_next(),
                                   stage_post_load_get_mode_b4d0(), g_ghost_count,
                                   rec ? rec[0x2bc57] : 0 };
        g_dungeon_boss = actor_spawn_enemy(&st, km40, 0.0f, km40, 27);   /* FUN_0041f903 */
        if (g_dungeon_boss != NULL) {
            act_seti(g_dungeon_boss, ACT_INUSE, 1);
            act_seti(g_dungeon_boss, 0xa54, 8);
        }
        g_dat_0438b8a8 = 0;
    } else {
        g_event_stage = 10;
    }
}

/* FUN_0044d9d3: dungeon 2's floor setup: floor 29 (Tielle's), or a checkpoint
 * floor's arena FUN_0044cc80(1); b8a4 = 0.  Ported from the disasm; no
 * recording reaches dungeon 2 yet. */
void dungeon2_stage_setup(void)
{
    g_dat_09643128 = 0;
    g_scene1_camera_stage_class = 0;
    const int32_t floor = stage_gate_get_next();
    if (floor == 29) {
        tielle_floor_setup();
    } else if (floor % 5 == 4) {
        boss_floor_setup(1);
    }
    g_event_tick = 0;
}

/* FUN_004427f1's dungeon-2 arm: floor 29 → Tielle's timeline, a checkpoint
 * floor → FUN_0044d0a4(1); every floor counts b8a4. */
static void dungeon2_event_tick(void)
{
    const int32_t floor = stage_gate_get_next();
    if (floor == 29) {
        /* PORT-DEBT(stub, FUN_0044db3c): Tielle's floor-30 timeline (her
         * entrance, the fight, the scenes; arg = story mode 2) */
    } else if (floor % 5 == 4) {
        boss_floor_event(1);
    }
    ++g_event_tick;
}

/* FUN_004427f1: the house's story cascade, or the dungeon's events. */
void dungeon_event_tick(void)
{
    switch (g_scene1_combat_stage_id) {
    case 0:
        dungeon0_event_tick();          /* FUN_0044c9ba */
        break;
    case 1:
        dungeon1_event_tick();          /* FUN_0044d47d */
        break;
    case 2:
        dungeon2_event_tick();          /* inline in FUN_004427f1 */
        break;
    default:
        /* PORT-DEBT(stub, FUN_004427f1): dungeons 3..5's events — the
         * dungeon-3 arm inline in FUN_004427f1 (the b8a4 timelines on floors
         * 29 / 49), FUN_00450877, FUN_00451546. */
        break;
    }
}

/* FUN_0048b3f6 */
void dungeon_player_tick(void)
{
    static const char *const k_start_voice[8][4] = {
        { "bin/se/02li/action/02li_08a.bin", "bin/se/02li/action/02li_08b.bin" },
        { "bin/se/03sy/action/03sy_08a.bin", "bin/se/03sy/action/03sy_08b.bin" },
        { "bin/se/04ca/start/04ca_11a.bin", "bin/se/04ca/start/04ca_11d.bin",
          "bin/se/04ca/start/04ca_12a.bin", "bin/se/04ca/start/04ca_12d.bin" },
        { "bin/se/05ti/start/05ti_10.bin", "bin/se/05ti/start/05ti_11.bin",
          "bin/se/05ti/start/05ti_12.bin" },
        { "bin/se/06el/start/06el_12.bin", "bin/se/06el/start/06el_12a.bin",
          "bin/se/06el/start/06el_13a.bin", "bin/se/06el/start/06el_14.bin" },
        { "bin/se/07na/start/n_10.bin", "bin/se/07na/start/n_11.bin",
          "bin/se/07na/start/n_12.bin", "bin/se/07na/start/n_13.bin" },
        { "bin/se/08gr/start/08gr_10b.bin", "bin/se/08gr/start/08gr_16b.bin" },
        { "bin/se/09ar/start/09ar_17.bin", "bin/se/09ar/start/09ar_18.bin" },
    };
    CALL_TRACE_ENTER(0x48b3f6u);
    const uint8_t *rec = save_work_bank_at(save_work_active_slot());
    const int32_t chara = chara_equip_get_current_chara();
    if (g_event_stage == 0 && !stage_gate_floor_is_checkpoint() &&
        rec != NULL && rec[0x2bcf8] == 0 && rec[0x2bce2] == 0) {
        if (g_floor_frames == 10) {
            if (rng_next15() & 1) {
                audio_play_se_file("bin/se/01ti/action/01ti_btl01.bin");
            } else if (chara >= 0 && chara < 8) {
                uint32_t r = (uint32_t)rng_next15();
                int k;
                switch (chara) {
                case 2: case 4: case 5: k = (int)(r & 3); break;
                case 3:             k = (int)(r % 3u); break;
                default:            k = (int)(r & 1); break;
                }
                audio_play_se_file(k_start_voice[chara][k]);
            }
        }
        if (g_floor_frames == 100)
            audio_play_se_file((rng_next15() & 1) ? "bin/se/00re/action/00re_btl01a.bin"
                                                  : "bin/se/00re/action/00re_btl01b.bin");
    }
    if (g_dat_0438b8d4 > 0 && --g_dat_0438b8d4 == 0 && stage_post_load_get_dat_056db0bc() > 0.0f) {
        const uint8_t *cr = rec ? rec + 0x2ceb0 + 0x6c * chara : NULL;
        int16_t a = 0, b = 0;
        if (cr) { memcpy(&a, cr + 0x3c, 2); memcpy(&b, cr + 0x3e, 2); }
        const float max_hp = spill32((long double)((int32_t)a + (int32_t)b));
        if ((long double)max_hp * 0.2f < (long double)stage_post_load_get_dat_056db0bc())
            audio_play_se_file((rng_next15() & 1) ? "bin/se/00re/action/00re_btl02a.bin"
                                                  : "bin/se/00re/action/00re_btl02b.bin");
        else
            audio_play_se_file("bin/se/00re/action/00re_btl03.bin");
    }
    party_tick();           /* FUN_0048b850 */
    actor_say_tick();       /* FUN_00485861 */
}

void floor_event_pick(void)
{
    const uint8_t *rec = save_work_bank_at(save_work_active_slot());
    g_floor_event_n = 0;
    if (stage_gate_floor_is_checkpoint() || rec == NULL || rec[0x2bce2] != 0 || rec[0x2bcf8] != 0)
        return;
    const int32_t d = g_scene1_combat_stage_id, next = stage_gate_get_next() + 1;
    if (d < 0 || d >= SNEWS_DUNGEON_SLOT_COUNT) return;
    int sec = 0;
    for (int k = 0; k < SNEWS_SECTION_COUNT && g_snews.sections[d][k].floor_start != -1; k++) {
        if (next >= g_snews.sections[d][k].floor_start && next <= g_snews.sections[d][k].floor_end) {
            sec = k;
            break;
        }
    }
    const snews_section_t *s = &g_snews.sections[d][sec];
    int n = 0;
    uint32_t total = 0;
    for (int k = 0; k < SNEWS_ENTRY_COUNT; k++)
        if (s->entries[k].id != -1) { total += (uint32_t)s->entries[k].weight; n++; }
    if (n > 0) {
        int32_t r = (int32_t)((uint32_t)rng_next15() % total);
        int k = 0;
        for (; k < SNEWS_ENTRY_COUNT; k++) {
            if (s->entries[k].id != -1) {
                r -= s->entries[k].weight;
                if (r <= 0) break;
            }
        }
        int32_t id = 0;
        if (k < SNEWS_ENTRY_COUNT) id = s->entries[k].id;
        if (id >= 0 && id < SNEWS_NAME_COUNT && g_snews.names[id].active) {
            g_floor_event_id[g_floor_event_n] = id;
            snprintf(g_floor_event_text[g_floor_event_n], sizeof g_floor_event_text[0], "%s",
                     g_snews.names[id].name);
            ++g_floor_event_n;
        }
    }
    int32_t ofs = 0;
    for (int k = 0; k < g_floor_event_n; k++) {
        g_floor_event_ofs[k] = ofs;
        ofs += (int32_t)strlen(g_floor_event_text[k]) + 4;
    }
    g_floor_event_len = ofs;
    g_dat_0438b950 = 1;
}

int floor_event_active(int32_t id)
{
    const stage_record_t *sr = scene1_current_stage_record();
    if (sr == NULL || sr->maptype <= 0 || g_floor_event_n == 0) return 0;
    for (int k = 0; k < g_floor_event_n; k++)
        if (g_floor_event_id[k] == id) return 1;
    return 0;
}
