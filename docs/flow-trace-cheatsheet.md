# Flow-trace cheatsheet — the ONE way to compare port↔retail state

The flow-trace is how we compare *any* per-frame state between the port and
retail. It replaces the retired ad-hoc per-frame logs (`--player-pos-log`,
`--dlg-log`, `--dust-log`, retail `--watch`) and the old `phase_probe.py`. The
rule of thumb: **don't add a one-off debug flag — annotate the function on both
sides and let `flow_diff` compare it.** Coverage grows every session.

Three pieces:
1. **Port** declares fields at a function via `CALL_TRACE_BEGIN(va)/…FIELD…/END`
   (`src/call_trace.h`).
2. **Retail** declares the same-named fields for that VA in
   `tools/flow/retail_fields.json` (the Frida agent reads them).
3. **`tools/flow_diff.py`** joins the two `call_trace.jsonl` by `(va, frame, seq)`
   and field name.

---

## Capture both sides (one command)

```sh
nix develop --command python3 tools/scenario-test.py <scenario> \
    --target both --call-trace            # add --d3d-trace[-verts] for draws
```
Needs a `{calltrace:[start,len]}` op in the scenario's `trace.jsonl` (anchor-
relative, same window as `{caprange}`). Outputs:
- `runs/scenario-test/<scn>/openrecet/call_trace.jsonl`
- `runs/scenario-test/<scn>/retail/call_trace.jsonl`

For a clean comparison the scenario should also carry `{phasepin:N}` (zeros the
load-dependent phase: db054 / anim / b154 on both sides) and, when RNG-driven
particles are in frame, `{rngseed:[N,seed]}`. With db054 pinned, frame numbers
are a shared clock — `flow_diff` pairs frames directly.

## Ask the three questions

```sh
R=runs/scenario-test/<scn>/retail/call_trace.jsonl
P=runs/scenario-test/<scn>/openrecet/call_trace.jsonl

# 1. PHASE / RNG VERDICT — "is a divergence load-dependent phase, or real logic?"
nix develop --command python3 tools/flow_diff.py --retail $R --port $P --verdict
#   per field: ALIGNED / CONST-OFFSET (phase-sync, NOT logic) / DRIFT (real),
#   plus an authoritative rngcalls-consumption row. Exit 1 only on DRIFT/DESYNC.

# 2. WHICH FIELD first stopped tracking retail, and WHEN?
nix develop --command python3 tools/flow_diff.py --retail $R --port $P \
    --field-timeline                      # add --timeline-va 0x48670f to focus one
#   prints actual per-field VALUES both sides + first divergent (frame, field).

# 3. In a KNOWN-BAD frame, which CALL's data diverged first (execution order)?
nix develop --command python3 tools/flow_diff.py --retail $R --port $P \
    --retail-frame F --port-frame F       # or --mapped-only over common frames
```

## RNG drill — which functions consume the LCG (incl. unported ones)?

```sh
nix develop --command python3 tools/frida_capture.py --input-segtrace \
    tests/scenarios/<scn>/trace.jsonl --rng-callsites N   # N frames after the pin
nix develop --command python3 tools/flow_diff.py \
    --rng-drill runs/.../rng_callsites.json               # aggregate by function
```

---

## Annotate a NEW function (do this as you port / debug)

**Port side** — at the function entry, BEFORE any traced sub-call, read the
INPUTS/state (so it mirrors retail's onEnter read):

```c
#include "call_trace.h"
...
CALL_TRACE_BEGIN(0x48670fu);        /* or _STUB if the port body is partial   */
CALL_TRACE_I32("poct", rec[CHR_ACTOR_FACING]);
CALL_TRACE_F32("px",   g_scene1_player_pos[0]);
CALL_TRACE_END();
```

**Retail side** — `tools/flow/retail_fields.json`, keyed by the same VA:

```json
"0x48670f": {
  "name": "house_update",
  "fields": [
    {"name": "poct", "src": "global", "va": "0x56dab00", "type": "i32"},
    {"name": "px",   "src": "global", "va": "0x56da1d8", "type": "f32"}
  ]
}
```

Field `src`: `global` (read a DAT at `va`) · `arg`/`argderef` (Frida onEnter
arg, 0-based index [+`off`]) · `rngcalls` (cumulative LCG draws) · `retval`
(onLeave). `type`: `i32|u32|f32|hex`.

**Timing rule:** the agent reads `src:global` at the hooked function's **onEnter**
(before its body runs). So emit the port `CALL_TRACE_BEGIN` at the function's
entry too, and read INPUT state. For a per-frame STATE dump, hook a function that
runs **once per frame** (`0x48670f` = HOUSE update, `0x49a59e` = title sim,
`0x47be92` = scheduler) and read the globals there — both sides see the same
frame-start values.

**Mark benign / chain-benign** in the spec when a field legitimately differs
(phase-origin counter, RNG seed origin, layout pointer) so it surfaces as
`⚠ accepted` with a `reason`, not a false ✗. See existing stanzas
(`scene_title_sim`, `fade_tick`) for the pattern.

## Standard once-per-frame anchors already wired
| VA | name | carries |
|----|------|---------|
| `0x47be92` | `tick_scheduler` | `rng` (LCG state), `rngcalls` (consumption) |
| `0x48670f` | `house_update` | player+companion `poct/pang/coct/px/py/pz/cx/cz/anim/frame` |
| `0x49a59e` | `scene_title_sim` | menu state machine (10 fields) |
| `0x46c320` | `dialogue_tick` | opening-prologue standee + box-anim state |

Cross-refs: `docs/plans/execution-flow-trace.md`, `docs/findings/render-diff.md`
(the draw-side twin `render_diff.py --explain`).
