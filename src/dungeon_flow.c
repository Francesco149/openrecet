/*
 * dungeon_flow.c — see dungeon_flow.h.
 */
#include "dungeon_flow.h"

#include <string.h>

#include "audio.h"               /* audio_play_se_file (FUN_0049933c) */
#include "record_hit.h"          /* DAT_0438b900 / b90c..b914 */
#include "call_trace.h"
#include "chara_equip.h"         /* DAT_0438b7d8 */
#include "floor_layout.h"        /* DAT_0438b1ec (the floor origin) */
#include "dungeon_party.h"       /* FUN_0048b850 (the dungeon path) */
#include "dungeon_objects.h"     /* DAT_0695ef78, DAT_06a47054 */
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
    g_fall_timer = 0;
    g_orb_pick_n = 0;
    g_scene1_combat_dat_0438b904 = 0;
    g_scene1_combat_dat_0438b908 = 0;
    g_kill_combo = 0;
    g_kill_combo_type = -1;
    g_kill_combo_timer = 0;
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

/* FUN_004427f1: the house's story cascade, or the dungeon's events. */
void dungeon_event_tick(void)
{
    switch (g_scene1_combat_stage_id) {
    case 0:
        dungeon0_event_tick();          /* FUN_0044c9ba */
        break;
    default:
        /* PORT-DEBT(stub, FUN_0044d47d): dungeons 1..5's events — FUN_0044d47d,
         * the dungeon-2 arm inline in FUN_004427f1 (the b8a4 timeline on floors
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
    dungeon_party_tick();   /* FUN_0048b850 */
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
