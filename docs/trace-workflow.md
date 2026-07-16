# Trace workflow — current deterministic parity loop

> **Status:** authoritative operational guide  \
> **Last verified:** 2026-07-16  \
> **Renderer workflow:** Trace Studio v3  \
> **Legacy v2 record:** `archive/trace-workflow-v2-2026-06.md`

A trace is an exact input program segmented by semantic engine anchors. The same trace
runs against the port and retail executable, with save state and selected
nondeterministic origins controlled. Trace Studio v3 captures the D3D8 command program
once, pairs frames by stored logical identity, and replays both sides in a native viewer.

The trace is the experiment. The proof contract determines what the experiment proves.

## 1. Fast paths

### Drive an existing scenario

```sh
nix develop --command python3 tools/scenario-test.py <scenario> --target both
```

Add `--call-trace` only when the scenario or investigation needs flow data. The harness
owns input replay, anchors, save virtualization, process cleanup, resolution, and target
layout. Do not manually compose `run-openrecet.sh` and `frida_capture.py` for paired
scenario runs.

### Capture and inspect a v3 window

```sh
nix develop --command python3 tools/trace_studio_v3/orv3_window.py \
  <scenario> --anchor HOUSE_FREEROAM --window 120:240 --state --view
```

`--view` writes the native viewer manifest and updates the installed “OpenRecet Trace
Studio” shortcut without launching a blocking UI process. Use `--launch` only when an
interactive launch is explicitly wanted.

The command:

1. validates/builds the requested window;
2. reuses a content-addressed retail capture when valid;
3. re-drives stale/missing port capture;
4. verifies each side replays its own reference;
5. joins port and retail by `(anchor, occurrence, offset)`;
6. writes `pairs.json` and optionally `view.json`.

Important: current `ALIGNED`/future `JOIN_COMPLETE` means the identities paired. It does
not prove cross-target pixels, draws, state, audio, or saves equal. Until
`plans/parity-evidence-roadmap.md` EP-05 lands, inspect and record those pillars
explicitly.

### Read user viewer notes

```sh
nix develop --command python3 tools/trace_studio_v3/orv3_notes.py \
  <scenario> --render
```

Read notes before starting a visual investigation. They are identity-labelled and point
to the precise frame/region the user flagged.

## 2. Canonical porting loop

1. **Scout retail live.** Use the persistent probe described in
   `live-probe-harness.md` to reach the behavior, inspect state/anchors, and test a
   candidate call/poke.
2. **Create or select a deterministic trace.** The behavior must be exercised, not
   inferred from a nearby screen.
3. **Stabilize the trace.** Use semantic anchors, exact save state, phase/RNG pins, and
   scene-specific load brackets. Verify at least two retail runs and two port runs when
   introducing a new path.
4. **Capture the smallest useful v3 window.** Add `--state` when diagnosing logic/phase,
   not only pixels.
5. **Attribute the first divergence.** Check input/upstream state, phase, RNG, game
   state, draw program, pixels, audio, and external effects in that order.
6. **Ground retail behavior.** Cross-check live/captured evidence with decompile and
   disassembly. A visual guess is not an implementation spec.
7. **Port one bounded chip.** Preserve retail quirks and x87 behavior.
8. **Re-drive the same window.** Retail should normally remain cached; port refreshes.
9. **Run focused host tests and the exact parity gate available for the chip.**
10. **Persist evidence.** Update findings/quirks/debt and, once implemented, attach the
    parity proof ID.

Work frame-0-forward. A downstream difference is not actionable until upstream inputs
and state match.

## 3. Scenario layout

```text
tests/scenarios/<scenario>/
  scenario.yaml
  trace.jsonl
  golden/                 optional port-side legacy golden artifacts
```

`scenario.yaml` currently controls description, RNG seed, frame budget, capture
count/frames, duration ceiling, load suppression, and optional zoom metadata. The
parity-evidence roadmap will extend it with versioned proof and coverage contracts.

`trace.jsonl` is an ordered segtrace. Trace-global operations such as `savefile` can
appear anywhere but apply to the whole replay. Segment-relative operations apply to the
most recent resolved anchor.

