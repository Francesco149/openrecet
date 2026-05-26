# Pure-function differential testing

Phase D.1 of `docs/harness-roadmap.md`.  Cross-check each ported pure
function's output against the **actual retail unpacked exe** by:

  - **Agent**: invoke retail's `FUN_*` directly via Frida `NativeFunction`,
    with the relevant globals snapshotted-injected-readback-restored so
    the engine state is left exactly as we found it.
  - **Orchestrator** (`tools/diff_test.py`): build a small host shared
    library (`tests/build/libengine_diff.so`) from our pure-C ports and
    load it via `ctypes`.  Drive the same input vectors through both,
    diff outputs bit-exact.

This catches arithmetic / table-lookup / shift drift in our ports
*without* needing a full game-state scenario to surface it.  The next
LCG state after seed=1 (the engine's `.data` initial value of
`DAT_006023a0`) is always included as a fixed-edge vector, so any
regression in our port that would corrupt the very first random call
on boot is caught immediately.

This is the generic replacement for the bespoke `tools/state_diff/`
pattern (one-off subprocess oracle + per-target driver).  The original
`tools/state_diff/lcg_fade.py` remains in place as a regression gate
until D.2 migrates `audio_fade_compute` over.

## Current targets

| function name      | retail address  | port file   | what it does                                |
|--------------------|-----------------|-------------|---------------------------------------------|
| `rng_next15`       | `FUN_005041f6`  | `src/rng.c` | single LCG step (`*0x343fd + 0x269ec3`); 15-bit return  |

Each entry's host-side unit-test file (e.g. `tests/test_rng.c`)
already proves the port matches our spec.  The diff test complements
those by proving they match **retail's behavior** for the same inputs,
not just our spec.

## Run

```fish
nix develop
make -C tests diff             # one-shot — build libengine_diff.so
python3 tools/diff_test.py     # 200 vectors per target (default)
```

The orchestrator builds `libengine_diff.so` on first run if missing,
so a bare `python3 tools/diff_test.py` works from a clean clone.

Knobs (full list in `--help`):

  - `--functions rng_next15` — comma-separated subset (default: all)
  - `--vectors N` — vectors per target (the first 7 are fixed edge
    cases; the rest are seeded random)
  - `--seed N` — RNG seed for vector generation (default 0;
    reproducible; accepts `0x`-prefixed hex)
  - `--frida-remote host:port` — defaults to `cutestation.soy:27042`
  - `--warmup-s N` — agent-ready timeout (default 10s — typically
    completes in <100 ms since retail stays suspended)

Latest verified run: **2000/2000 vectors pass for rng_next15**
(2026-05-26, retail build via cutestation.soy, seed 0xdeadbeef,
2.2 s end-to-end).

## Architecture

```
                                 ┌───────────────────┐
                                 │ libengine_diff.so │
                                 │  (host-loadable)  │
                                 │   src/rng.c       │
                                 │   src/diff_entry.c│
                                 └─────────▲─────────┘
                                           │ ctypes
┌────────────────────┐         ┌───────────┴─────────┐
│ tools/diff_test.py │────────▶│       port runner   │
│   - argparse CLI   │         └─────────────────────┘
│   - target dict    │
│   - vector gen     │         ┌─────────────────────┐
│   - result diff    │────────▶│      retail runner  │
└────────────────────┘         └───────────┬─────────┘
                                           │ frida RPC
                                 ┌─────────▼─────────┐
                                 │ openrecet-agent.js│
                                 │  runRetailRng…    │
                                 │  diff_test: true  │
                                 └─────────┬─────────┘
                                           │ NativeFunction
                                 ┌─────────▼─────────┐
                                 │  retail process   │
                                 │  (suspended;      │
                                 │   helper thread   │
                                 │   runs agent)     │
                                 └───────────────────┘
```

The Frida-injected helper thread runs independently of the engine
main thread.  Retail is spawned `CREATE_SUSPENDED` and never resumed
— no engine code executes during a diff run, so there's nothing
reading or writing `DAT_006023a0` between our snapshot and restore.
This eliminates the entire race surface for D.1.

## What D.1 does NOT do (yet)

