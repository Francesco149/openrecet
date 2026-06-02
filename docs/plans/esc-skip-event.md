# Plan — context-sensitive ESC + the skip-event prompt

> Durable copy of the working plan (was `~/.claude/plans/hidden-wiggling-snail.md`).
> Companion RE doc: `docs/findings/esc-skip-event.md` (the authoritative,
> continuously-updated subsystem map). Read that first.

## ✅ PHASE B/C LANDED (2026-06-02 PM) — choice-box port

The reframe below is DONE. The skip prompt is now the engine choice box
(`src/choice_box.{c,h}`: FUN_00434def/ed2/dbf + FUN_0043537e/435747 render),
with `src/skip_event.c` as the FUN_0046c2cb gate + FUN_0046c320 poll glue, the
render hooked into `scene1_dialogue_draw` (FUN_0046c090 tail), and
`g_skip_event_enabled=1`. 10 choice-box host tests + reworked skip_event tests,
suite green, both exes build. See `docs/findings/esc-skip-event.md` "✅ Phase
B/C LANDED" for the full mapping. **Only remaining: a port-side render golden
(human-verify deferred) — capture via `OPENRECET_FORCE_SKIP_AT` (no ESC-inject
path in the port harness).** Everything below is the historical run-up.

## STATUS (2026-06-02) — read this before resuming

- **Phase A: DONE & committed** (`b4ccaed`). `src/esc_dispatch.{c,h}` +
  `main.c` WM_KEYDOWN. ESC routes title→quit, in-game/dialogue→swallow. Unit
  tested (`test_esc_dispatch.c`), host suite green. The reported wrong-quit bug
  is fixed.
- **Phase B: DONE** (this session). `src/skip_event.{c,h}` — prompt state
  machine + `scene1_intro_dialogue_skip_to_end()` (force prologue → free-roam).
  Arm wired into `esc_pressed()`, modal tick into `sim.c`. 12 host tests, suite
  green, both exes build. **Gated off live via `g_skip_event_enabled=0`** (no
  soft-lock from an unrendered prompt); Phase C flips it on. The interactive
  Yes/No input is observable-behavior (PORT-DEBT — the engine choreography is
  not statically legible; `DAT_06a499c8` cancel counter is never set positive in
  the corpus). See `docs/findings/esc-skip-event.md` "Port status".
- **⭐ GOLDEN CAPTURED + subsystem corrected (2026-06-02).** The retail prompt is
  `runs/skip-golden/arm485/frame_00514.png` — the gold "Do you want to skip this
  event?" Yes/No over the HOUSE. **The whole `FUN_00453384`/`DAT_06a49998`/
  `FUN_00454191` model was the PAUSE menu** (radial blur — user-confirmed). The
  REAL skip is the engine CHOICE BOX: `FUN_0046c2cb → FUN_00434def(...)`, gated on
  `DAT_073a3e18` (skip_prompt, already ported as `ive_scene_state.skip_prompt`,
  bumped every dialogue frame by `FUN_0046c320`), polled by `FUN_00434ed2`,
  selection `DAT_0438ac24`. ESC works any time ≥2 frames into a line (skip_prompt
  > 1) — no `b1c8==0` needed; `b1c8==1` is correct for the dialogue. See findings
  "MAJOR CORRECTION". (RE method lesson: should've read the screenshot first, not
  the wrong globals.)
- **NEXT = Phase B/C reframe (no longer blocked):**
  - Port the choice-box subsystem: `FUN_00434def` (open + text layout into
    `&DAT_0438af3b`, `DAT_0438af34=1`, `DAT_0438ac08`=2, `DAT_0438ac24`=sel) +
    `FUN_00434ed2` (poll/commit) + the `FUN_0046c2cb` gate (`skip_prompt>1 &&
    DAT_073a3dec==0 && DAT_073a6db0==0`). Wire the WndProc ESC → that, off the
    already-ported `skip_prompt`.
  - Re-point `src/skip_event.c` at the choice-box globals (observable Yes/No
    logic stays; render is the choice box, NOT `FUN_00454191`), then set
    `g_skip_event_enabled=1` and verify the port's prompt vs the f514 golden.

  **Reproduce the golden** (runs/ is gitignored — re-capture in one command;
  build the boot→line0→idle segtrace then drive retail at normal speed, faithful
  ESC at the settled line, no force/turbo):
  ```
  sed -n '1,216p' tests/scenarios/intro-dialogue-lines/trace.jsonl > runs/skip-golden/trace.jsonl
  printf '%s\n' '{"capture":0}' '{"frame":0,"buttons":"0x0000"}' \
    '{"frame":400,"buttons":"0x0000"}' '{"capture":30}' '{"capture":60}' \
    '{"capture":90}' '{"capture":150}' '{"capture":240}' >> runs/skip-golden/trace.jsonl
  nix develop --command python3 tools/frida_capture.py --run-dir runs/skip-golden/g \
    --input-segtrace runs/skip-golden/trace.jsonl --silent-audio --hide-window \
    --force-resolution 1024x768 --duration-ms 70000 --max-frames 4000 \
    --arm-skip-at-frame 485
  # → runs/skip-golden/g/frames/frame_00514.png = the prompt over the HOUSE.
  ```
- **Skip-arm trigger CRACKED.** The skip arms via `FUN_0045337b → FUN_00453384(0)`
  **iff `DAT_0438b1c8 == 0`** (dialogue sub-state) + the `cVar4`/`DAT_0438bf7c==0`/
  `DAT_0438be98==0` terms. Confirmed an arm by spamming the skip from frame ~50
  (`b1c8==0`): prompt opened (`s98:1→2→3`, `sa0=1`), confirmed (`DAT_0438b1c0=9`).
  `esc-disabled` (`DAT_06a49954`) is 0 during the cutscene (not a blocker).
