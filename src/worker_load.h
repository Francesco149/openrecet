/*
 * worker_load.h — engine asset-load worker thread (FUN_00452cde +
 *                 LAB_0045293d at 0x452cde / 0x45293d).
 *
 * The engine kicks off a one-shot worker thread on every cross-scene
 * transition to load the destination scene's assets while the main
 * thread keeps pumping the message loop + rendering the "Now Loading…"
 * overlay. The thread reads the (just-written) `g_scene_state` and
 * jumps through a 17-entry dispatch table into the per-scene loader,
 * then exits.
 *
 * Engine source (verified against vendor/unpacked/recettear.unpacked.exe
 * via objdump @ 0x452cde / 0x45293d / 0x452917 / 0x452911):
 *
 *   FUN_00452cde  spawner:
 *     DAT_06a49954 = 1;             // busy
 *     DAT_06a49958 = 1;             // nowloading gate
 *     DAT_06a49950 = CreateThread(NULL,0,&LAB_0045293d,NULL,0,
 *                                 &DAT_06a496cc);
 *
 *   LAB_0045293d  thread routine:
 *     ax = DAT_0438b1c0;            // scene state
 *     if (ax > 16) goto cleanup;
 *     jump *(0x452a27 + ax*4);      // 17-entry table
 *     <each case calls 1-2 per-scene loaders, then falls into cleanup>
 *   cleanup:
 *     CloseHandle(DAT_06a49950);
 *     DAT_06a49950 = 0;
 *     DAT_06a49954 = 0;
 *     return 1;
 *
 *   FUN_00452917  close-helper:
 *     if (DAT_06a49950 != NULL) {
 *         CloseHandle(DAT_06a49950);
 *         DAT_06a49950 = NULL;
 *         DAT_06a4995c = 0;         // secondary worker busy (other family)
 *         DAT_06a49960 = 0;         // secondary nowloading gate
 *     }
 *
 *   FUN_00452911  busy-query:
 *     return DAT_06a49954;
 *
 * Jump table decoded from 0x452a27 (objdump as bytes; Ghidra
 * misinterprets it as code):
 *
 *   case  0 (TITLE)   → FUN_004733d5 + FUN_0049a3a3
 *                       (= scene_title_load_assets + scene_title bootstrap)
 *   case  1 (INGAME)  → FUN_00474a9a + FUN_00436f97          [UNPORTED]
 *   case  2           → FUN_0047355d                          [UNPORTED]
 *   case  3           → FUN_004736bd + FUN_0041edf1           [UNPORTED]
 *   case  4           → straight to cleanup (engine no-op)
 *   case  5           → FUN_0046c01e + FUN_0046bf38           [UNPORTED]
 *   case  6           → FUN_00473769                          [UNPORTED]
 *   case  7           → FUN_00473585                          [UNPORTED]
 *   case  8           → FUN_0049de20 + FUN_004735ad           [UNPORTED]
 *   case  9           → sub-dispatch on DAT_06a4997c (0/1/2)  [UNPORTED]
 *   case 10           → FUN_0047347d                          [UNPORTED]
 *   case 11           → FUN_0045bdc2 + FUN_00473874           [UNPORTED]
 *   case 12           → straight to cleanup (engine no-op)
 *   case 13           → FUN_00473972                          [UNPORTED]
 *   case 14           → FUN_00473991                          [UNPORTED]
 *   case 15           → FUN_004739fb                          [UNPORTED]
 *   case 16           → FUN_004739dc                          [UNPORTED]
 *
 * This port lands the dispatcher infrastructure (spawn / busy / close
 * / case-callback table). Per-case callbacks are registered by
 * higher-level modules via `worker_load_set_cb(N, fn)` so the worker
 * stays decoupled from scene-specific code. Cases with no registered
 * callback are no-ops — exactly what the engine does for the literal
 * cleanup-targets at cases 4 and 12, and what we want for the 14
 * other cases whose loader targets aren't ported yet.
 *
 * Alt primary worker (FUN_00452eed + LAB_00452a6b @ 0x452eed / 0x452a6b):
 *
 *   A sibling of FUN_00452cde — same primary gates (DAT_06a49954 +
 *   DAT_06a49958), different thread proc target (LAB_00452a6b instead
 *   of LAB_0045293d). The alt thread proc does NOT dispatch via the
 *   17-entry table; it runs a fixed body:
 *
 *     if (DAT_06a4996c == 0) {
 *         FUN_0047472c();  // pre-room-change A
 *         FUN_00474681();  // pre-room-change B
 *     }
 *     FUN_004746fc();      // room-load step 1
 *     FUN_00473c15();      // room-load step 2
 *     FUN_00436f97();      // shared with case-1 (INGAME) loader
 *     <primary cleanup>
 *
 *   The DAT_06a4996c gate is a "same-room" flag set at the sole
 *   caller (decompiled at FUN_00452f16 surroundings) and means "skip
 *   pre-prep, the destination room is the same as the source". The
 *   port exposes this as a single registered callback
 *   (`worker_load_set_alt_cb`) — the scene module that owns the body
 *   decides internally whether to short-circuit. Same shape, scene
 *   logic stays in scene-land.
 *
 *   Cleanup is structurally identical to LAB_0045293d (close handle,
 *   clear primary busy, return 1) — we share the thread-end helper.
 *
 * Secondary worker family (FUN_00452d07/d3e/d85/dc1/dfd/e39/e75/eb1
 * + LAB_00452aab/ae8/b13/b3e/b82/bc6/c0a/c4e/c96):
 *
 *   Eight spawn entry points (`worker_load_spawn_d07` etc.) — each
 *   raises the same `secondary` busy + nowloading gates (DAT_06a4995c
 *   + DAT_06a49960), latches the param into DAT_06a49980, optionally
 *   pre-writes its per-kind "pending=2" state byte (5 distinct bytes
 *   across the 8 spawners), and CreateThread's on one of 9 thread
 *   procs. The shared close/busy machinery is reused.
 *
 *   Nine thread proc bodies (per-LAB_*). Each:
 *     1. Runs the registered inner-body callback (currently NULL for
 *        all 9 slots — scene-specific work, will register as scene
 *        loaders port). Engine call targets per slot decoded via
 *        objdump @ 0x452aab..0x452cdd:
 *
 *          slot | LAB         | engine inner-body call(s)
 *          -----+-------------+--------------------------------------
 *           AAB | 0x452aab    | FUN_0046bf38()                (sc1 inventory/chrname/icon loaders)
 *           AE8 | 0x452ae8    | FUN_0047329b()                (buy phase: per-entry + chrname + shopmode)
 *           B13 | 0x452b13    | FUN_0047333b()                (buy phase alt, per DAT_0730b56c page)
 *           B3E | 0x452b3e    | FUN_0047474e(1)               (wall asset loader — WIRED, src/scene_walls.{c,h})
 *           B82 | 0x452b82    | FUN_004747dc(1)               (floor asset loader — WIRED, src/scene_floor.{c,h})
 *           BC6 | 0x452bc6    | FUN_0047486a(1)               (jutan/rug asset loader — WIRED, src/scene_jutan.{c,h})
 *           C0A | 0x452c0a    | FUN_004748f8(1)               (table asset loader)
 *           C4E | 0x452c4e    | (unnamed @ 0x435873)()        (FPU state init — Ghidra missed it; WIRED, src/scene_pause.{c,h})
 *               |             | + FUN_00473a3e()              (pause menu + adventurer status assets — WIRED, src/scene_pause.{c,h})
 *           C96 | 0x452c96    | FUN_0049de20()                (world-map state machine entry)
 *               |             | + FUN_004735ad()              (world map BMP loaders)
 *
 *        Note on the literal `1`: each of the four wall/floor/rug/table
 *        loaders is also called with literal `0` from the main-thread
 *        scene-1 init path (around line 73066 of decompiled/all.c).
 *        Param mode is always a compile-time constant at the call site:
 *        `0` = "load only the selector" (foreground), `1` = "load
 *        everything except the selector" (background, this worker).
 *
 *        All 12 targets are scene-1 (INGAME) specific — they'll wire
 *        up via worker_load_set_sec_body() when the respective scene
 *        loaders port. As of 2026-05-22 B3E (walls), B82 (floors),
 *        BC6 (jutan/rugs), and C4E (pause+status) are wired; the
 *        other 5 slots stay NULL until their scene loaders port.
 *     2. Falls into the shared secondary cleanup tail (CloseHandle,
 *        zero handle, zero 4995c, zero 49960).
 *     3. Writes its per-LAB_* "ready=1" state byte.
 *     4. Six of nine (LAB_00452b3e/b82/bc6/c0a/c4e/c96): if
 *        DAT_06a49980 == 1, call `fade_phase_out_start(0, 0x11)`. The
 *        engine pattern is `cmp [param], 1 ; jne SKIP_FADE ; <fade>`
 *        — the fade is the FALL-THROUGH branch, fires when param==1.
 *     5. LAB_00452aab only: also writes DAT_06a49984=1 and calls
 *        `FUN_00499579(0)` (= DAT_09643120 = 0). Engine XORs eax to
 *        zero and pushes that as the arg — this RESETS the audio LFO
 *        context, doesn't raise it. (FUN_0049966a's reader checks
 *        DAT_09643120 == 0 to clear DAT_005d1968.)
 *     6. Return 1.
 *
 *   FUN_00452d07 has one extra pre-spawn slot — the engine calls
 *   FUN_0046c01e on the main thread before CreateThread; we expose
 *   that as `worker_load_set_sec_d07_pre_spawn`.
 *
 *   FUN_00452e75 and FUN_00452eb1 are unreferenced in the engine's
 *   decompiled C (no callers found in `docs/decompiled/all.c`); their
 *   thread procs LAB_00452c4e and LAB_00452c96 are similarly dead
 *   storage. We port them for completeness — they're real engine
 *   entries with consistent shape.
 *
 * Per-kind state bytes (`g_worker_sec_state_*`):
 *
 *   Spawner writes 2 (pending), thread proc writes 1 (ready) — except
 *   `1c8`/`984`/`audio` which are write-only by LAB_00452aab.
 *
 *     +---------+--------------------------+-----------------------+
 *     | global  | engine addr              | written by            |
 *     +---------+--------------------------+-----------------------+
 *     | 1c8     | DAT_0438b1c8             | aab (=1)              |
 *     | 1cc     | DAT_0438b1cc             | d3e (=2) / ae8|b13(=1)|
 *     | 1d0     | DAT_0438b1d0             | e75 (=2) / c4e (=1)   |
 *     | 1d4     | DAT_0438b1d4             | d85|dc1|dfd|e39 (=2)  |
 *     |         |                          |  / b3e|b82|bc6|c0a(=1)|
 *     | 1d8     | DAT_0438b1d8             | eb1 (=2) / c96 (=1)   |
 *     | 984     | DAT_06a49984             | aab (=1)              |
 *     | audio   | DAT_09643120 via 0x499579| aab (=0, resets LFO)  |
 *     +---------+--------------------------+-----------------------+
 *
 * NOT yet ported:
 *   - Inner-body callbacks for 5 of the 9 secondary thread procs (the
 *     per-LAB_* scene work — register via `worker_load_set_sec_body`
 *     as scene loaders port). B3E/B82/BC6/C4E wired 2026-05-22;
 *     AAB/AE8/B13/C0A/C96 still NULL.
 *   - Pre-spawn for FUN_00452d07 (FUN_0046c01e — register via
 *     `worker_load_set_sec_d07_pre_spawn` when that ports).
 *
 * Wired (was a deferred item, now landed):
 *   - Per-tick "if worker done, drop the overlay gate" — lives at the
 *     top of `sim_step_a` in src/sim.c (engine FUN_004536cb L50363-
 *     50367). Note: engine inlines this at the top of FUN_004536cb,
 *     NOT FUN_004547ab as an earlier note in this header claimed.
 *
 * Race notes:
 *
 *   The engine has a latent race: CreateThread can return + the
 *   thread can start running before the spawner assigns the handle
 *   to DAT_06a49950, so the thread's `CloseHandle(DAT_06a49950)` may
 *   close stale state. In practice this never bites because real
 *   case-0..16 loaders take milliseconds. We match the engine. The
 *   secondary close-helper (worker_load_close) is the explicit
 *   "shut down any in-flight worker" hook callers use when they need
 *   determinism (e.g. before spawning a new worker for a different
 *   scene).
 */

