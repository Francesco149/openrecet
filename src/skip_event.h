/*
 * skip_event.h — the ESC "skip this event?" yes/no prompt state machine.
 *
 * When ESC is pressed during an interruptible in-game event (the opening
 * prologue dialogue is the first case), retail pops a gold scroll banner —
 * "Do you want to skip this event?" with a Yes/No cursor — over a lightly
 * darkened scene. Yes tears the event down and drops to free-roam; No resumes.
 *
 * Engine correspondence (the prompt subsystem, by-address/453384.c +
 * 4536cb.c + 4532df.c + the FUN_00454191 render):
 *
 *   FUN_0045337b → FUN_00453384(0)   arm (from WndProc ESC, FUN_0047b2e7)
 *   DAT_06a499a0                      prompt-open flag        → g_open
 *   DAT_06a4999c                      open/render anim phase  → phase (draws >1)
 *   DAT_06a4997c                      skip-KIND (0 = dialogue/general skip)
 *   DAT_06a49998                      confirm counter (==3 → b1c0=9 confirm)
 *
 * IMPORTANT — what is faithful here and what is PORT-DEBT:
 *   The arm GATE and the open/teardown STRUCTURE are taken from the disasm
 *   (FUN_00453384 — for the prologue, kind 0, the gate reduces to "a dialogue
 *   line is up": the FUN_00434dd6 / DAT_0450f4xx stage-flag sub-gates are all
 *   clear in the prologue). The interactive INPUT choreography is NOT legible
 *   from static disasm: the auto-confirm counter (FUN_004532df) only climbs
 *   while ESC is *disabled*, and the cancel counter DAT_06a499c8 is never set
 *   positive anywhere in the decompiled corpus — i.e. the live Yes/No / confirm
 *   / cancel handling resolves through state the static dump doesn't express.
 *   docs/findings/esc-skip-event.md concludes this needs a live retail golden.
 *   So the Yes/No selection + confirm/cancel below is modelled to the
 *   user-confirmed OBSERVABLE behavior (ESC → prompt, cursor defaults to Yes,
 *   Left/Right toggles, A confirms, B cancels), per the user's directive to
 *   implement the observable result rather than over-fit the Frida
 *   choreography. The exact counter values + the real selection global are
 *   reconciled against the golden when Phase C (render) lands.
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

/* Master enable. Stays 0 until the Phase C prompt RENDER lands: an armed but
 * invisible prompt freezes the underlying event with no on-screen Yes/No, i.e.
 * a soft-lock. With this 0, skip_event_arm() is a no-op and the in-game ESC
 * keeps its current swallow behavior. The host tests flip it to 1 to exercise
 * the state machine. Phase C sets it once the banner renders. */
extern int g_skip_event_enabled;

/* Arm the prompt. `skippable` is the caller's skippable-context predicate —
 * for the prologue, scene1_intro_dialogue_active() (a dialogue line is up).
 * No-op when disabled (g_skip_event_enabled == 0), when !skippable, or when
 * already open (idempotent re-press — mirrors FUN_00453384's "s98 > 0 → no
 * re-arm"). Returns 1 if the prompt is open afterwards. */
int skip_event_arm(int skippable);

/* Is the prompt currently open? The caller freezes the underlying event
 * (the dialogue tick) while this returns 1. */
int skip_event_open(void);

/* Advance one frame with player-0's held button mask (g_input_state[0].buttons).
 * Returns PENDING while the prompt stays up; CONFIRMED or CANCELLED on the
 * frame it closes (the prompt auto-closes — open() reads 0 afterwards). No-op
 * (returns PENDING) when the prompt is closed. */
skip_result_t skip_event_tick(uint16_t held);

/* Force the prompt closed without a choice (scene teardown / reset). */
void skip_event_close(void);

/* ── render-state getters (Phase C, src/skip_prompt_draw.c, consumes these) ── */

/* Open/render anim phase (DAT_06a4999c). Climbs 1→0xc while open, 0 when
 * closed. The retail render (FUN_00454191) draws the banner only when > 1. */
int skip_event_phase(void);

/* Yes/No cursor: 0 = Yes (default), 1 = No. (Selection storage is PORT-DEBT —
 * see the header banner; the golden pins the real engine global.) */
int skip_event_selection(void);

#endif /* OPENRECET_SKIP_EVENT_H */