- **Corrections to the original plan below (do not trust those bits):**
  - The "button `0x100` arms the skip via DInput, so capturable via segtrace"
    claim is **WRONG** — `0x100` injection never armed. The skip is keyboard-ESC
    (WndProc) — repro with a real `WM_KEYDOWN VK_ESCAPE` (probe:
    `frida_capture.py --arm-skip-at-frame N`, now drives WndProc dispatch).
  - The real dialogue lines are at **~frame 456+** (long bedroom intro f72→456),
    NOT f72-250. Earlier skip probes fired on the intro (no dialogue box) and on
    a `b1c8=1`-stuck forced playthrough — that's why they didn't arm.
  - Render: the skip prompt is the gold **"Do you want to skip this event?"**
    banner + Yes/No over a *lightly darkened* HOUSE (user screenshot at
    `/mnt/c/Users/headpats/Documents/skip.png`, mirrored to the feed). The heavy
    radial-blur RTT is the *transition*, shared with the pause menu.
- **Next concrete step:** reproduce `DAT_0438b1c8 == 0` on a *real dialogue line*
  (faithful A-press pacing so the dialogue cycles, or a user-recorded trace),
  screenshot-verify the line + watch `b1c8==0`, inject ESC, and capture the
  prompt-over-HOUSE golden + the `FUN_00453384 → FUN_00454191` call graph. Then
  implement Phase B (state machine, arm-when-`b1c8==0`) + Phase C (banner render).
- **Methodology (user-directed, now in memory):** probe via synthetic input
  traces + screenshot-verify the state + read the call graph. Direct VA calls are
  only for pure-fn unit tests / state-forcing hacks — NOT faithful behavior repro.

## Engine reference

WndProc `FUN_0047b2e7` ESC arm (`by-address/47b2e7.c:96-114`):
```
if FUN_00452911() != 0:  return                 // (A) DAT_06a49954 esc-disabled
if DAT_0438b1c0 != 0:  FUN_0045337b(); return    // (B) in-game → skip-event handler
if FUN_0049a585() == 0:  return                  // (C) DAT_09643520|544 → swallow
PostMessage(WM_CLOSE)                            // (D) title → quit confirm
```
- `FUN_0045337b` → `FUN_00453384(0)` — skip-event state machine (821 B). Arm gate:
  `b1c8==0` AND `cVar4==1` AND `DAT_0438bf7c==0` AND `DAT_06a49990==0` AND
  `DAT_0438be98==0`. Arms by `DAT_06a49998=1, DAT_06a4999c=1, DAT_06a499a0=1`.
- `FUN_004532df` (129 B) — per-frame prompt counter anim (`s98` climbs, caps 0xc).
- `FUN_004536cb` (1745 B) — in-game master tick; `s98==3 → DAT_0438b1c0=9` (confirm);
  `b1c0==9 ∧ s98>0xb` → real teardown/skip.
- `FUN_00454191` (1391 B) — prompt RENDER: scene darken + gold banner `DAT_073cb900`
  (640×480), alphas keyed on `DAT_06a4999c`/`DAT_06a49990`; Yes/No highlight via
  `DAT_06a4997c` (0=Yes). (`DAT_073cb900`/`DAT_073d8688` texture sources still TBD.)
- Confirm/skip teardown: `FUN_00435612` / `FUN_004844ef` / `FUN_00473c03|668|672`.

Port hooks: `g_scene_state` (`scene.c`), `scene1_intro_dialogue_active()` +
`_runtime()` + `_program()`, dialogue state machine `D_SCRIPT1→D_LOAD→D_SCRIPT2→
D_DONE` (`scene1_intro_dialogue.c`), `scene1_dialogue_draw.c` render-hook pattern.
The port's `g_scene_substate` (`scene.c`) mirrors `DAT_0438b1c8`.

## Phases

**Phase B — skip-event state machine + functional skip** (port-side, host-testable):
- In `esc_dispatch`: `skip_event_arm()` (gate on the port's `b1c8`-equivalent ==0 +
  dialogue active), `skip_event_tick(held)` (counter `s98` 1→2→3 → confirm; Yes/No
  nav via `DAT_06a4997c`-equiv; cancel via `DAT_06a499c8`-equiv). Call the tick from
  the scene1 ingame tick.
- `scene1_intro_dialogue_skip_to_end()` — force-complete the script + advance `D_*`
  to reach HOUSE free-roam (mirror the engine teardown at port altitude).
- Unit-test the pure-C core; behavioral test: ESC → confirm → HOUSE reached early.

**Phase C — faithful prompt render + selection** (`src/skip_prompt_draw.c`, Win32):
- C1: gold banner `DAT_073cb900` + light scene-darken (match the user screenshot).
- C2: the RTT scene-snapshot/darken pass faithfully (if the prompt uses it; note the
  heavy blur is the pause-menu transition — verify which the skip prompt actually uses).
- C3: pixel-reconcile + Yes/No highlight; finish the selection/confirm input RE.
- Gated on a clean retail prompt-over-HOUSE golden (see Next step above).

## Open RE / PORT-DEBT
- Reproduce `b1c8==0` on a real dialogue line for the golden (the immediate blocker).
- `DAT_073cb900` / `DAT_073d8688` texture sources (no standard `FUN_0047193c` loader).
- `quit_allowed()` title-only until the pause menu (`FUN_0049a59e`) lands (PORT-DEBT).
- Non-prologue skippability branches in `FUN_00453384` dormant until shop/story
  events are reachable (PORT-DEBT).