#ifndef OPENRECET_WORKER_LOAD_H
#define OPENRECET_WORKER_LOAD_H

#include <stdint.h>

/* The engine's jump table has exactly 17 entries (cmp $0x10 + ja
 * cleanup). Values outside [0,16] are treated as cleanup-only. */
#define WORKER_LOAD_CASE_COUNT 17

/* Per-scene loader callback. Invoked from the worker thread (Win32
 * build) or synchronously when the test harness drives the
 * dispatcher. Must be self-contained — D3D work is OK on Win32 because
 * the engine does it, but the callback is responsible for any locking
 * it needs (the engine itself does none). */
typedef void (*worker_load_cb)(void);

/* Register the loader callback for `case_idx`. Pass NULL to clear.
 * Out-of-range indices are silently ignored. Safe to call multiple
 * times — last write wins. */
void worker_load_set_cb(int case_idx, worker_load_cb cb);

/* Look up the currently-registered callback for `case_idx`, or NULL
 * if none / index out of range. Inspection helper for tests. */
worker_load_cb worker_load_get_cb(int case_idx);

/* Port of FUN_00452911: returns 1 while a worker thread is still
 * executing, else 0. Read of the engine's DAT_06a49954. */
int  worker_load_busy(void);

/* Engine DAT_06a4995c — the secondary-worker busy flag, raised by the
 * six (+two-undocumented) DAT_06a49960 spawners and cleared by their
 * thread procs and by FUN_00452917 (close-helper). Those spawners
 * aren't ported yet, so this currently only ever reads 0; the
 * accessor exists so callers can be wired against the final shape
 * today. */
