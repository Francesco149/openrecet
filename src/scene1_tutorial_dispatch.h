/*
 * scene1_tutorial_dispatch.h — focused port of the FUN_0044bd0d story-event
 * scheduler's two shop-display tutorial branches (iv1_5 / iv1_6).
 *
 * FUN_0044bd0d (all.c:45406-45818, 2723 B) is the master per-frame tutorial /
 * scenario scheduler.  It consumes NO shared LCG (verified) and its activation
 * helper FUN_00452d07 only sets the dialogue gate + spawns the load thread, so a
 * FOCUSED port of just the iv1_5/iv1_6 branches is RNG-neutral.  Each branch, when
 * its placement condition flag is set and its per-slot done-flag is clear and no
 * dialogue is active (DAT_0438b1c8 == 0), activates the corresponding script:
 *
 *   iv1_5 (scene 1, sub 5): item placed in the window counters (row 0)
 *                           → DAT_0450f3fb set by the placement confirm (D1)
 *   iv1_6 (scene 1, sub 6): every display stand filled
 *                           → DAT_0450f3fd set by the placement confirm (D1)
 *   iv1_7 (scene 1, sub 7): the post-tutorial wrap-up ("And that is, essentially,
 *                           how it goes…") → DAT_0450f400 set by the sell-tutorial
 *                           cs leave/dissolve (FUN_00462403 @0x462403, mirrored in
 *                           customer_service.c).  P2 of the customer-service arc.
 *
 * iv1_5/iv1_6 are if/else-if (iv1_5 has priority); iv1_7 is an independent check
 * after them (retail likewise).  The no-dialogue gate serialises all three, so
 * each fires only after the previous has finished (retail: ord 770 → 1483 → the
 * post-haggle wrap-up).
 *
 * PORT-DEBT(focused, FUN_0044bd0d): the outer DAT_0450f454 "all early tutorials
 * done" gate, the DAT_0450f455 / DAT_0450fb88 alternate gate, and the rest of the
 * scenario chain past iv1_7 (iv1_8 "sit at the counter" = DAT_0450f402, the scene
 * 2/4/12 dialogues) are not ported — only the iv1_5/iv1_6/iv1_7 branches.  The
 * per-slot done-flags below make each fire exactly once regardless of the missing
 * outer gate.
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
 * beat elapses.  1 while the beat holds — the conversation pose driver ORs this into
 * its pose gate so the actors stay posed (panim 6 / canim 4) across the beat instead
 * of releasing to idle at iv2_5's dialogue end.  See
 * docs/findings/cutscene-replay-anchor-drift.md (Residual B). */
int  scene1_tutorial_dispatch_iv2_beat_active(void);

/* The beat counter DAT_0438b924 (0..190).  Drives the "Day N" day-transition CARD
 * (FUN_0040a765 all.c:7500-7559): black backdrop + centred "Day N" text (alpha
 * b924*8) for b924<0x7a, then a white exit-fade for b924>0x5a — the same b924 the
 * b924<0xbe gate uses, so the card runs across the first 140f of the beat. */
int  scene1_tutorial_dispatch_iv2_beat_ctr(void);

/* Consume the one-shot DAY-2 actor re-place (retail's FUN_0048526d scene-entry
 * re-seat the port's dialogue-load iv2 model skips).  Armed when iv2_5 arms the
 * beat, consumed here on the first free-roam frame.  Call from the INGAME
 * default-running arm TOP (scene1_ingame_default_arm_tick), before the
 * conversation-pose tick, so the re-seated positions are in place before the
 * pose derives the face-each-other octants and before render. */
void scene1_tutorial_dispatch_consume_day2_replace(void);

/* Clear the transient beat state (a fresh prologue / scenario re-entry). */
void scene1_tutorial_dispatch_reset(void);

#endif /* OPENRECET_SCENE1_TUTORIAL_DISPATCH_H */
