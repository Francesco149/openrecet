# Live-probe harness — driving retail Recettear live via MCP

Terse. The **interactive** counterpart to the deterministic trace pipeline:
one persistent retail process you drive turn-by-turn (inputs, memory,
engine-thread calls, screenshots) instead of a fixed TAS replay. Use it to
**explore ahead** of a trace — find what the game does, discover anchors/state,
confirm a poke/call reproduces an input's code path — then bake the settled
finding into a `tests/scenarios/` trace for bit-exact parity work.

Mirror of `../OpenLords2` probe layer, Recettear-specific (button mask input,
no mouse; ESC = keyboard path).

## Pieces
- `tools/frida/openrecet-agent.js` — the Frida agent. Live-probe RPC surface
  added alongside the capture path (search `live-probe layer`). Same engine, one
  injected agent; `init({probe_mode:true, ...})` arms the probe niceties.
- `tools/probe_daemon.py` — spawns retail ONCE, holds the frida session, serves
  line-JSON commands on `127.0.0.1:<port>` (published to `runs/probe/daemon.json`).
  Save-sandboxed (never touches the real save). Turbo default ON.
- `tools/probe.py` — thin CLI client (one command → daemon → reply). For humans.
- `tools/openrecet_mcp.py` — stdio MCP server over the daemon. For the agent.
  Registered as the `openrecet` MCP in `.mcp.json`.

## Two ways in
1. **MCP (agent)** — the `openrecet` MCP tools. `launch` first (spawns the
   daemon detached; survives across calls), then `screenshot` / `game_state` /
   `press` / `hold` / `walk` / `esc` / `wait` / `poke_memory` / `call_function`
   / `anchors` / `set_interactive` / `set_turbo` / `quit`. `screenshot` inlines
   the PNG so the model SEES the screen.
2. **CLI (human / scripts)** — start the daemon by hand, drive with `tools/probe.py`:
   ```
   nix develop --command python3 tools/probe_daemon.py --view --rng-seed 19937 &
   tools/probe.py state ; tools/probe.py shot ; tools/probe.py tap up ; tools/probe.py tap a
   ```

## Preview window (user-visible, no focus steal, input-locked)
- `--view` / `launch{view:true}` → the agent substitutes the engine's first
  `ShowWindow` with **SW_SHOWNOACTIVATE** (shown, never activated) instead of
  SW_HIDE. User watches; focus never leaves their foreground app.
- **Human input LOCKED by default**: while `probe_active` the probe queue OWNS
  `var_input_mask` (real keyboard/pad is overwritten post-poll, same write-path
  the DInput poll takes). Toggle: MCP `set_interactive{enabled:true}` (hand to
  human) / `false` (re-lock). CLI `tools/probe.py input on|off`.
- `force_active` re-asserts the tick gate (`DAT_073dfca0=1`) every poll so a
  user click-away can't park the engine in WaitMessage.

## Input (button mask — the faithful path)
Injection writes the decoded button mask to `DAT_073dddd0` at the input-poll
onLeave — the **same global the engine's own DInput decode writes**, so a probe
tap and a real player press hit the identical downstream code. Masks
(`src/input.c input_binding_mask`): up 0x04 right 0x01 down 0x08 left 0x02,
a 0x10 (confirm/talk/pick) b 0x20 (cancel) c 0x40 d 0x80 e 0x100, s0..s4 0x200..0x2000.
Name form: `'up+a'`, `'0x14'`. ESC is NOT in the mask → `esc` synthesizes a real
WndProc `WM_KEYDOWN VK_ESCAPE` (skip/pause path).
- `press` = tap (held N polls, released M, ×repeat). Frame-exact via a queue
  (one queue step per input poll).
- `hold` = sticky mask OR'd under the queue (walk + tap compose). `walk` = timed hold.

## Navigation + cheats (fast driving)
Walking the character across a room via inputs is slow; these skip it.
- `where` → player world pos (x,z). Axes: left/right = -/+X (DAT_056da1d8),
  up/down = -/+Z (DAT_056da1e0), py≈0.
- `move_to{x,z|name}` — greedy walk toward a target with adaptive step +
  collider wiggling; stops at the closest reachable point. `waypoint set <name>`
  records the current spot (build a map: counter, stands); `move_to{name}` recalls.
