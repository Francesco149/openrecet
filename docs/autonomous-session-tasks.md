# Autonomous-session task queue — picked 2026-05-22

> Picked for an unattended session. Read in order. Each task has
> acceptance criteria the assistant can verify itself (no user
> screenshot / audio judgement needed). Stop where you stop; don't try
> to cram every item.
>
> Standing rules during autonomy:
> - Commit in logical units as you go (see `AGENT-WORKFLOW.md`).
> - Co-author trailer: `Claude Opus 4.7 (1M context) <noreply@anthropic.com>`.
> - Spawn Sonnet subagents for mechanical work; brief them with exact
>   signatures (see worktree-isolation note in `AGENT-WORKFLOW.md`).
> - Don't push / amend / skip hooks.
> - If a task gets stuck after one debug attempt, **leave a
>   `BLOCKED:` line in `PROGRESS.md` and move on** — do not flail.
> - Run `make -C tests run` before each commit that touches a decoder
>   or pure-C math.

## Decisions already made (do not relitigate)

- **Trace format:** Phase A's `trace.jsonl` reused as-is — same
  `{frame: int, buttons: "0xNNNN"}` sparse schema. Phase B injects
  the same masks into retail.
- **Inject point:** Hook `FUN_0047b73c` (input_poll) at LEAVE. The
  engine's natural DInput poll runs first; we then OVERWRITE
  `DAT_073dddd0` with the trace value for the current frame. This
  way every observer downstream of input_poll (sim_a, button-state
  ring, scene transitions) sees the forced mask, AND the `input_state`
  hook below still emits the truth (what was actually used).
- **Frame source:** `DAT_073dfcfc` (`var_frame_counter` in
  `openrecet-agent.js::ADDR`). Increments on every Present. The
  current `input_state` hook already reads it.
- **Sparse → dense:** JS-side. Pass the sparse trace as-is to
  agent.init; agent keeps a "last-seen mask" plus the next-entry
  pointer. O(1) per frame after init.
- **Out of scope (defer, document as `BLOCKED:` if asked for):**
  RNG pinning (`DAT_006023a0`), clock pinning (QPC override),
  joystick/mouse injection, paused-state forcing.

## Task list (execute in order)

### 1. Input injection into Phase B harness (~2-3 hr) ✅ landed 2026-05-22

**Status:** Implemented end-to-end. See PROGRESS.md "Phase B input
injection" entry. The spec below is preserved as the design record.

**Status from previous session:** Spec discussed and agreed; not yet
implemented. The detailed plan and reasoning lives in this file.

**Goal:** `tools/scenario-test.py --target retail <name>` replays the
same `trace.jsonl` that Phase A reads, so retail walks the same input
sequence as openrecet. Unblocks every future retail golden capture
(currently retail captures only idle scenes because there's no
human at the keyboard).

**Engine background (already RE'd, see
`docs/findings/winmain-and-bootstrap.md` §"Input poll"):**

- `FUN_0047b73c` (VA `0x0047b73c`) is `input_poll`. Runs once per
  frame. DInput keyboard + 2 joysticks → aggregated into
  `DAT_073dddd0` (u16, player-0 button mask). Already hooked at
  LEAVE in `openrecet-agent.js::installInputHook` to emit `input_state`.
- Button bits: UP=0x04, RIGHT=0x01, DOWN=0x08, LEFT=0x02,
  A=0x10, B=0x20, C=0x40, D=0x80, E=0x100, skill0..4=0x200..0x2000.
- Frame counter `DAT_073dfcfc` (`ADDR.var_frame_counter`) — u32,
  bumped on every Present.

**Implementation plan:**

1. **Agent side** (`tools/frida/openrecet-agent.js`):
   - Add module globals:
     ```js
     let g_input_trace = [];      // [{frame: N, mask: M}, ...] sorted ascending
     let g_input_trace_i = 0;     // next-entry cursor (advances as frames pass)
     let g_input_force_active = false;
     let g_input_last_forced = 0; // last mask we applied (sticky between sparse entries)
     ```
   - Extend `init(config)` to accept `input_trace` (array of
     `{frame: int, mask: int}`) and `force_input` (bool).
     Sort by frame on receive. Set `g_input_force_active`.
   - Modify `installInputHook`'s `onLeave`:
     - Read current frame from `var_frame_counter`.
     - Walk `g_input_trace` from `g_input_trace_i` forward while
       `entry.frame <= current_frame` — that's the sticky-mask
       update. (Don't walk the whole array each frame; advance the
       cursor monotonically.)
     - If `g_input_force_active`, write `g_input_last_forced` to
       `var_input_mask` (which is `DAT_073dddd0`). Use `writeU16`
       since the mask is u16.
     - Emit `input_state` as it does today — but now reflecting the
       FORCED value (since we just wrote it). The driver consumes
       this for `trace.jsonl` recording, which now becomes a
       "playback verification" rather than a real polling record.

