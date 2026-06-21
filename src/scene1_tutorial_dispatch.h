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

#endif /* OPENRECET_SCENE1_TUTORIAL_DISPATCH_H */
