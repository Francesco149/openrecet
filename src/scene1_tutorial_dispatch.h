/*
 * scene1_tutorial_dispatch.h — FUN_0044bd0d, the house story / tutorial
 * scheduler (0x44bd0d, 2723 B, all_ida 49399), whole: survival's opener, the
 * day-8 visitor's scene (FUN_00470a46), the day-9 f488 morning (iv0_8 at b924
 * 630), iv9_1, the iv7_1 second-week morning, the b928 / b924 < 190 morning
 * hold, iv1_1 / iv1_2 / iv12_1, then LABEL_44: iv1_4, iv0_6, iv1_17 / iv1_18,
 * iv2_15 / iv2_14, iv0_17, iv12_2 / iv12_3, iv4_4 / iv4_5, iv1_5 / iv1_6, iv1_7
 * and the iv1_8 → iv2_1 → iv2_2 → iv2_3 (day advance) → iv2_5 → iv2_6 chain.
 * Each arm fires once, while no dialogue runs (DAT_0438b1c8 == 0).  It draws no
 * LCG.  The "OHA:%4d" sprintf is a debug text line (no state).
 * Arm-by-arm checked against all_ida 2026-11-19 (flag offsets included).
 *
 * PORT-DEBT(render-coupled, FUN_00452809): the arms that call FUN_00452809 arm
 * the blackout DAT_0438bf74 (a full-screen quad, render only; the port derives
 * it in scene1_intro_dialogue_blackout_active for the prologue only).
 */
#ifndef OPENRECET_SCENE1_TUTORIAL_DISPATCH_H
#define OPENRECET_SCENE1_TUTORIAL_DISPATCH_H

/* Per-frame tutorial-event check.  Call from the INGAME default-running arm
 * (scene1_ingame_default_arm_tick), AFTER the player controller so a placement's
 * condition flags are seen the same frame — matching retail's FUN_0048670f →
 * FUN_004427f1 → FUN_0044bd0d order in FUN_00442cef (all.c:40849). */
void scene1_tutorial_dispatch_tick(void);

/* The iv2_5→iv2_6 "Recette looks up at Tear" idle BEAT (retail DAT_0438b928==1 /
 * DAT_0438b924 timer).  iv2_5's dispatch arms it (b928=1, b924=0); the master tick
 * increments b924 every free-roam frame (all.c:86801); FUN_0044bd0d gates the whole
 * scenario cascade behind `b928==1 && b924 < 0xbe(190)` (all.c:45489 → the
 * `if(!bVar1) return` @45507), so iv2_6 (the DAY2 load) can't fire until the 190-frame
 * beat elapses.  1 while b928 == 1 && b924 < 200 (retail's hold) — the conversation
 * pose driver ORs this into
 * its pose gate so the actors stay posed (panim 6 / canim 4) across the beat instead
 * of releasing to idle at iv2_5's dialogue end.  See
 * docs/findings/cutscene-replay-anchor-drift.md (Residual B). */
int  scene1_tutorial_dispatch_iv2_beat_active(void);

/* Arm the beat (b928 = 1, b924 = 0): iv2_5's dispatch and the day-end summary
 * init FUN_0048da71 (the next morning's beat — the "Day N" card, Recette facing
 * Tear for 200 master ticks, the day-9+ morning block at b924 0). */
/* The same hold as seen by code that runs BEFORE this frame's master-tick bump
 * (the conversation-pose driver at the default-arm top): b924 + 1 < 200. */
int  scene1_tutorial_dispatch_beat_holds_this_tick(void);

void scene1_tutorial_dispatch_arm_beat(void);

/* The master-tick side of DAT_0438b924 (FUN_0048670f): the day-9+ morning block
 * in the prologue (before the bg-NPC pump), the ++b924 at LABEL_104 (after the
 * counter-menu slide, before the cc08 dispatch), and the re-layout writes
 * (FUN_00436f97 → 0, FUN_0048526d → 300). */
void scene1_beat_master_prologue(void);
void scene1_beat_master_voices(void);   /* the b924 1/50/110/150 greetings (before the count) */
void scene1_beat_master_count(void);
void scene1_beat_set_counter(int v);
/* DAT_0438b928: 1 = the morning beat (iv2_5 / FUN_0048da71), 2 = the world-map
 * return (FUN_0049e163's home arm), 0 = a title load (FUN_0049a59e) or a dungeon
 * entry (FUN_0045c051).  It persists across house loads (FUN_00436f97 only
 * zeroes b924). */
void scene1_beat_set_flag(int v);
int  scene1_beat_flag(void);
int  scene1_beat_counter(void);

/* The beat counter DAT_0438b924 (0..190).  Drives the "Day N" day-transition CARD
 * (FUN_0040a765 all.c:7500-7559): black backdrop + centred "Day N" text (alpha
 * b924*8) for b924<0x7a, then a white exit-fade for b924>0x5a — the same b924 the
 * b924<0xbe gate uses, so the card runs across the first 140f of the beat. */
int  scene1_tutorial_dispatch_iv2_beat_ctr(void);

/* Clear the transient beat state (a fresh prologue / scenario re-entry). */
void scene1_tutorial_dispatch_reset(void);
/* House load (pose_house_standing): b924 = 0 + the day-2 re-place disarmed;
 * b928 is kept (only a title load / dungeon entry clears it). */
void scene1_tutorial_dispatch_house_entry(void);

#endif /* OPENRECET_SCENE1_TUTORIAL_DISPATCH_H */