int  worker_load_busy_secondary(void);

/* Pure-C dispatcher core — invokes the registered callback for
 * `scene_state` if any. Does NOT touch the busy flag. Used by the
 * thread procedure on Win32 and by unit tests directly.
 *
 * Returns 1 if `scene_state` was in [0, WORKER_LOAD_CASE_COUNT),
 * else 0 (engine: branches via the `ja $0x452a09` early-out). The
 * return value reports the dispatch decision; whether a callback
 * actually ran (i.e. one was registered) is observable via the cb
 * itself. */
int  worker_load_dispatch_pure(int scene_state);

/* Pure-C side of FUN_00452cde — set busy=1 and raise the nowloading
 * gate. Win32 `worker_load_spawn` calls this before CreateThread; the
 * non-Win32 build of `worker_load_spawn` does only this, leaving
 * dispatch to a separate explicit call. */
void worker_load_begin(void);

/* Pure-C end-of-thread — clear busy=0. The Win32 thread proc calls
 * this after dispatch_pure. The nowloading gate (DAT_06a49958) is NOT
 * cleared here — that's the engine's per-tick scene-machine
 * responsibility, which we haven't ported yet. */
void worker_load_end(void);

/* Port of FUN_00452917 — close the worker thread handle if any, zero
 * the handle, and clear the SECONDARY worker's busy + nowloading-gate
 * flags (DAT_06a4995c / DAT_06a49960). The primary busy + gate are
 * deliberately untouched: the engine's close-helper exists for the
 * secondary family's external shutdown path, where the primary state
 * may legitimately be in flight on a parallel transition. The
 * secondary nowloading gate share is honoured via nowloading's
 * collapsed-OR model — see worker_load.c.
 *
 * Idempotent. Safe to call when no worker is running. */
