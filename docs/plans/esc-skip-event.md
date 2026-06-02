# Plan — context-sensitive ESC + the skip-event prompt

> Durable copy of the working plan (was `~/.claude/plans/hidden-wiggling-snail.md`).
> Companion RE doc: `docs/findings/esc-skip-event.md` (the authoritative,
> continuously-updated subsystem map). Read that first.

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
- **NEXT = Phase C (needs a golden — HUMAN/RE GATE):** render the gold "Do you
  want to skip this event?" banner + light scene-darken (`FUN_00454191`) and
  reconcile the confirm choreography against a live retail prompt-over-HOUSE
  golden. The golden is **blocked on reproducing `DAT_0438b1c8 == 0` on a real
  dialogue line** — the turbo/forced Frida playthrough leaves it stuck at 1.
  Most reliable unblock: a **user-recorded playthrough trace** (F2/F3 recorder,
  faithful line pacing) that settles a line with `b1c8==0`, then inject ESC +
  capture. Then port the banner render and set `g_skip_event_enabled=1`.
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