2. **Driver side** (`tools/frida_capture.py`):
   - Extend `CaptureConfig` (around line 165) with:
     ```python
     input_trace_path: Path | None = None
     force_input: bool = False
     ```
   - In `_run_capture_impl`, before `script.exports_sync.init(...)`,
     load the trace if path is set:
     ```python
     trace_entries = []
     if cfg.input_trace_path and cfg.input_trace_path.exists():
         for line in cfg.input_trace_path.read_text().splitlines():
             if not line.strip(): continue
             rec = json.loads(line)
             trace_entries.append({
                 "frame": int(rec["frame"]),
                 "mask":  int(rec["buttons"], 16),
             })
         trace_entries.sort(key=lambda r: r["frame"])
     ```
   - Pass to agent:
     ```python
     script.exports_sync.init({
         "capture_frames": list(cfg.capture_frames),
         "max_frames":     cfg.max_frames,
         "input_trace":    trace_entries,
         "force_input":    cfg.force_input,
     })
     ```
   - Extend `run_capture(...)` kwargs to accept `input_trace_path`
     and `force_input`. Default both off so existing callers don't
     change behavior.

3. **Plumbing side** (`tools/scenario-test.py`):
   - `run_scenario_capture_retail` (around line 151):
     - Build `trace_path = scen.path / "trace.jsonl"` (use the
       `_ensure_trace_exists(scen)` helper that already exists).
     - Pass `input_trace_path=trace_path, force_input=True` to
       `frida_capture.run_capture`.
   - Update the docstring — remove "No input replay yet".

4. **Re-bless retail goldens for active scenarios:**
   - `./tools/scenario-test.py boot-idle --target retail --bless
     --frida-remote cutestation.soy:27042` — should be a no-op since
     boot-idle has no input. Verify no regressions.
   - `./tools/scenario-test.py title-z-press --target retail --bless
     --frida-remote cutestation.soy:27042` — Z is pressed at frame 30
     in the trace; if the engine sees the Z press, the "Start a new
     game" tooltip should appear in the frame 44/50 captures. Visually
     inspect.

**Acceptance:**

- `boot-idle` retail capture still produces the unchanged 3 BMPs.
- `title-z-press` retail capture shows the tooltip text on frames
  44/50 (matching Phase A's openrecet capture for the same scenario).
- `tests/scenarios/title-z-press/golden-retail/trace.jsonl` records
  the FORCED mask each frame (sparse format), matching the input
  scenario's `trace.jsonl` post-frame-30.
- `nix develop --command make -C tests run` still passes (no
  decoder/math files touched, but cheap to verify).

**Risk / gotchas:**

- **Hook ordering**: there's a button-state ring update at the top of
  `FUN_004536cb` (sim_a) that reads `DAT_073dddd0`. Our injection
  must happen on input_poll's LEAVE *before* sim_a's read. The
  game's per-frame ordering is `input_poll → sim_a → sim_b → render`
  (see `src/tick.c`). Frida's `onLeave` for `input_poll` fires
  between the function return and the caller's next instruction —
  before sim_a runs. ✓
- **`writeU16` on a u32 global**: `DAT_073dddd0` is documented as
  u16 (`ADDR.var_input_mask`). Writing u16 should leave the upper
  16 bits untouched. If a downstream reader uses u32 — verify they
  mask `& 0xffff` (the engine bit layout maxes out at 0x2000).
- **`force_input: false` default**: existing capture flows that
  don't pass a trace must continue to record real polls.
- **Pre-resume timing**: the agent's `init()` runs while the
  process is paused. The trace is set up before `device.resume(pid)`,
  so frame 0 sees the right mask. ✓

**Files touched (estimate ~250 LOC):**

- `tools/frida/openrecet-agent.js` (+~60)
- `tools/frida_capture.py` (+~50)
- `tools/scenario-test.py` (+~10)
- `docs/findings/winmain-and-bootstrap.md` (1 paragraph documenting
  the injection point)
- `docs/harness-roadmap.md` (mark Phase B input-injection ✅)

**Commit logical units:**

1. Agent-side trace plumbing + hook modification.
2. Driver-side trace load + RPC wiring.
3. Scenario-test plumbing + docstring update.
4. Re-blessed `golden-retail/` for `title-z-press` (gitignored;
   commit the side-by-side regen-comparisons output as a contact
   sheet IF the user wants visual archive).
5. Docs update.

### 2. Font draw_text dst-rect fix (~30 min)

**Status from previous session:** Hypothesis confirmed empirically
via `tools/diagnostics/font/font_drawrect_probe.py`. Implementation
written out verbatim in `docs/font-fix-pending.md` — read that file
end-to-end, then apply the patch to `src/font_draw.c` (and the same
math change to `font_draw_text_centered`'s measure walk — see file).

**Acceptance:**

- Build + run boots without regression.
- Title settings panel ("Music"/"Sound"/"Voice"/etc. row) visually
  matches retail's row height + horizontal extent. Compare against
  the user-supplied `font-issue2.png` right-half.
- `make -C tests run` still passes.

**Files:** `src/font_draw.c` (the dst-rect block around line 128),
maybe `src/font_draw_text_centered` if width calc changes.

### 3. (Optional, only if #1 + #2 land cleanly)
Frida-probe scene-state forcing — a small RPC that lets the harness
write to the scene-state global to skip the title menu and drop
retail straight into settings (or shop, or dungeon, …) without input
replay. Spec: identify the scene-state offset (`FUN_004547ab` dispatch
function — check `docs/findings/winmain-and-bootstrap.md`), expose a
`forceScene(state_id)` RPC in the agent, document the known state IDs.

**Acceptance:** From `scenario-test.py`, can launch retail and have
it boot directly to the settings panel with no input frames.
