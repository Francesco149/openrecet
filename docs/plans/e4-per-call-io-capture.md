# Phase E.4 — Per-call I/O capture (implementation plan)

> Detailed executable plan for the heaviest chip in the leaf-first roadmap.
> Pointed to from `harness-roadmap.md` §E.4.
>
> **Status: Tier 1 LANDED (2026-05-29).** First two stateful leaves
> (`stage_gate_boss_id_allowed` / `floor_is_checkpoint`) are bit-exact vs
> retail through the existing `diff_test.py` oracle — see
> `findings/pure-function-diff.md` §"E.4 Tier 1". Tiers 2–3 remain planned.
>
> **Plan correction (see Tier 1 below):** Tier 2's tick-freeze is NOT a
> prerequisite for Tier 1 here — the `diff_test.py` harness already spawns
> retail `CREATE_SUSPENDED` and never resumes, so the engine is frozen for
> the whole run and there is no race. Tier 2 is only needed for a *live*
> retail (capturing a leaf's I/O mid-scenario at a real frame).

## Why this is the critical chip

Today the leaf-first loop (`tools/call_trace_diff.py` + `CALL_TRACE_ENTER`
probes) verifies **call-count parity**: retail called function X N times this
frame, and so did the port. That catches *structural* divergence — a leaf we
never reach, or one we call that retail doesn't. It does **not** catch a leaf
that is called the right number of times but **computes the wrong answer**.

For a "behaviorally-complete faithful port," the acceptance test for a leaf is
stronger:

> **same inputs ⇒ same return value + same memory delta.**

E.4 is the chip that measures that. Without it, the E.5 iteration loop can
declare a leaf "done" on count parity while it silently diverges — the exact
failure mode that produces a port that runs but plays subtly wrong. It is the
difference between *structural* and *faithful* parity, and faithful is the goal.

## Design principle: extend what works, don't build the cathedral first

The project already has a **bit-exact I/O oracle** that works end-to-end:
`tools/diff_test.py` + `tests/build/libengine_diff.so` + the Frida
`runRetail*` RPCs (rng_next15 lands 200/200 vectors bit-exact vs retail; the
audio_fade migration adds the second target). That harness already does
"same input ⇒ same output" for **pure** functions.

E.4 generalises it to **stateful** leaves in three escalating tiers. Land them
in order; each is independently useful and the early tiers cover most leaves.

---

## Tier 1 — Stateful pure-ish leaves via the existing oracle (small)

Many "leaf" functions read/write a handful of known globals but are otherwise
deterministic (e.g. `scene1_records_b_tick` per-slot dispatch, the
stage-transition gate pair `0x4319d6`/`0x43195d` from the frame-59 work, RNG
consumers). For these, no new capture machinery is needed — extend the
`diff_test.py` `Target` pattern:

- **Vector** = `{input_globals: {addr: value, ...}, args: [...]}`.
- **Port side** (`src/diff_entry.c` + `libengine_diff.so`): a thin entry that
  (1) writes the input globals into the linked port's BSS, (2) calls the port
  leaf, (3) reads back the watched output globals + return value, (4) returns
  them. Mirror the existing `rng` / `audio_fade` entries.
- **Retail side** (Frida agent `runRetail<Leaf>` RPC): snapshot the watched
  globals, write inputs, **freeze the engine tick** (see Tier 2), call the leaf
  via `NativeFunction`, read back outputs + ret, **restore globals in a
  `finally`**, return.
- **Diff**: byte-exact compare of `{ret, output_globals}`. First mismatch dumps
  the full vector.

Watched-global sets come from the port commit's own docstring (each chip already
documents the `DAT_xxxxxxxx` it touches) — harvest them into the `Target` def.

**Deliverable:** 2–3 stateful leaves green at 200 vectors each. Proves the
pattern handles state without the heavy machinery below.
**DONE (2026-05-29):** `stage_gate_boss_id_allowed` (arg injection) +
`stage_gate_floor_is_checkpoint` (2-global injection), both 300/300 vs
retail. Touched: `src/diff_entry.{c,h}`, `tests/diff_stubs.c` (added
`g_scene1_combat_stage_id` + `g_enemylist` storage), `tests/Makefile`
(`stage_gate.c` → DIFF_SRCS), `tools/frida/openrecet-agent.js` (2 RPCs +
ADDR entries), `tools/diff_test.py` (structs/vectors/targets).

**Depends on:** ~~Tier 2's tick-freeze for the retail side to be safe.~~
**Correction:** NOT needed for the `diff_test.py` path — retail is spawned
`CREATE_SUSPENDED` and never resumed, so the engine never mutates the
injected globals. Tier 2 is only a prerequisite when running against a
*live* retail (Tier 3's mid-scenario I/O capture, or any future RPC that
fires while the engine main thread is advancing).