Never put live mutable status or a prose parity claim in the YAML. The scenario defines
the experiment; proof artifacts carry results.

## 4. Creating a trace

### Record

For port-side/manual recording, run the supervised debug build and use:

- `F2`: start/stop recording;
- `F3`: capture marker;
- `F4`: call-trace window.

For retail-first work, use the live probe/recorder path documented in
`live-probe-harness.md` and the existing recording wrapper. Recordings are raw
per-frame masks plus anchor firings and save snapshots.

Never launch either executable bare. Never record against the user's real save without
an explicit safe-copy plan.

### Distil

```sh
nix develop --command python3 tools/distill_trace.py \
  <raw.jsonl> --anchor-segments -o tests/scenarios/<name>/trace.jsonl
```

Useful existing options:

- `--carry-pins-from <trace>` when re-distilling an already stabilized trace;
- `--drop-fragile-after` / `--drop-fragile-region` for cosmetic anchors in a known
  auto-play region;
- `--saves-dir` for the local content-addressed save store;
- `--no-savefile` only for an intentional save-independent experiment.

Do not re-distill a stabilized long trace without carrying its load/phase pins. Do not
drop interactive-region anchors merely because they are inconvenient; prove a reliable
replacement boundary.

### Stabilize

The shared lint/auto-pin implementation currently lives at
`tools/trace_studio/edits/lint.py` even though the v2 viewer is retired. v3's trace build
uses it. Its placement is historical; its behavior remains current.

Canonical expectations:

- exact `{savefile}` (`@fresh` or content-addressed local blob);
- `{rngseed}` and `{phasepin}` at the comparison origin;
- `{calltrace}` around the state window when flow evidence is required;
- load-bracket pins sized from measured retail maxima and placed in the correct segment;
- `{caprange}` large enough for v3 capture-once/slice-many;
- deterministic completion at a semantic anchor, not a wall-clock sleep.

Pin values are scene- and trace-specific. Copy them from a proven sibling trace only
after verifying the same load path. Old prose values in archived docs are not defaults.

Verify reproducibility:

```sh
nix develop --command python3 tools/scenario-test.py <scenario> --target retail
nix develop --command python3 tools/scenario-test.py <scenario> --target retail
nix develop --command python3 tools/scenario-test.py <scenario> --target openrecet
nix develop --command python3 tools/scenario-test.py <scenario> --target openrecet
```

Compare anchor sequence, RNG value/draw count, captured state, and completion—not only
process exit.

## 5. Segtrace operations

The executable parser is `src/input_segtrace.c`; the retail mirror is
`tools/frida/openrecet-agent.js`; the Python lowering path is
`tools/frida_capture.py`. A new operation must be implemented and tested in all relevant
parsers.

Common operations:

| Shape | Meaning |
|---|---|
| `{"frame": N, "buttons": "0xNN"}` | set held engine mask at segment-relative frame |
| `{"wait": "ANCHOR"}` | start next segment at the next anchor occurrence |
| `{"wait_until": {...}}` | segment boundary on a typed live-state predicate |
| `{"capture": N}` | legacy screenshot point |
| `{"caprange": [start, count]}` | full-extent capture window |
| `{"calltrace": [start, count]}` | call/state trace window |
| `{"rngseed": [frame, value]}` | set LCG state before simulation |
| `{"phasepin": N}` | normalize mapped load-dependent frame origins |
| `{"tutloadpin": N}` | segment-scoped tutorial/dialogue load minimum |
| `{"csloadpin": N}` | customer-service load minimum |
| `{"primaryloadpin": N}` | primary worker/load minimum |
| `{"esc": N}` | synthesize engine ESC dispatch |
| `{"savefile": "@fresh"}` | start without a save |
| `{"savefile": "../_saves/<sha>.sav.gz"}` | seed the sandbox from exact save bytes |
| `{"memsnap": N}` | diagnostic memory snapshot |

Additional research-specific pins/operations exist. Read all three parsers and focused
tests before editing the grammar. Preserve unknown-op failure behavior; silently ignoring
an operation can produce a convincing but invalid trace.

