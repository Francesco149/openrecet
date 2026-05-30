# TAS framework — deterministic anchors for port↔retail parity (plan)

> **Status: PLANNED (2026-05-30).** Design doc, not yet implemented.
> Motivated by the texture-filtering verification session: the mip fix was
> *correct* but I could not produce the clean book/blind pixel-diffs the
> user wanted, because port and retail could not be driven to the **same
> deterministic state**. This plan is the fix for that whole class of
> problem. Pairs with `tools/pixel_diff.py` (the diff renderer, already
> landed) and the existing capture harnesses (`scenario-test.py`,
> `frida_capture.py`, the Frida agent).

## Design principle: the trace is the source of truth

**One TAS trace must drive the port and retail *identically* — assuming the
port is accurate.** The input trace (+ a pinned RNG seed and clock) is the
single source of truth; a faithful port is a deterministic function of it,
bit-for-bit in lockstep with retail. This has two consequences that shape
everything below:

1. **Anchors are a correctness signal, not just an alignment crutch.** If
   the same trace makes anchor `HOUSE_FREEROAM` fire at port-frame 3300 but
   retail-frame 6000, that gap is **evidence** — either a port inaccuracy
   (e.g. we skip/shorten the intro the dialog/HUD aren't ported yet) or a
   **determinism leak** (RNG/clock/timing not pinned). The anchor stream
   *localises* the first divergence. As the port becomes accurate, anchor
   frames **converge**; in the limit the per-side anchor remap is the
   identity and the trace alone suffices, no realignment needed.
2. **Determinism must be pinned before "identical behaviour" is even
   testable.** The same trace can only behave identically if every
   non-input source of variation is pinned on both sides: **RNG seed**
   (`DAT_006023a0`), **clock/QPC** (virtual time — the port's `--turbo`
   already does fixed 17ms/frame; retail needs the same QPC override),
   and frame-step. These were marked out-of-scope in the old
   `autonomous-session-tasks.md`; for this framework they are **P1
   prerequisites**, not optional.

So the framework does double duty: while the port is incomplete, anchors
let us still align + diff what *is* ported (today's need); once accurate,
the *same* anchors become a divergence alarm — the trace is replayed on
both and any anchor-frame mismatch is a regression to investigate.

## The problem, concretely

To compare a rendered region between the port and retail we need both
binaries at the *same scene, same sub-state, same camera, same frame
within that state*. Today we drive each side by **absolute frame number
from boot**, and that breaks the moment timing diverges:

- **Frame numbers don't correspond.** The port reached free-roam HOUSE at
  frame ~3300; retail (same input trace) was still in the intro dialog at
  frame 6000–16000. Boot timing, loading durations, and intro pacing all
  differ, so "frame N" means different things on each side.
- **Sub-state differs even when the scene matches.** At my best-aligned
  frames the *room camera* matched (dx=dy=0) but retail had the **intro
  dialog box** occluding the front book and the **un-ported 2D HUD**
  (1,000-pix banner, Day wheel) over the back blinds, while the port was
  in dialog-free free-roam. Different sub-state → contaminated diff.
- **No way to freeze a canonical state.** A bit-exact diff wants the
  player at a fixed position with a frozen camera on *both* sides. The
  port can seed that; retail can't, without forcing globals + pausing.

The throughput tax is real: I burned most of a session hand-driving
traces, hunting frame numbers, and cropping around occluders. We will hit
this on every future render-parity chip (the HUD, combat, dungeons, the
buy/sell screens). A TAS framework with **deterministic anchors** removes it.

## The idea: anchor the timeline to events, not frame counts

Instead of "capture at frame 6000," express timing as **named anchors**
defined by an observable event, and capture **relative to an anchor**:

```
anchors:
  - name: HOUSE_FREEROAM           # fires when...
    on: scene_id == 1              # ...the scene machine enters scene 1
  - name: PLAYER_AT_REST
    on: state 0x056daae8[..] stable # player sprite-state ring stops moving
timeline:
  - freeze_count_until: HOUSE_FREEROAM   # don't count frames during the load
capture:
  - at: PLAYER_AT_REST
    offset: 0                            # both sides capture the SAME moment
    force: standing_pose                 # apply the canonical-state forcing
```

The same anchor spec drives **both** targets. Each side emits an anchor
event stream `{anchor: name, frame: N}`; the harness then captures/diffs
**by anchor**, so port-frame-3300 and retail-frame-6000 are recognised as
"the same instant" because both are `HOUSE_FREEROAM + k`.

This directly encodes the user's ask: *"stop counting frames for a loading
screen, start counting again when this function fires or this state
changes."* `freeze_count_until` = stop counting; `on: function/state` =
the resync event.

## What already exists to build on (don't reinvent)

The Frida agent already has the *ad-hoc* version of this — generalise it:

- **`auto_3d_trace` mode** (`tools/frida/openrecet-agent.js`): hooks
  `IDirect3DDevice8::DrawIndexedPrimitive` (vtable[71]), **anchors on the
  first 3D draw** (`g_auto_3d_seen_frame`), and captures the window
  `[anchor, anchor + g_auto_3d_trace_frames]`. This *is* an
  anchor-relative capture — just hardcoded to one anchor type.
- **`dump_b` / `quad_hist` modes**: anchor on `count_b>0`
  (`g_dump_b_anchor_frame`) and capture configured offsets. Same pattern,
  different trigger.
- **`var_frame_counter` (0x073dfcfc)**: the shared frame clock both sides
  already read.
- **Port `CALL_TRACE_ENTER(VA)` probes** (`src/call_trace.{c,h}`):
  annotation-driven VA hooks — the port-side equivalent of a Frida
  function hook. These become the port's `on: function` anchor source.
- **`tools/pixel_diff.py`** (landed): the `[A | B | white-diff]` renderer
  the whole framework feeds.
- **`scenario.yaml` + `scenario-test.py`**: the capture/golden harness to
  extend with an `anchors:`/`capture:` section.

## Architecture

### 1. Anchor sources (both targets emit a common event stream)

| anchor `on:` | port implementation | retail (Frida) implementation |
|---|---|---|
| `function VA` | `CALL_TRACE_ENTER(VA)` already present, or a lightweight `ANCHOR(VA)` probe | `Interceptor.attach(rva(VA))` |
| `state ADDR op VAL` | read global each tick in an `anchor_tick()` | `MemoryAccessMonitor` / per-Present poll |
| `scene_id == N` | the scene-machine id global | same global via Frida |
| `3d_draw` | first mesh DrawIndexedPrimitive of the frame | vtable[71] hook (exists) |
| `state_stable ADDR` | value unchanged for K frames | same |

Common wire format (reuse the trace JSONL style):
`{"anchor": "HOUSE_FREEROAM", "frame": 3300}`. Port writes it from a new
`src/anchor_trace.c`; the Frida agent emits the same `kind: "anchor"`
message. Both keyed off `var_frame_counter`.

### 2. Anchor-relative capture + the alignment map

The harness reads both anchor streams, builds `anchor → frame` per side,
and resolves each `capture: {at: ANCHOR, offset: k}` to the concrete
per-side frame. `freeze_count_until` just means "ignore frames before this
anchor when interpreting offsets" — i.e. offset 0 == the anchor frame.

Output: matched frame pairs `(port_frame, retail_frame)` that are
*semantically* the same instant → fed to `pixel_diff.py`.

### 3. Canonical-state forcing (for bit-exact clean diffs)

Anchor alignment gets us the same *moment*; bit-exact diffs of a moving
scene also need the same *pose + camera*. Apply a named **force** profile
symmetrically:

- **Player/camera pose**: port already seeds `g_scene1_player_pos` +
  the actor model (`scene1_postload_pose_house_standing`, pos
  -0.30/0/9.35). Retail: Frida-write the same globals
  (`DAT_056daae8` ring, `DAT_056da1cc/1d8`, camera state) at the anchor.
- **Sim freeze**: pause the tick on both sides at the anchor (port: the
  existing `g_paused`; retail: the tick-freeze noted in
  `e4-per-call-io-capture.md` Tier 2 / `CREATE_SUSPENDED` trick) so the
  captured frame is reproducible.
- **Overlay suppression / parity**: the contaminating overlays are
  *un-ported* (intro dialog, 2D HUD). Two options: (a) on the **retail**
  side, dismiss the dialog / hide the HUD layers via Frida so retail
  matches the port's bare scene; or (b) drive both to a free-roam state
  where neither shows. Prefer a free-roam anchor (no intro dialog) for
  render-parity work; the 2D HUD is a porting target (C7i) anyway.

### 4. Diff layer

`pixel_diff.py` already emits `[retail | us | amplified white-diff]` and
prints differing-px + mean-abs. Add a thin `tas_diff.py` that: drives both
targets per the anchor spec → resolves matched frame pairs → calls
`pixel_diff.py` per configured crop → writes an index.html gallery (like
`regen-comparisons`). One command, repeatable.

## Autonomous incremental trace synthesis (the long game)

The endgame is not just *replaying* hand-made traces — it's tooling that
lets the agent **autonomously discover** an input trace, growing it in
baby steps and verifying game state at every step, until it can drive a
**full run of the whole game**. The anchors + state-probing above are the
substrate; this is the closed loop built on top.

**Shape of the loop:**

1. **State oracle** — a readable snapshot of *every probeable global* at
   any frame (player pos/facing, scene id, menu cursor, inventory/gold,
   dialog state, RNG state, day/phase, …). "Once we understand all the
   global state we can probe" = building + cataloguing this oracle is the
   gating prerequisite. It subsumes the anchor `on: state` sources and the
   existing `mem_watch`/call-trace probes. Each probe is a named field with
   an address + type + meaning.
2. **Waypoint library** — named target states expressed against the oracle
   (`AT_TITLE`, `NEW_GAME_CONFIRMED`, `HOUSE_FREEROAM`, `FIRST_CUSTOMER`,
   `ITEM_SOLD`, …). A full-game TAS = an ordered chain of waypoints.
3. **Baby-step synthesis** — to advance from waypoint *k* to *k+1*: propose
   a short input segment, replay it from the *k* checkpoint, read the oracle,
   and **assert the resulting state is what we expect**. Accept on match;
   revise/backtrack on mismatch. Small segments + a checked waypoint each =
   "check every step of the way that the game state is what you expect."
   The agent drives this search (it knows the menu/▸/input semantics);
   the tooling makes propose→replay→verify→accept a tight, scriptable cycle.
4. **Checkpointing** — snapshot/restore engine state at each accepted
   waypoint so iteration resumes from the last good point instead of
   replaying from boot every time. Without this a full run is unsearchable
   (replaying tens of thousands of frames per probe). Likely a save-state
   (serialize the relevant global arena) or a fast-forward-to-anchor.
5. **Regression value** — once a waypoint chain exists, it doubles as the
   parity test bed: replay the synthesised trace on *both* targets (design
   principle) and any anchor/state divergence is a port bug. The synthesised
   full-game trace becomes the ultimate behavioural acceptance test.

**Scaling notes.** Start with the simplest cases (title → new game → HOUSE)
where waypoints are few and the input is short. The architecture must scale:
the oracle grows as we RE more subsystems; the waypoint chain grows linearly
with game progress; checkpointing keeps per-step cost bounded; the trace
format (sparse `{frame,buttons}`) handles arbitrary length now that the
`INPUT_TRACE_MAX_ENTRIES` cap is lifted (P0 ✅). Determinism pinning (P1) is
non-negotiable here — synthesis only converges if replay is reproducible.

## Phased implementation

- **P0 — papercut fixes (cheap, do first; unblock the current workflow):**
  1. ✅ **DONE (2026-05-30).** `INPUT_TRACE_MAX_ENTRIES` (was 4096) silently
     failed the whole load when a trace was longer (the 8256-entry trace →
     "replay disabled"). Fixed by making the replay table heap-grown
     (`struct input_trace.entries` is now a doubling `realloc` buffer, freed
     via `input_trace_free`) and slurping the whole file dynamically — both
     fixed caps removed (the entry array AND the old 1 MiB read buffer). The
     constant is now only a 16 M-entry *sanity ceiling* that fails **loudly**
     (`stderr`), far past a full-game run. Regression test
     `input_trace_parse_grows_past_old_fixed_cap` (9000 entries).
  2. ✅ **DONE (2026-05-30).** `tools/run-openrecet.sh` now translates
     `--input-trace-replay` / `--input-trace-record` paths via `wslpath -w`
     (resolving repo-relative against the root, like `--capture-to`); replay
     paths warn if the file is missing, record paths `mkdir -p` their parent.
     Verified end-to-end: a repo-relative replay of the 9000-entry trace
     loads (`replaying ← \\wsl.localhost\…\big.jsonl (9000 entries)`).
  3. Document the working capture recipe (turbo + silent-audio + trace
     replay + capture-frames) in one place so it's not re-derived.
- **P1 — determinism pinning + anchor emission (do together):**
  - **Pin the non-input variation** so the same trace is reproducible on
    each side and comparable across them: RNG seed (`DAT_006023a0` — the
    port has `--rng-seed`; force the same on retail via Frida), virtual
    clock/QPC (port `--turbo` = fixed 17ms/frame; mirror on retail), and
    frame-step. Acceptance: replaying one trace twice on the *same* target
    is bit-identical frame-for-frame.
  - **Anchor emission:** `src/anchor_trace.c` (port) + agent `kind:
    "anchor"` (retail), both reading a shared anchor spec. Wire formats
    identical. Validate: both emit the same anchor *names*; record the
    per-side frames and the gap (the gap is the divergence metric P1
    exists to surface — see the design principle).
- **P2 — anchor-relative capture + alignment map:** extend `scenario.yaml`
  with `anchors:`/`capture:`; `scenario-test.py` + `frida_capture.py`
  resolve anchor+offset → frame. Re-run this session's case: capture port
  & retail at `HOUSE_FREEROAM + k` and confirm the room aligns.
- **P3 — canonical-state forcing + `tas_diff.py`:** named force profiles
  (pose/camera/freeze/overlay) applied symmetrically; one-command diff
  gallery. **Acceptance test: reproduce the deferred clean book + back-
  blind diffs vs retail from the mip-fix session, bit-exact where aligned.**
- **P4 — autonomous incremental trace synthesis (the long game):** the
  state oracle (catalogue probeable globals) → waypoint library →
  checkpointing → the propose/replay/verify/accept loop. First milestone:
  autonomously synthesise + verify the title→new-game→HOUSE-freeroam trace
  in baby steps. Scale target: a full-game TAS run. Depends on P0 (trace
  cap), P1 (determinism + oracle/anchor emission), and benefits from P3
  (forcing/freeze for stable probing). See the dedicated section above.

## Open questions

- **Scene-id global**: confirm the address + enumerate the scene ids
  (HOUSE = 1?). Needed for the most useful anchor (`scene_id == N`).
- **Camera determinism in free-roam**: is the HOUSE camera a pure function
  of player position, or does it carry velocity/lag state? If pure,
  forcing the pose is enough; if not, the camera state globals must be
  forced too.
- **Retail overlay suppression**: cheapest Frida hook to dismiss the intro
  dialog / hide HUD layers (vs. just choosing a free-roam anchor).
- **Frida-server stability**: captures degrade after ~5–10 spawns
  ([[feedback_frida_server_leak]]); the harness should detect + restart
  frida-server between drives.

## Relationship to existing roadmap

This is the verification-tooling backbone for Phase E render-parity work
(`harness-roadmap.md` §E) — it generalises the per-mode anchors already in
the Frida agent and the `CALL_TRACE_ENTER` probes into one declarative,
dual-target system. It does not change the porting order; it makes every
render-parity *check* cheap and repeatable instead of a manual session.
