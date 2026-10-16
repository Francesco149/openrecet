/*
 * scene1_player_ctrl.h — the house controller FUN_0048670f (the cc08 / cc04
 * arms, the door, the counter, the display stands) and the actor-state
 * accessors of the party block.  The player controller FUN_0048b850 it calls
 * is dungeon_party.c's party_tick (house and floor alike); FUN_0048619f, the
 * faced display cell, is scene1_shop_display.c.
 */
#ifndef SCENE1_PLAYER_CTRL_H
#define SCENE1_PLAYER_CTRL_H

#include <stdint.h>

/* Per-actor slots the shop-walker player draw walks (0=player, 1/2=party). */
#define PC_NUM_ACTORS        3
#define PC_ACTOR_REC_DWORDS  11   /* DAT_056daae8 ring stride (sprite-state) */

/* The two after-image banks of the party block (DAT_056dab6c trail,
 * DAT_056dacc0 burst; FUN_0048b850's tail fills them): 5 records of 17
 * dwords each — sprite-state [0..10], x/y/z [11..13], life [14]. */
#define PC_TRAIL_RECORDS     5
#define PC_TRAIL_REC_DWORDS  17   /* 0x44-byte record stride */
#define PC_BURST_RECORDS     5

/* ── Cchr.2h: player/companion actor-state model ─────────────────────────
 *
 * The engine globals the shop-walker player draw (FUN_004552d0 L357-454)
 * reads per actor i: char id (DAT_056da1cc[i]), XZ/Y scale (DAT_056dae18[i]
 * / DAT_056dae24[i]), and the 11-dword sprite-state record (DAT_056daae8 +
 * i*0xb).  FUN_0048b850 is their live writer; until it lands, the pose
 * function below seeds actor 0 from the runs/cchr2b retail leaf ground
 * truth.  Position is the separate g_scene1_player_pos (DAT_056da1d8).
 */

/* Seed actor 0 = the standing player (idle pose), empty slots 1/2.
 * `player_char` is the engine's DAT_056da1cc (0 = Recette on HOUSE entry). */
void player_ctrl_pose_house_standing(int player_char);

/* Read accessors for the draw side (i out of range → -1 / 0 / NULL). */
int            player_ctrl_actor_char(int i);
float          player_ctrl_actor_scale_xz(int i);
float          player_ctrl_actor_scale_y(int i);
const int32_t *player_ctrl_actor_record(int i);

/* Mutable record access for the companion controller (scene1_companion_ctrl,
 * engine FUN_0048a833) — it owns the actor-2 record's anim/facing fields the
 * same way scene1_player_ctrl_tick owns actor 0's.  Returns NULL if i is out of
 * range.  PC_ACTOR_REC_DWORDS dwords. */
int32_t       *player_ctrl_actor_record_mut(int i);

/* Trace-harness `{phasepin}` for the player (actor 0): zero the anim cycle
 * (FRAME/TIMER/COUNTER) so an IDLE-window port↔retail comparison normalizes the
 * load-dependent phase origin (the companion twin lives in scene1_companion_ctrl).
 * Comparison ONLY — the shipped game keeps the free-running cycle. */
void           player_ctrl_phasepin(void);

/* ── FUN_0048b850 tail render banks (Chip 2) ─────────────────────────────────
 * The two after-image banks the chr-sprite walker (FUN_00456f56) draws, owned
 * and written by the b850 tail (player_ctrl_b850_render_tail, engine all.c
 * L90242+): sweep 0 = the dash-trail bank (DAT_056dab6c, always read), sweep 1
 * = the burst bank (DAT_056dacc0, gated on the burst count below).  Each slot
 * is a PC_TRAIL_REC_DWORDS record (sprite[0..10], x/y/z at [0xb..0xd], life/age
 * at [0xe]).  Both banks are empty in HOUSE free-roam (no dash spawn / zero
 * burst counter); the walker iterates them and draws nothing.  Out-of-range
 * sweep/idx → NULL. */
const int32_t *player_ctrl_render_bank_slot(int sweep, int idx);
int            player_ctrl_burst_count(void);

/* Set the stored world facing db05c (conversation-pose ±π/2 write). */
void player_ctrl_set_facing_angle(float angle);

/* DAT_056db048 — the player pose-hold state (0 free; 0xc the display-stand
 * carry; 15 the day-close tired pose, FUN_0048526d). */
void player_ctrl_emote_reset(void);   /* DAT_056db000/004 = 0 */
int  player_ctrl_db048(void);
void player_ctrl_set_db048(int v);
/* FUN_0048b850 as FUN_0048670f's day-8 scene block calls it: the walk (pad
 * impulse, move, anim) without the display-cell highlight */
void player_ctrl_walk(void);

/* FUN_00436f97's player-controller resets (blocks 11-15): every actor's
 * facing octant 4 + flags 0 + scale 1.0, the walk velocity 0, the facing
 * angle db05c = π, db048/db04c/emote/burst counters 0. */
void player_ctrl_stage_entry_reset(void);
void player_ctrl_display_latch_reset(void);   /* DAT_0438cbe8 = 0 */
int  player_ctrl_display_latch(void);

int  player_ctrl_b850_ran(void);   /* FUN_0048b850 (party_tick) ran this frame */