void worker_load_close(void);

/* Reset all worker state — clears busy + handle + all registered
 * callbacks. Tests only; the engine has no analogue. */
void worker_load_reset(void);

/* Spawn the worker.
 *
 * Win32: ports FUN_00452cde end-to-end. Calls worker_load_begin, then
 * CreateThread on the internal thread procedure. The thread reads
 * `g_scene_state`, dispatches via worker_load_dispatch_pure, runs
 * worker_load_end, closes its own handle, and exits.
 *
 * Non-Win32: calls worker_load_begin only — no actual thread is
 * created. Tests that want to exercise the dispatch should call
 * worker_load_dispatch_pure + worker_load_end directly. This split
 * lets scene-state code call worker_load_spawn unconditionally on
 * both platforms without spinning up a real thread under the unit
 * test build. */
void worker_load_spawn(void);

/* ─── alt primary worker (FUN_00452eed + LAB_00452a6b) ──────────────────
 *
 * Same primary gates as worker_load_spawn (busy + nowloading raised),
 * different thread routine. The alt thread proc runs the registered
 * `alt_cb` (if any) in place of the 17-entry table dispatch, then
 * shares the primary cleanup. */

/* Register the alt thread proc body. Pass NULL to clear. Last write
 * wins; safe to overwrite. The callback runs on the worker thread on
 * Win32 and synchronously under tests via worker_load_dispatch_alt_pure. */