- **CHEATS** (direct pokes — instant, for the driving agent):
  - `teleport{x,z|name}` — poke the actor position (0x056da1d8/e0). Instant,
    bypasses colliders + walk time; the physics still CLAMPS to the playable
    bounds next tick (out-of-room targets snap to the nearest valid spot).
  - `set_facing{dir}` — force facing (poke DAT_056db05c world angle). Compass
    name (up/down/left/right/diagonals) or radians. idle +pi/2 = down/toward camera.
  - `set_gold{n}` — set pix (working bank 0x044e37a4).
  - (Dungeon combat cheats — godmode/instakill — TODO once we reach a dungeon and
    map the HP/enemy VAs; the LLM can't fight in real time.)
- Handing to the human: `set_interactive{enabled:true}` unlocks input AND drops
  turbo to 1× (playable); re-locking restores turbo. `set_turbo` toggles alone.

## Memory + engine calls
- `read_memory` / `poke_memory` — typed (u8/i16/i32/f32/f64/ptr) at a **Ghidra
  VA** (agent translates to load base; ImageBase 0x400000). `read_state` = the
  curated snapshot (scene/rng/cc08/player/gold/day/…, see `STATE_SPECS` in the daemon).
- `call_function` — **engine-thread** call, queued to run at the pre-sim
  input-poll point (never races the sim). va + args + argt (frida types) + ret + abi.
  **GOTCHA: `argt` is REQUIRED** (e.g. `argt:["int"]` for a 1-int fn) — omitting it
  builds a 0-arg NativeFunction and the call fails with `err:"not a function"`. Pointer
  args: pass the VA as an int with `argt:["int"]` (the callee casts) or `["pointer"]`.
  NB Frida reads/pokes the STATIC-image VA via base-fixup; a `DAT_` that's really a
  heap/malloc'd buffer (VA far above the image, e.g. the item catalog `DAT_095d3804`)
  is NOT at its Ghidra VA at runtime — call the in-process accessor instead of reading
  the VA raw. RNG-seed VA to pin across A/B trials = `DAT_006023a0` (MSVC LCG).
  **Prefer a call/poke over an input ONCE mapped + confirmed it reproduces the
  input's code path** (user directive) — read the decompile (`docs/decompiled/all.c`), find the
  handler an input dispatches to, call it directly, diff state vs the input path.

## Anchors → deterministic traces (probe-ahead RE)
The agent's anchor poll fires on the same semantic edges the TAS waits on
(LOADING_END, CONV_POSE_START/END/BLINK, CUSTOMER_SERVICE_ENTER/EXIT,
TEXT_ANIM_START/END, PAUSE_OPEN/CLOSE, …) with frame + rng. `anchors` drains
them. Use a live run to **discover the anchor sequence + rng at each** for a new
scenario, then hand-author / record the `tests/scenarios/` trace and its
`{rngseed}`/`{phasepin}`/load pins from that ground truth — the live harness is
the scout, the trace is the deterministic foundation (per the ★ trace-is-foundation
directive).

## Turbo / determinism notes
- Probe mode installs a **runtime-toggleable** clock stub (turbo on/off live via
  `set_turbo`), with monotonic continuity across toggles. Turbo = the same fixed
  17ms/tick virtual clock the capture path uses. Bootstrapping via `--segtrace`
  reuses the full TAS pin machinery ({rngseed}, load pins) → the probe starts
  from a pinned known state.
- `launch{segtrace:...}` boots inactive, replays the segtrace to a known state,
  then flips `probe_active` on (poll `game_status.segtrace.done`).

## Static RE (decompile companion)
⚠️ Ghidra MCP decommissioned 2026-07-10 (RAM runaways OOM'd the box); grep the static
export `docs/decompiled/all.c` or open the `.gpr` in the Ghidra GUI — do NOT rebuild the
daemon. Static RE says WHAT a function does + WHO calls it; the live harness confirms it
(call it, watch the state/pixels move). For computed struct accesses / xrefs that grep
misses, open the analyzed `ghidra/projects/openrecet.rep` in the GUI.

## Gotchas
- MCP server MUST run under the GC-pinned raw python
  (`~/.local/state/openrecet/mcp-python`), NOT `nix develop -c python3` (devshell
  banner corrupts the JSON-RPC stream). The daemon it launches DOES use `nix
  develop` (needs frida/PIL).
- ONE retail at a time (singleton mutex). **HARD RULE (user 2026-07-10): every
  kill must target OUR probe specifically — parallel projects run their own
  probes on this host (e.g. an OpenLords2 daemon was live during the news
  gate).** The daemon's start-reap kills ONLY the pid recorded in our
  `runs/probe/daemon.json` (foreign same-named pids get a warning, never a
  kill); `tools/kill_retail.py` likewise defaults to our daemon.json pid —
  `--pid N` for an explicit target, `--all` (dangerous) for the old name-sweep.
- **callq goldens: use the atomic seed WINDOW.** The engine-thread call queue
  returns `seed_at_call` AND `seed_after_call` (DAT_006023a0 read immediately
  before/after the call on the engine thread).  NEVER read the "final seed"
  via a separate RPC — it races the resumed sim and over-counts draws (the
  news-gate round-1 lesson; roster's seed_at_call was the entry-side half).
- Frida host = `cutestation.soy:27042`, auto-spawned (never gate on it).
- Long idle sessions: `input_state` is deduped to change-points; screenshots
  bypass load-suppression on purpose.