/* ── FUN_0048670f cc08 dispatch state (Chip 4, engine-quirks §78) ────────────
 * The in-game interaction state the controller dispatches on each frame
 * (DAT_0438cc08): 1 = free-roam walk; other values are the unported event /
 * camera / counter / menu / dialogue arms. */
void player_ctrl_cc08_enter_freeroam(void); /* FUN_004850ec: set cc08 = 1 (HOUSE entry) */
int  player_ctrl_cc08(void);                /* read cc08 */
int  player_ctrl_cc04(void);                /* read cc04 (free-roam interaction sub-state) */
/* cc08==4: the master tick (FUN_00462403) runs FUN_0048a833 at 0x4624a1, after
 * its b1cc / ESC-skip gates and before the b534 dispatch. */
void player_ctrl_companion_tick_from_cs(void);
void player_ctrl_debug_set_cc08(int state); /* test hook: force cc08 (stands in for the
                                             * unported state-transition writers) */
int  player_ctrl_cc08_f406_pending(void);   /* pure: f406 first-customer entry armed (RE §21.18) */
int  player_ctrl_cc08_f406_entry(void);     /* the f406 autonomous first-customer cs entry
                                             * (all.c:87485): f406!=0 ⇒ cc08=4 + session init.
                                             * Exposed for host tests; called from the
                                             * free-roam arm. */

/* T1 — shop-door exit → the world map (mode 8).  `player_ctrl_at_shop_door` is the
 * pure door-zone predicate (v59, FUN_0048670f 0x488616-0x48872e); the arm
 * + stage-2 drive the dissolve fade (FUN_004526f5) and the mode-8 load
 * (FUN_0045281c / FUN_00452cde).  See scene1_player_ctrl.c. */
int  player_ctrl_at_shop_door(float x, float z, float facing, int scene_type,
                              int already_exited);
void player_ctrl_worldmap_exit_arm(void);
int  player_ctrl_worldmap_exit_stage2(void);
int  player_ctrl_worldmap_exit_armed(void);  /* test accessor */
void player_ctrl_worldmap_exit_reset(void);  /* test reset */

/* Free-roam interaction-affordance emote bubble (DAT_056db000 / DAT_056db004) —
 * the "GO!" door tooltip + talk/pick-up prompts.  `level` (0..10) is the slide-in
 * gauge the bubble's sin scale + visible gate read; `type` is the hpmp_base.tga
 * cell (7 = the shop door).  Driven by player_ctrl_cc08_proximity_detect (the
 * bVar17 door-zone ramp), consumed by the FUN_0040a765 draw (scene1_hud.c). */
int  player_ctrl_emote_level(void);          /* DAT_056db000 */
int  player_ctrl_emote_type(void);           /* DAT_056db004 */

/* Pure ramp step (host-testable): at an affordance → set `type`, ramp `level` up
 * to 10; off it → ramp `level` down to 0.  See scene1_player_ctrl.c. */
void player_ctrl_emote_ramp_step(int at_affordance, int affordance_type,
                                 int *level, int *type);

/*
 * ── W1: the per-frame player-controller tick (FUN_0048670f entry) ─────────
 *
 * Engine FUN_0048670f (0x48670f, 11.5 KB) is the HOUSE player driver — it
 * reads the input masks, moves the player, and sets the animation record,
 * calling FUN_0048b850 (the player controller) as a sub-step.
 *
 * Call order (engine FUN_00442cef / scene1_ingame_default_arm_tick,
 * all.c L40593-40598): the controller runs FIRST — before FUN_0043ae20
 * (records-B), gated on `DAT_0438be94 < 0x78` and a dispatcher that picks
 * FUN_0048670f when `0 <= DAT_068dd3fc[DAT_0438b4dc*0x6cf] <= 4` (the live
 * HOUSE branch) else the FUN_0048b3f6 variant.  Both gates are open in HOUSE;
 * faithfully reproducing them is deferred to a later sub-chip.
 *
 * The cc08 == 1 arm's walk is FUN_0048b850 (party_tick) + FUN_0048619f.
 */
void scene1_player_ctrl_tick(void);

/* DAT_0438be7c / be80: Tear in reach (the countdown) and the just-talked latch.
 * be80 = 1, be7c = 0 at FUN_00436f97 / FUN_004706ea / FUN_004852fb. */
void    player_ctrl_tear_reach_reset(void);
int32_t player_ctrl_tear_reach(void);

/* Engine facing octant from a world facing angle + the scene camera yaw.
 * oct = (int)((angle + cam_yaw + π/8) * 8/(2π) + 8) & 7  (b850 ftol formula;
 * the +8 keeps the value positive before the mask, mirroring the engine). */
int player_ctrl_facing_octant(float angle, float cam_yaw);

/* FUN_00486435: the house bounds the room mesh lacks — small rooms (shop tier
 * < 3) pz ≤ 9.5 and, outside customer service, px ≥ −1.5 while pz > 7.0;
 * large rooms pz ≤ 17 and px = 9.5 inside (5, 9.5) while pz > 11.5. */
void player_ctrl_house_room_clamp(float *px, float *pz);

#endif /* SCENE1_PLAYER_CTRL_H */