void worker_load_set_alt_cb(worker_load_cb cb);

/* Inspect the currently-registered alt callback (NULL if none). */
worker_load_cb worker_load_get_alt_cb(void);

/* Pure-C side of the alt thread proc body — invokes the registered
 * alt cb if any. Always returns 1 (the engine's LAB_00452a6b never
 * short-circuits; it always reaches its cleanup tail with eax=1). */
int  worker_load_dispatch_alt_pure(void);

/* Spawn the alt worker. Win32: port of FUN_00452eed — raises primary
 * gates, then CreateThread on the alt thread proc which runs the
 * registered alt cb + shared primary cleanup. Non-Win32: gates-only
 * (mirrors worker_load_spawn's split). */
void worker_load_spawn_alt(void);

/* ─── secondary worker family ────────────────────────────────────────── */

/* Nine thread proc body slots — one per LAB_00452*. Pair these up
 * with their spawners via the mapping in this header's banner. */
#define WORKER_LOAD_SEC_BODY_AAB    0   /* LAB_00452aab — paired with d07           */
#define WORKER_LOAD_SEC_BODY_AE8    1   /* LAB_00452ae8 — paired with d3e param==0 */
#define WORKER_LOAD_SEC_BODY_B13    2   /* LAB_00452b13 — paired with d3e param!=0 */
#define WORKER_LOAD_SEC_BODY_B3E    3   /* LAB_00452b3e — paired with d85          */
#define WORKER_LOAD_SEC_BODY_B82    4   /* LAB_00452b82 — paired with dc1          */
#define WORKER_LOAD_SEC_BODY_BC6    5   /* LAB_00452bc6 — paired with dfd          */
#define WORKER_LOAD_SEC_BODY_C0A    6   /* LAB_00452c0a — paired with e39          */
#define WORKER_LOAD_SEC_BODY_C4E    7   /* LAB_00452c4e — paired with e75          */
#define WORKER_LOAD_SEC_BODY_C96    8   /* LAB_00452c96 — paired with eb1          */
#define WORKER_LOAD_SEC_BODY_COUNT  9

