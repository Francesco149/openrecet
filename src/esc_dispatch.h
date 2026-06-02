/*
 * esc_dispatch.h — context-sensitive ESC-key routing.
 *
 * Mirrors the ESC arm of the engine WndProc (FUN_0047b2e7 @ 0x47b2e7,
 * decompiled by-address/47b2e7.c:96-114):
 *
 *     if FUN_00452911() != 0:  return                  // (A) DAT_06a49954 — ESC disabled
 *     if DAT_0438b1c0 != 0:  FUN_0045337b(); return      // (B) in-game sub-mode → skip-event prompt
 *     if FUN_0049a585() == 0:  return                    // (C) overlay open → swallow
 *     PostMessage(WM_CLOSE)                               // (D) title, nothing open → quit confirm
 *
 * The port skeleton (src/main.c WM_KEYDOWN) used to PostMessage(WM_CLOSE)
 * unconditionally, so ESC popped the quit box in every context. This module
 * restores the per-context routing. The decision is pure C (no Win32) so the
 * host suite exercises it; main.c performs the PostMessage on the QUIT result.
 *
 * RE writeup: docs/findings/esc-skip-event.md.
 */
#ifndef OPENRECET_ESC_DISPATCH_H
#define OPENRECET_ESC_DISPATCH_H

/* What an ESC keypress should do, decided by esc_pressed(). */
typedef enum {
    ESC_RESULT_SWALLOW = 0,  /* consume; take no window-level action       */
    ESC_RESULT_QUIT    = 1,  /* post WM_CLOSE → "quit the game?" confirm    */
} esc_result_t;

/* Mirror of DAT_06a49954 — when nonzero, ESC is fully disabled (the engine
 * raises it across non-interruptible loads/transitions). PORT-DEBT: no
 * producer wired yet, so it stays 0; swallowing ESC during loads is handled
 * by the in-game branch (B) anyway. */
extern int g_esc_disabled;

/* Decide what an ESC keypress should do given the current scene state.
 * See the file banner for the engine correspondence. */
esc_result_t esc_pressed(void);

#endif /* OPENRECET_ESC_DISPATCH_H */
