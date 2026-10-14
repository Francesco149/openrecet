/*
 * dungeon_flow.h — the scalars of the in-game stage tick FUN_00442cef and the
 * stage-entry init FUN_00436f97 that a dungeon floor runs on, plus the small
 * dungeon-only callees of the tick.
 *
 * The floor's life, as FUN_00442cef drives it:
 *   - FUN_00436f97 (a dungeon stage, DAT_0438b4c4 set at the guild Depart):
 *     the floor title FUN_00406551(180, 1) and the intro countdown b4b4 = 200;
 *   - b4b4 counts down one per tick; at 1 the dungeon's first-visit dialogue
 *     FUN_0044ba2c(2, 10, 0); at 0 the "Equip items in bag?" prompt (bea0 = 1);
 *   - the player plays (FUN_0048b3f6 → FUN_0048b850) until a portal (object
 *     kinds 6 / 8, the be9c prompt), the floor end (be98 / be94) or a fall
 *     (FUN_0044bb1a);
 *   - b8d0 counts the playing ticks (the start voices at 10 and 100), b1e8 the
 *     floor time.
 */
#ifndef OPENRECET_DUNGEON_FLOW_H
#define OPENRECET_DUNGEON_FLOW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int32_t g_floor_intro;        /* DAT_0438b4b4: the floor-entry countdown (200) */
extern int32_t g_floor_frames;       /* DAT_0438b8d0: playing ticks (b4b4 == 0) */
extern int32_t g_floor_time;         /* DAT_0438b1e8: ticks on the floor (be98 == 0) */
extern int32_t g_floor_timer[225];   /* DAT_0438c85c: [0] the hurry-up countdown (rooms × 900..3600);
                                      * the stage init fills all 225 with 2400 (7200 in dungeon 0) first */
extern int32_t g_dat_0438b4d4;       /* the floor within a boss run: -1 = a regular run, 0.. */
extern int32_t g_dat_0438b4d8;       /* dungeon 5's start floor */
extern int32_t g_floor_end_timer;    /* DAT_0438be94: the floor-end sequence, 0..120 */
extern int32_t g_floor_end_kind;     /* DAT_0438be98: 1 = the party fell, 2 = return portal, 3 = the last floor's */
extern int32_t g_portal_prompt;      /* DAT_0438be9c: 1.. the portal Yes/No, -1 = declined, 119..121 = leaving */
extern int32_t g_portal_return;      /* DAT_0438bea8: the prompt is the return portal (kind 6) */
extern int32_t g_portal_cooldown;    /* DAT_0438c14c: ticks before the next portal prompt */
extern int32_t g_equip_prompt;       /* DAT_0438bea0: the "Equip items in bag?" prompt, 1..118 */
extern int32_t g_ctrl_lock;          /* DAT_0438b8b0: -1 = the player has control, else the pad mask fed to the actor */
extern int32_t g_fall_timer;         /* DAT_0438b770: FUN_0044bb1a's respawn countdown (60) */
extern int32_t g_input_hold;         /* DAT_0438cd74: pad edges held back while bed8 > 0 */
extern int32_t g_event_tick;         /* DAT_0438b8a4: the per-dungeon event counter (FUN_004427f1's arms) */
extern int32_t g_event_stage;        /* DAT_0438b8a0: the per-dungeon event stage (1 during the stage init) */
extern int32_t g_ghost_timer;        /* DAT_0438cbe0: ticks since the first hurry-up spawn */
extern int32_t g_ghost_count;        /* DAT_0438cbe4: hurry-up spawns on this floor */
extern int32_t g_dat_0438beb0;
extern int32_t g_dat_0438beb4;       /* > 0: FUN_0044b38c owns the tick */
extern int32_t g_dat_0438bebc;       /* > 0: FUN_0044b635 owns the tick */
extern int32_t g_dat_0438becc;       /* > 0: the tick runs every third frame */
extern int32_t g_dat_0438b8d4;       /* the HP-check voice countdown (FUN_0048b3f6) */
extern int32_t g_dat_0438b8d8;       /* re-arm guard of b8d4 (FUN_00485c74 sets 120) */
extern int32_t g_cut_follow;         /* DAT_0438b788: the partner follows a fixed point (b78c / b794) */
extern float   g_cut_follow_x, g_cut_follow_y, g_cut_follow_z;   /* DAT_0438b78c / 790 / 794 */
extern float   g_floor_spawn_yaw;    /* DAT_0438b4ac: the player's facing after the stage init */
extern int32_t g_dat_0438b930;
extern int32_t g_dat_0438b950;
extern int32_t g_dat_0438b954;
extern int32_t g_dat_0438b8b4;       /* FUN_0048b850's first-tick latch */
extern int32_t g_dat_0438b8b8;
extern int32_t g_dat_0438b550;       /* 1: FUN_0048b850 spawns the queued b554 effects */
extern int32_t g_dat_0438b554[20];   /* -1 = empty */
extern int32_t g_dat_0064e818;       /* set 1 by every full FUN_00442cef tick */
extern int32_t g_dat_06a46fa4;       /* 1 on a bed8 hit-pause tick (the camera holds) */
extern int32_t g_dat_044e28fc[32][7];/* [i][0] counts down to 0 each tick */

/* FUN_00406551 / the floor title DAT_00648270/74/78 (FUN_00406584 counts it up
 * to `len`, the render draws it). */
extern int32_t g_title_banner;       /* DAT_00648270: 0 = off, else 1..len */
extern int32_t g_title_banner_len;   /* DAT_00648274 */
extern int32_t g_title_banner_kind;  /* DAT_00648278: 1 = the floor title */
void dungeon_title_banner_open(int32_t len, int32_t kind);   /* FUN_00406551 */
void dungeon_title_banner_tick(void);                        /* FUN_00406584's step */

/* The floor event (snews.txt): FUN_004364bc picks one by weight for the
 * floor at the player's first controllable tick; FUN_0043647f asks for one. */
extern int32_t g_floor_event_n;           /* DAT_0438b938 */
extern int32_t g_floor_event_id[5];       /* DAT_0438b93c */
extern int32_t g_floor_event_len;         /* DAT_0438b934: the ticker text's total length */
extern int32_t g_floor_event_ofs[5];      /* DAT_0438b958 */
extern char    g_floor_event_text[5][256];/* DAT_0438b96c */
void floor_event_pick(void);              /* FUN_004364bc */
int  floor_event_active(int32_t id);      /* FUN_0043647f */

/* The current stage is a dungeon floor: (unsigned) DAT_068dd3fc[b4dc] > 4. */
int  dungeon_flow_is_dungeon(void);

/* FUN_00436f97's resets of the scalars above (its head) — the rest of the
 * stage init is scene1_postload_stage_entry_init. */
void dungeon_flow_stage_reset(void);

/* FUN_0044bb1a: the fall check — below the stage's floor (+0x1b1c) the party
 * loses 10 % max HP and respawns at the floor origin after 60 ticks; at 0 HP
 * the floor ends (be98 = 1). */
void dungeon_fall_tick(void);

/* FUN_004427f1's dungeon arms (the house arm is FUN_0044bd0d,
 * scene1_tutorial_dispatch_tick); FUN_0044c9ba is dungeon 0's. */
void dungeon_event_tick(void);

/* FUN_0048b3f6: the dungeon player tick — the start voices (b8d0 10 / 100),
 * the low-HP voice, then FUN_0048b850 and FUN_00485861. */
void dungeon_player_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_DUNGEON_FLOW_H */
