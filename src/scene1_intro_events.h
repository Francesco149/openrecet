/*
 * scene1_intro_events.h — STUB: post-new-game intro-event sequencer.
 *
 * ── What the engine does (not yet ported) ───────────────────────────────
 * Retail's new-game → playable-HOUSE path is not a single load. After the
 * NEW GAME commit the engine runs two short scripted intro events (the
 * shopkeeper-arrival dialogue + the first-day setup), EACH bracketed by its
 * own asset-load. So the retail anchor stream is:
 *
 *     NEW_GAME → LOADING_START → LOADING_END → HOUSE_FREEROAM   (event 1)
 *              → LOADING_START → LOADING_END → HOUSE_FREEROAM   (event 2)
 *              → controllable free-roam
 *
 * i.e. HOUSE_FREEROAM fires TWICE before the player can move. The TAS
 * segtrace traces (traces/house_walk.jsonl, tests/scenarios/house-movement)
 * encode this directly: `wait HOUSE_FREEROAM` twice, the second resolving on
 * the second firing (see src/input_segtrace.h).
 *
 * ── What the port does today, and why this stub exists ──────────────────
 * The port reaches HOUSE through ONE load (scene_post_fade_init →
 * worker_load_spawn), so it fires HOUSE_FREEROAM exactly ONCE — and any
 * trace with a second `wait HOUSE_FREEROAM` stalls forever waiting on a
 * firing that never comes. The intro-dialogue subsystem that would produce
 * the second event is unported.
 *
 * This module is a minimal STUB that reproduces the *anchor shape* (a second
 * LOADING_START/END → HOUSE_FREEROAM) without rendering any dialogue: a tiny
 * frame-counted state machine that, once the first load has cleared, raises
 * the asset-load gate (worker_load_begin) for a few frames then drops it
 * (worker_load_end), driving a second HOUSE_FREEROAM edge through the
 * existing anchor_trace path. When the real dialogue subsystem lands it
 * replaces this stub and the *same* traces keep working — only the visual
 * fills in (the user's stated TAS principle: stub the dialogue so one trace
 * drives both targets).
 *
 * Pure C, no Win32 — host-testable. It only touches the worker-load gate
 * (src/worker_load.h) + reads nowloading (src/nowloading.h), so a test can
 * model one sim frame's gate logic and assert HOUSE_FREEROAM fires twice
 * (tests/test_anchor_trace.c).
 */
#ifndef OPENRECET_SCENE1_INTRO_EVENTS_H
#define OPENRECET_SCENE1_INTRO_EVENTS_H

/* Arm the sequencer. Called from scene_post_fade_init right after the
 * scene flips to INGAME (src/scene.c), i.e. once per new-game commit.
 * Idempotent within a transition — re-arming restarts the sequence. */
void scene1_intro_events_arm(void);

/* Advance one sim frame. Call once per sim tick BEFORE the worker-busy
 * check in sim_step_a, so that on the frame this stub raises the gate the
 * same tick's `if (worker_load_busy())` sees it and holds the overlay. A
 * no-op unless armed and still running. */
void scene1_intro_events_tick(void);

/* Nonzero while the stub still has work to do (armed, not yet finished). */
int  scene1_intro_events_pending(void);

/* Reset to the dormant state. For tests + scene re-entry. */
void scene1_intro_events_reset(void);

#endif /* OPENRECET_SCENE1_INTRO_EVENTS_H */