The harness is intentionally minimal at D.1.  Future phases will add:

  - **Race-detect retry** (planned D.3): for stateful targets where
    the engine main thread is running, retry on snapshot perturbation
    up to N times before declaring a true mismatch.  Not needed at
    D.1 — the engine is suspended.
  - **Engine-tick freeze** (planned D.3): `Interceptor.replace` the
    per-tick driver with a no-op for the duration of a diff run.
    Required when the engine main thread is running AND globals
    we touch are also touched per-tick.  Not needed at D.1.
  - **Per-call timestamp injection** (planned D.2 if `audio_fade`
    needs it): scripted `timeGetTime`-style returns via a FIFO.
  - **D3D state-trace diff** (planned D.4/D.5/D.6): completely
    separate orchestrator (`tools/render_diff.py`) that uses the
    same agent surface for state-trace JSONL emission.

See `docs/harness-roadmap.md` Phase D for the full multi-session
plan.  Cross-ref the OL2 reference at
`../OpenLords2/docs/findings/pure-function-diff.md` for the advanced
patterns (race-detect retry, FIFO clock injection, etc.) that we'll
port as needed.

## Adding a new target

Three files to touch:

  1. **`src/diff_entry.h`**: append a new
     `Engine<Target>In` + `Engine<Target>Out` struct pair.  Wire
     stability rule: append new fields at the end; never reorder.
  2. **`src/diff_entry.c`**: add `engine_<target>(in*, out*)` that
     marshals the In struct into our port's normal call surface
     and stuffs the result into Out.
  3. **`tools/frida/openrecet-agent.js`**: add `runRetail<Target>`
     to `rpc.exports`.  Pattern:

     ```javascript
     runRetailMyTarget: function (arg1, arg2) {
         if (!g_diff_test_enabled) {
             throw new Error('runRetailMyTarget: diff_test mode required');
         }
         ensureBase();
         const snap = {state: rva(ADDR.var_my_state).readU32()};
         try {
             rva(ADDR.var_my_state).writeU32(arg1 >>> 0);
             const fn = new NativeFunction(rva(ADDR.fn_my_target),
                                           'uint32', []);
             const ret = fn() >>> 0;
             return {ret_value: ret,
                     post_state: rva(ADDR.var_my_state).readU32()};
         } finally {
             rva(ADDR.var_my_state).writeU32(snap.state);
         }
     }
     ```

     Add `ADDR.fn_my_target` + `ADDR.var_my_state` to the table at
     the top of the agent if they're not already there.
  4. **`tools/diff_test.py`**: add a `ctypes.Structure` mirror, four
     callables (`gen_*_vectors`, `run_port_*`, `run_retail_*`,
     `diff_*`), and a `Target` registry entry.  Add the new symbol
     binding to `load_port_lib()`.
  5. **`tests/Makefile`**: if the target pulls in a new `src/*.c`,
     append to `DIFF_SRCS`.
  6. **This doc**: add a row to the "Current targets" table.

## Vector design

Each target's `gen_*_vectors(n, rng)` returns a deterministic list of
N inputs.  Pattern: a fixed-edge prefix (typically 7 vectors —
boundary values that random sampling won't reliably produce) followed
by random fill via `rng.getrandbits(...)`.  The orchestrator's
`--seed` flag pins the RNG so a re-run on the same retail build is
reproducible.

Why edges first?  Random `u32` rarely produces `0` / `1` /
`0xFFFFFFFF` / `0x80000000`; those are the values most likely to
expose signed/unsigned slips, wraparound bugs, or absorbing fixpoints.
Putting them up-front means a regression that breaks only at edge
values still fails the first time the diff runs.

## Cross-references

  - `tools/diff_test.py` — orchestrator.
  - `src/diff_entry.{c,h}` — ABI shims.
  - `tests/Makefile` — `diff` target builds `libengine_diff.so`.
  - `tools/frida/openrecet-agent.js` — agent RPCs.
  - `docs/harness-roadmap.md` Phase D — full multi-session plan.
  - `../OpenLords2/docs/findings/pure-function-diff.md` — reference
    pattern, advanced cases (race detect, FIFO clock injection,
    engine-tick freeze).
  - `tools/state_diff/lcg_fade.py` — original bespoke driver,
    superseded but kept until D.2 migrates audio_fade.
