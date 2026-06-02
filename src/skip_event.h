/*
 * skip_event.h — the ESC "Do you want to skip this event?" yes/no prompt.
 *
 * When ESC is pressed during an interruptible in-game event (the opening
 * prologue dialogue is the first case), retail pops the engine CHOICE BOX —
 * a gold scroll banner with "Do you want to skip this event?" and a Yes/No
 * hand cursor — over the scene. Yes tears the event down and drops to
 * free-roam; No / B resumes.
 *
 * This module is the thin event-specific glue: the FUN_0046c2cb arm gate
 * (open the choice box iff the dialogue's skip_prompt counter > 1) and the
 * FUN_0046c320 poll branch (Yes → CONFIRMED, No → CANCELLED). The prompt's
 * state machine + render live in the reusable choice box (src/choice_box.h,
 * FUN_00434def / FUN_00434ed2 / FUN_0043537e), which the engine shares across
 * save / shop / event confirmations. Golden:
 * runs/skip-golden/arm485/frame_00514.png.
 *
 * Pure C, no Win32 — the host suite drives it directly (test_skip_event.c).
 */
#ifndef OPENRECET_SKIP_EVENT_H
#define OPENRECET_SKIP_EVENT_H

#include <stdint.h>

/* The outcome of one skip_event_tick() while/at the close of the prompt. */
typedef enum {
    SKIP_EVENT_PENDING   = 0,  /* prompt still up, awaiting a choice          */
    SKIP_EVENT_CONFIRMED = 1,  /* Yes — caller runs the event teardown        */
    SKIP_EVENT_CANCELLED = 2,  /* No / cancel — caller resumes the event      */
} skip_result_t;

/* Master enable (default 1 — the choice box renders). Set 0 to force the arm
 * to a no-op (ESC keeps the Phase A swallow); the host tests toggle it. */
extern int g_skip_event_enabled;

/* Arm the prompt (FUN_0046c2cb). `skippable` is the caller's skip gate — for
 * the prologue, scene1_intro_dialogue_skippable() (a line is up AND has been
 * shown ≥2 frames). No-op when disabled, when !skippable, or when already open
 * (idempotent re-press, mirrors DAT_073a3dec==1). Returns 1 if open afterwards. */
int skip_event_arm(int skippable);

/* Is the prompt currently open? The caller freezes the underlying event
 * (the dialogue tick) and draws the choice box (choice_box_draw) while this
 * returns 1. Mirrors DAT_073a3dec. */
int skip_event_open(void);

/* Advance one frame with player-0's held button mask (g_input_state[0].buttons).
 * Returns PENDING while the prompt stays up; CONFIRMED or CANCELLED on the
 * frame it closes (the prompt auto-closes — open() reads 0 afterwards). No-op
 * (returns PENDING) when the prompt is closed. */
skip_result_t skip_event_tick(uint16_t held);

/* Force the prompt closed without a choice (scene teardown / reset). */
void skip_event_close(void);

#endif /* OPENRECET_SKIP_EVENT_H */