---

## Tier 2 — Engine-tick freeze + race-retry (medium; = roadmap D.3)

Retail's main thread is alive during a Frida RPC, so a stateful read/write can
race the engine mutating the same globals. This is **prerequisite infra** for
Tier 1's retail side and is already specified as Phase D.3 — fold it in here.

- Identify the per-tick driver (analogous to the OL2 `FUN_004b99c0`); trace from
  the main loop / WndProc to the per-frame entry. Document in
  `findings/per-tick-driver.md`.
- `Interceptor.replace(per_tick_driver, no_op)` to freeze on a tick boundary;
  RPC fires safely; uninstall to resume.
- **Race-detect retry**: snapshot + checksum the inputs pre-call; after the
  call, re-checksum to confirm they weren't perturbed; retry up to N; fail with
  `raced` if budget exceeded.
- **Cycling quirk (mandatory, pre-emptive):** install the freeze **once** per
  run, uninstall **once** at end. OL2 burned sessions on a Frida-internal
  `TypeError` from install→uninstall→reinstall cycling on the same target.

**Deliverable:** a `freeze`/`thaw` pair in the agent + a `--freeze-tick` flag
in `diff_test.py`; Tier 1 leaves pass under freeze with zero `raced` failures.

---

## Tier 3 — General memory-delta capture (large; only if Tiers 1–2 leave gaps)

For leaves whose write-set is **not** statically known (heap writes, large
struct mutations, function-pointer-driven dispatch), capture the delta
generically instead of enumerating globals.

- **Port side:** wrap the leaf call between two **shadow-page hashes** of the
  writable data segments (`.data`/`.bss` ranges + any known heap arenas).
  Implement as an extension of the `-finstrument-functions`-free
  `CALL_TRACE_ENTER` site: `call_trace_enter_io(va, ...)` snapshots a CRC of
  watched regions on enter, again on the matching exit, emits the changed
  page list + before/after bytes (bounded) into the JSONL. Gate per-frame the
  same way `d3d_trace_begin_frame` / `call_trace_begin_frame` already do, so
  cost is zero when not capturing.
- **Retail side (TTD, finally consumed):** this is where the **orphaned TTD
  harness pays off**. Extend `tools/ttd/scripts/batch_calls.js` to dump, per
  call to a target VA, the `Registers` model and the `MemoryWrites` the call
  performed (TTD records these natively — no re-execution, no race, repeatable).
  Gate with `MAX_IO_CAPTURES_PER_VA` (per-call cost is high). **This is the
  bridge that gives E.0 a downstream consumer** (see audit: TTD currently feeds
  nothing).
- **Diff orchestrator:** extend `call_trace_diff.py` (or a new
  `io_diff.py`) — for each VA present on both sides with matching args, compare
  `{ret, memory_delta}`. Same args + different delta = the leaf diverges; that
  is the next thing to fix in the E.5 loop.

**Deliverable:** for one HOUSE-scene leaf (e.g. a `scene1_walker` sub-helper),
TTD-captured retail I/O diffed against port-captured I/O, surfacing a concrete
behavioral divergence.

---

## Sequencing & cost

| tier | effort | unblocks | build when |
|------|--------|----------|------------|
| 1 | small | faithful-parity check for known-write-set leaves | first — most leaves fit |
| 2 | medium | safe stateful retail RPCs (race-free) | with/just before Tier 1 retail side |
| 3 | large | unknown-write-set leaves + gives TTD a consumer | only if Tier 1 coverage proves insufficient |

Total to "first faithful-parity leaf comparison": ~Tier 1 + Tier 2 = 2–3
sessions. Tier 3 is a further 2–3 and may never be needed if the write-sets
stay enumerable.

## Cross-cutting (carried from harness-roadmap §D cross-cutting notes)

- Every RPC that mutates retail globals **restores in a `finally`** — an
  exception must never leave the engine perturbed.
- Schema: each JSONL emitter writes a `version: N` header line.
- The host oracle (`libengine_diff.so`) stays **sanitizer-free** (separate from
  the ASan unit build); the unit suite still covers UB/memory.
- No mandatory retail dependency: E.4 is opt-in, run only when diagnosing a
  suspected behavioral divergence. Default CI remains the host suite + the new
  render-trace gate.

## Open questions to resolve at build time

- Per-tick driver VA (Tier 2) — needs the trace; candidate is the function the
  main loop calls once per `Present`.
- Heap-arena ranges for Tier 3 shadow-hashing — does the engine use a custom
  allocator with known arenas, or raw `HeapAlloc`? Determines whether the
  shadow set is bounded.
- Whether Tier 3 is needed at all — re-evaluate after Tier 1 covers the first
  ~20 leaves; if write-sets stay small and static, skip the cathedral.