## 6. Save virtualization

Both targets replay in per-run sandboxes:

- `@fresh`: no initial save;
- content-addressed `.sav.gz`: decompressed into sandboxed `save.dat`;
- absent `savefile`: legacy behavior; avoid for new deterministic scenarios.

Retail file hooks redirect `save.dat`/`_save.dat`; the port uses its save override/write
directory. Raw recordings can contain `{save_write}` events. Distillation stores
in-session save snapshots in a sidecar.

Current limitation: capture does not yet constitute a cross-target save equality proof.
The roadmap's ST-01 package will validate save blob hashes and compare every sandboxed
write. Until then, manually hash and inspect save outputs when save behavior is the chip.

Never allow a replay or test to write the user's actual save. Cleanup and restoration
belong in `finally` paths.

## 7. Trace Studio v3 identities and artifacts

Logical frame identity is:

```text
(anchor_name, anchor_occurrence, offset_since_anchor)
```

This survives different absolute load/present counts. `--join-anchor NAME` rebases a
window when both sides armed on different occurrences of the base anchor. Use it only
when the named event is proven to be the same semantic boundary.

Main artifacts:

```text
runs/studio-v3-cache/<scenario>-<key>/{retail,port}/
runs/studio-v3-windows/<...>/
  port/
  retail/
  pairs.json
  view.json
```

Exact paths may evolve; use the paths printed by `orv3_window.py`, not hard-coded scripts.

Cache controls:

- `--force-retail`: re-drive retail;
- `--force-port`: re-drive port;
- `--reuse-port`: intentionally accept a port capture older than the executable;
- `--no-verify`: skip same-side replay verification; never use for a parity claim.

Until provenance re-keying lands, force recapture whenever retail/port executable, save,
assets, configuration, proxy, agent, runtime environment, or capture flags changed even
if the current cache considers the entry fresh.

## 8. What current tools prove

| Observation | Sound conclusion |
|---|---|
| identity join complete | corresponding logical frames were found |
| same-side replay exact | capture container/replayer reproduced that side's reference |
| pixel diff exact | compared replayed output pixels match for the paired frames |
| draw/material diff aligned | measured render program/state fields match under that comparator |
| flow field aligned | captured named fields match over the window |
| call counts aligned | captured call counts match; return/writes may still differ |
| audio identity/count aligned | IDs/counts match; timing/fades/PCM may still differ |
| human confirmed 1:1 | user accepted the recorded scope and evidence |

No single current row proves all pillars. Use precise wording in findings and commits.

## 9. Common failure classes

- **Zero frames:** anchor never reached, trace parked on a past/fragile anchor, or frame
  budget too small. Inspect anchor stream and current segment.
- **Retail-only load drift:** completion-based worker race; measure bracket and use a
  bilateral extend-only load pin at a reliable boundary.
- **Join gaps:** inputs/anchor occurrence differ, capture extent misses frames, or the
  behavior truly diverged. Do not pair by ordinal to hide it.
- **Same-side replay failure:** recorder/replayer/capture completeness problem; fix before
  cross-target analysis.
- **Pixels differ but state is equal:** inspect draw program, inherited D3D state,
  resources, phase, and environment.
- **State differs before pixels:** fix the first state/mutation divergence.
- **RNG call count equal but value differs:** draw order/consumer differs; not phase.
- **One lucky capture:** repeat both harnesses/runs before calling a new trace stable.
- **Stale cache:** force the affected side and record why; roadmap EP-08 will make this
  automatic.

## 10. Legacy tools

`tools/trace_studio/` and `tools/trace_studio.py` remain because v3 reuses selected
trace-building/lint code and historical investigations may need old artifacts. They are
not the visual parity front end.

Build history:

- `plans/archive/trace-studio-v2.md`
- `plans/trace-studio-v3.md`
- `archive/trace-workflow-v2-2026-06.md`

Do not add new v2 UI/workflow features. Move reusable model/lint pieces only when a
bounded migration includes tests and no active v3 caller is broken.
