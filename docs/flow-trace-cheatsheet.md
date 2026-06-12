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

## Cutscene / mode-6 verdict — when `--align-field db054` says "no shared values"

db054 is a HOUSE free-roam bob/sparkle counter — it does **not** advance during a
dialogue cutscene (mode 6 guild/town events, prologue iv*.ivt), so it's absent on
**both** sides and can't be the clock. Don't "extend the probe set" — the cutscene
is already richly probed via `dialogue_tick` (`FUN_0046c320`: box_open/reveal/
line_row/st5_*). Align by a CONSTANT frame offset from a shared dialogue anchor:

```sh
# offset taken from the first occurrence of the anchor; clip the pre-text fade-in
# (its origin rides the load-suppression seam) by starting at that anchor's frame.
nix develop --command python3 tools/flow_diff.py --retail $R --port $P --verdict \
    --align-anchor TEXT_ANIM_START --frame-from <retail TEXT_ANIM_START frame>
#   anchors default to anchors.jsonl beside each call_trace; override with
#   --retail-anchors/--port-anchors. --frame-to clips a trailing load-seam tail.
```

`trace_studio triage <session>` does this **automatically**: if db054 yields no
shared values it re-aligns by `TEXT_ANIM_START` and reports the cutscene verdict
(`merchants-guild` → ✅ PHASE-CLEAN: dialogue+fade+rngcalls ALIGNED, raw rng bit-
exact). render_quad_add/flush are deferred to `render_diff.py` (per-draw geometry
can't be classified by the verdict's per-frame occurrence pairing). Worked example +
the 3-way frame-exact proof: `findings/merchant-guild-RE.md` "CENSUS DONE".

**⚠️ PHASE-CLEAN proves the PROBED STATE is 1:1, NOT the full visual render.** The
verdict only compares the fields the stubs emit (dialogue_tick box_open/reveal/…,
fade, rng). The *surrounding scene's* draw (menus, HUD, backdrops) is invisible to
it. A cutscene can be PHASE-CLEAN yet still differ on-screen — e.g. the guild iv1_9
reminder came back PHASE-CLEAN but the port was hiding the main menu that retail
keeps rendered behind it (caught only by a content-matched frame compare; `aa773d0`).
**So after a PHASE-CLEAN verdict, ALWAYS also eyeball one content-matched port|retail
frame** (anchor-offset, not the seam-broken label-paired studio diff) before claiming
visual 1:1.

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

**Gate the port emit to the SAME state the retail hook fires in — or you flood
the trace with stale rows.** Retail's hooked function only runs in its own state;
the port mirror often runs more broadly. `0x48670f` (`scene1_player_ctrl_tick`)
is called every frame through the iv1_1/iv1_2 prologue too, where the actors sit
at the `pose_house_standing` init state (px −0.30, pcnt 25, coct 4) and the
free-roam walk arm is gated off. Emitting there as well floods the trace with
**stale pose rows** (1562 stale vs 270 live on one drift capture) that `flow_diff`
mis-pairs against retail's clean free-roam rows — it surfaced as a **PHANTOM**
companion-facing `coct 6/4` "divergence" (the live free-roam rows were bit-
identical to retail). Fix (`scene1_player_ctrl.c`): gate the `CALL_TRACE_BEGIN`
on the real-free-roam condition `s_actor_char[0] != -1 && s_cc08 == 1 &&
!scene1_intro_dialogue_active() && !scene1_intro_dialogue_loading()` — the same
guard the walk arm uses. After: 271 rows, all ALIGNED. **Lesson:** if a HOUSE
actor-state field shows a constant offset, first confirm the port isn't emitting
the row from a non-free-roam frame; a quick `distinct px` histogram (one stale
value + the live value) is the tell.

## Standard once-per-frame anchors already wired
| VA | name | carries |
|----|------|---------|
| `0x47be92` | `tick_scheduler` | `rng` (LCG state), `rngcalls` (consumption) |
| `0x48670f` | `house_update` | player+companion `poct/pang/coct/px/py/pz/cx/cz/anim/frame` |
| `0x49a59e` | `scene_title_sim` | menu state machine (10 fields) |
| `0x46c320` | `dialogue_tick` | opening-prologue standee + box-anim state |

## Draw looks wrong but its state "looks identical"? → `d3d_state_at_draw.py`

`flow_diff` is for SIM/logic divergence; `render_diff.py --explain` names the wrong
*draw*. But when a draw renders wrong and the per-frame d3d state looks the SAME on
the good and bad frames, the culprit is **INHERITED device state** — d3d state
(COLOROP/COLORARG/blend/filter) is persistent across frames, so a state set once and
never re-set won't show as a per-frame difference. `tools/d3d_state_at_draw.py`
replays the trace carrying the FULL device state FORWARD and prints the complete
pipeline at any matching draw:

```
nix develop --command python3 tools/d3d_state_at_draw.py <side>/d3d_trace.jsonl \
    --frames <good>,<bad> --tex item_win [--region X0,Y0,X1,Y1]
```

Run it on BOTH the port and the matching retail frame and diff the output — that is
the reliable color-pipeline ground truth. (It cracked the white-UI bug: retail sets
ZERO COLORARG in a whole menu frame; the port's 3D renderers leak COLORARG1=DIFFUSE/
COLORARG2=CURRENT into the 2D UI when the walk-dust stops.)

Cross-refs: `docs/plans/execution-flow-trace.md`, `docs/findings/render-diff.md`
(the draw-side twin `render_diff.py --explain`).