/* Register the inner-body callback for `body_id`. Pass NULL to clear.
 * Out-of-range indices silently ignored. Last write wins. */
void worker_load_set_sec_body(int body_id, worker_load_cb cb);
worker_load_cb worker_load_get_sec_body(int body_id);

/* FUN_00452d07's pre-spawn slot — engine calls FUN_0046c01e on the
 * main thread before CreateThread. Other spawners have no pre-spawn.
 * NULL = no-op (current state — FUN_0046c01e not yet ported). */
void worker_load_set_sec_d07_pre_spawn(worker_load_cb cb);
worker_load_cb worker_load_get_sec_d07_pre_spawn(void);

/* Per-kind state bytes. See header banner for the spawner/body
 * write-side mapping. Initial value 0; spawners set 2 (pending) and
 * thread procs set 1 (ready). The aab body's `1c8`/`984`/`audio` are
 * write-only by aab (no pending → ready transition). */
extern int32_t g_worker_sec_state_1c8;
extern int32_t g_worker_sec_state_1cc;
extern int32_t g_worker_sec_state_1d0;
extern int32_t g_worker_sec_state_1d4;
extern int32_t g_worker_sec_state_1d8;
extern int32_t g_worker_sec_state_984;
extern int32_t g_worker_sec_state_audio;

/* DAT_06a49980 — param latched by all 8 spawners; read by 6 thread
 * procs to gate the fade-kick (param != 1 → fade_phase_out_start(0,
 * 0x11)). Exposed for tests + observability. */
extern int32_t g_worker_sec_param;

/* Pure-C "begin" — raise the secondary busy + nowloading gates.
 * Mirrors `worker_load_begin` for the primary. */
void worker_load_begin_secondary(void);

/* Pure-C "end" — clear secondary busy. Nowloading gate stays raised
 * (same per-tick-clear quirk as `worker_load_end`). */
void worker_load_end_secondary(void);

/* Pure-C side of each thread proc's inner-body invocation. Runs the
 * registered body callback (if any) for `body_id`. Returns 1 if
 * `body_id` was in range, 0 otherwise. Does NOT touch handle / busy
 * / gate / state bytes — that's the cleanup + post-body's job. */
int  worker_load_dispatch_sec_pure(int body_id);

/* Pure-C side of each thread proc's post-body machinery. Writes the
 * per-LAB_* "ready" state byte, conditionally calls fade-kick (six of
 * nine bodies), and runs aab's two extra writes. Out-of-range body_id
 * is a no-op. Called by the thread proc AFTER the shared cleanup
 * tail and by tests directly. */
void worker_load_sec_post_body(int body_id);

/* The 8 spawn entry points. Each:
 *   1. (Optionally) Pre-writes per-kind pending=2 state byte.
 *   2. Calls worker_load_begin_secondary (busy + nowloading raised).
 *   3. Latches `param` into g_worker_sec_param (DAT_06a49980).
 *   4. (d07 only) Calls registered pre-spawn callback.
 *   5. On Win32: CreateThread on the picked thread proc.
 *      On non-Win32: no thread spawned — tests drive dispatch + post
 *      + end explicitly.
 *
 * The d3e variant picks LAB_00452ae8 (param==0) or LAB_00452b13
 * (param!=0) for its thread proc.
 */
void worker_load_spawn_d07(int param);
void worker_load_spawn_d3e(int param);
void worker_load_spawn_d85(int param);
void worker_load_spawn_dc1(int param);
void worker_load_spawn_dfd(int param);
void worker_load_spawn_e39(int param);
void worker_load_spawn_e75(int param);
void worker_load_spawn_eb1(int param);

#endif /* OPENRECET_WORKER_LOAD_H */
