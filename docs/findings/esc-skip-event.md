# ESC key — context-sensitive dispatch + the skip-event prompt

> Started 2026-06-02. The port's WndProc ESC handler was a skeleton that always
> `PostMessage(WM_CLOSE)` → the "quit the game?" box popped in *every* context.
> Retail routes ESC by context: title → quit, in-game free-roam → pause menu,
> in a script/dialogue → a yes/no "skip event?" prompt. This doc maps the engine
> subsystem and tracks the port.

## The dispatch (WndProc `FUN_0047b2e7`, `by-address/47b2e7.c:96-114`)

```
WM_KEYDOWN, key == VK_ESCAPE (0x1b):
  if FUN_00452911() != 0:  return                  // (A) DAT_06a49954 — ESC globally disabled
  if DAT_0438b1c0 != 0:  FUN_0045337b(); return     // (B) any in-game sub-mode → skip-event handler
  if FUN_0049a585() == 0:  return                   // (C) an overlay is open → swallow
  PostMessage(hwnd, WM_CLOSE, 0, 0)                 // (D) title, nothing open → quit confirm
```

- **(A) `FUN_00452911`** is a 6-byte getter `return DAT_06a49954;`. Writers that
  *set it to 1* (disable ESC): `FUN_00452917`, `FUN_00452cde`, `FUN_00452e39`,
  `FUN_00452eed` — fired across non-interruptible loads/transitions.
- **(B) `DAT_0438b1c0`** is the in-game **sub-mode** global (NOT the same as the
  port's coarse `g_scene_state`, though they share the address-0 "title/idle"
  meaning). Within the in-game tick `FUN_004536cb` it dispatches finer values:
  `1`=script/dialogue playing, `2/3/6/7/8`=shop/menu/event state machines,
  `9`=**skip-confirmed**, `0xa`/`0xb`=menus incl. pause, `0xd..0x10`=more.
  `FUN_0045337b` → `FUN_00453384(0)` is the skip-event entry.
- **(C) `FUN_0049a585`** returns 1 (allow quit) only if
  `DAT_09643520 == 0 && DAT_09643544 == 0` — those are the free-roam pause/menu
  "overlay engaging" counters (written by the pause subsystem `FUN_0049a59e`).
  So during free-roam ESC is swallowed; only at the title with nothing open does
  it fall through to the quit box.

## The skip-event prompt (`FUN_00453384`, 821 B, `by-address/453384.c`)

Three entry points set the **skip-kind** `DAT_06a4997c`: `FUN_0045337b`→`(0)`
(the general/dialogue skip — what the prologue uses), `FUN_004536b9`→`(1)`,
`FUN_004536c2`→`(2)` (other event-specific skips). They are *distinct triggers*,
not yes/no cursor positions; pressing a different kind while one is pending plays
the "can't" SE (`FUN_00499519(0x16a)`, the `DAT_06a4997c != param` guard).

Arm path (when `DAT_06a49998 == 0` and the event is skippable — a long `cVar4`
gate over save/shop/stage flags): plays the prompt SE `0x16b`, snapshots the
resume state (`DAT_06a499a8 = DAT_0438b1c0`, `FUN_00435625/44/682bf/682b9/681e6`),
then sets the prompt state:
- `DAT_06a49998 = 1` — confirm/phase counter (advanced by `FUN_004532df`; caps 0xc while open).
- `DAT_06a4999c = 1` — render/visible phase (advanced in `FUN_004536cb`, caps 0xc).
- `DAT_06a499a0 = 1` — prompt-open flag.

Confirm: `FUN_004536cb:127` `if (DAT_06a49998 == 3) DAT_0438b1c0 = 9;` → the
`DAT_0438b1c0 == 9` arm of `FUN_00453384` runs the teardown/resume
(`FUN_00435612` / `FUN_004844ef` / `FUN_00473c03|668|672` by skip-kind). Cancel:
the `DAT_06a499c8` counter (reaches 2 → zero the prompt + restore resume state,
`FUN_004536cb:111-126`).

> **Open RE (Phase C):** the exact open→confirm→close counter choreography
> (how `DAT_06a49998` lands on 3, the `a499a0`/`a499c8` interplay, the actual
> confirm/cancel buttons) reads ambiguously from static disasm — capture it live
> via a Frida watch on these globals while driving the skip key on retail.

## Render (`FUN_00454191`, 1391 B, `by-address/454191.c`)

Renders when `DAT_06a4999c > 1`. Render-to-texture: snapshots the framebuffer
into offscreen targets (`DAT_073de648`/`DAT_073de64c`, created via the
SetRenderTarget vtable slots 0x80/0x84/0x7c), draws a darkened/blurred version
(`DAT_073d8688` strips, `0x14dcdcdc` tint), then composites the full-screen
640×480 prompt overlay `DAT_073cb900` with a fade alpha keyed on `DAT_06a4999c`
(and the `DAT_06a49990` highlight). Yes/No highlight = `DAT_06a4997c==0 ?
0xff173c8c : 0xff3c3c3c` (lines 94-100). Texture sources for `DAT_073cb900` /
`DAT_073d8688` not yet located (no standard `FUN_0047193c` loader; likely a
system-atlas array near `data_win.tga`'s `DAT_073d8678`).

## Input reachability — why goldens are capturable

`FUN_004536cb:108` also arms the prompt from the **DInput** path:
`if ((held & 0x100) && DAT_06a49954==0 && DAT_0438b1c0!=0) FUN_00453384(0);`
— button bit `0x100` ("E"). So the skip prompt can be driven through the
input-segtrace harness (which overwrites the player-0 mask at `input_poll`),
i.e. retail goldens are capturable without a human at the keyboard.

## SUMMARY — skip trigger condition cracked (2026-06-02)

The skip arms via `FUN_0045337b → FUN_00453384(0)` **iff `DAT_0438b1c8 == 0`**
(the dialogue sub-state) and the `cVar4`/`bf7c`/`be98` terms below. That window =
the cutscene fade/load AND, in real play, settled dialogue lines. **Confirmed
arm:** spamming the skip from frame ~50 (`b1c8==0`) opened the prompt
(`s98:1→2→3`, `sa0=1`) and confirmed (`DAT_0438b1c0=9`) — captured the prompt
opening (`runs/skip-prompt-golden/frame_00052.png`) + the RTT scene-transition.
Arms at frames 120-456 failed only because the Frida forced/turbo playthrough
leaves `b1c8=1` stuck there (dialogue not cycling like real play). Render =
the gold "Do you want to skip this event?" banner (user screenshot) + a light
scene-darken; the heavy radial-blur is the *transition*, shared with the pause
menu. Choreography + render fns mapped below. **Remaining:** reproduce `b1c8==0`
on a real dialogue line (faithful pacing / user trace) for an over-HOUSE golden,
then port Phase B (state machine, arm-when-b1c8==0) + Phase C (banner render).

## Is the opening prologue even skippable? (investigation 2026-06-02)

Tried to trigger the skip on the prologue, both ways, on retail
(`cutestation.soy:27042`):

1. **DInput injection** of game-button `0x100` (the line-108 path) — pulsed 14×
   on a settled line (`b1c0==1`, `b1c8==0`). The skip globals
   (`DAT_06a49998/9c/a0/7c`, `DAT_06a499c8`) **never moved**. The injected mask
   reaches `DAT_073dddd0` (verified via `--watch raw`), so the edge fired — yet
   no arm. ⇒ `0x100` is not the prologue's skip trigger; the skip is
   **keyboard-ESC-only** (WndProc → `FUN_0045337b`), not DInput-reachable.
2. **Direct call** of `FUN_0045337b` via a new Frida probe (`--arm-skip-at-frame`)
   at frames 120 and 250 (settled lines) — still **no arm**.

**The prologue IS skippable** (user-confirmed 2026-06-02: ESC → yes/no → confirm
Yes). The probe failed to arm for a **harness-state** reason, now isolated.

Watching the precise arm conditions of `FUN_00453384`'s `LAB_004534df` (for
selection `s7c==0`): arm requires
`cVar4==1 ∧ DAT_0438bf7c==0 ∧ DAT_06a49990==0 ∧ DAT_0438be98==0`.
The early gate inputs were all clear (`DAT_0438af34/b148==0` ⇒ `FUN_00434dd6`
false; `DAT_06a4995c/0438b4e0/0438cc08/0438b928==0`), but **`DAT_0438bf7c == -1`
throughout the dialogue** (from the dialogue-active frame onward) → the
`DAT_0438bf7c==0` term fails → no arm.

`DAT_0438bf7c` is set to `-1` by **`FUN_0045281c`** (the scene melt/shatter
transition; `FUN_0045281c(0,0x11)` — the same transition the inter-script load
bracket uses, deferred PORT-DEBT in the port). It's cleared back to 0 when the
transition completes (`FUN_004528b3`/`FUN_004526ab` per-frame). In the
**turbo + Frida-injected** capture it stays stuck at -1 (the transition never
settles in the fast-forward/spawn context), so the skip can't arm there. In real
play it returns to 0 between lines, which is when ESC arms the prompt.

### CORRECTION (user, 2026-06-02)

- **The prologue is skippable by pressing ESC *at any time during the dialogue*,
  not just the fade-in** (user is 100% sure). My direct-call probe
  (`FUN_0045337b` via Frida) armed only when called during the `b1c0==0`
  fade/title window and NOT during `b1c0==1` (dialogue) — so the **direct call is
  not a faithful reproduction** of what the real ESC keypress does during the
  dialogue. The real path likely needs the actual `WM_KEYDOWN/VK_ESCAPE` (full
  WndProc dispatch) or additional state the bare call misses. Treat the
  choreography trace below as *indicative*, not authoritative.
- **The radial-blur RTT effect I captured (`runs/skip-prompt-golden/` f56) is the
  free-roam PAUSE menu**, not the skip prompt — arming at the title spuriously
  triggered the pause-style transition. The **real skip prompt** (user screenshot)
  is a gold scroll banner over a *mildly darkened* HOUSE — no swirl. So
  `FUN_00454191`'s render is the banner + a light scene-darken; the heavy
  render-to-texture blur belongs to the pause menu (`FUN_0049a59e` family).

**Implementation approach (revised):** rather than perfectly reproduce the engine
counter choreography via Frida (the direct-call model is unfaithful, and it's not
needed for a faithful *observable* result), implement the prompt + skip to match
the **user screenshot** (render) and the **observable behavior** (ESC anytime in
the dialogue → yes/no → Yes skips to HOUSE / No resumes), verified against the
screenshot + the user pressing ESC in the port.

### Frame-model correction + harness delivery wall (2026-06-02)

**The real dialogue lines are at ~frame 456+, not 72-250.** Screenshot
calibration (`runs/skip-calib/`, `runs/skip-fullcap/`) shows the new-game HOUSE
cutscene plays a long **bedroom intro / fade** from `b1c0==1` (frame 72) until
**line 0's dialogue box appears ~frame 456** (`runs/skip-fullcap/frame_00456.png`
= the cap_00 "Oh, for the love of…" line). Every earlier skip probe (frames
120-300) fired during the *intro* (bedroom, no dialogue box) — never on an actual
dialogue line — which is why none armed. **Lesson:** screenshot-verify the state
before trusting a global; `b1c0==1` ≠ "a dialogue line is up". The full
`intro-dialogue-lines/trace.jsonl` (with its `HOUSE_FREEROAM` anchor) is what
reaches the lines; truncated prefixes leave the cutscene stalled on the bedroom.

**Still cannot trigger the skip in the Frida harness.** Tried: DInput button
`0x100` injection, direct `FUN_0045337b` call, and `PostMessageA(WM_KEYDOWN/
WM_KEYUP, VK_ESCAPE)` to the engine HWND — none armed (`DAT_06a49998` stayed 0),
even at frame 456+ with the dialogue box up. A call-trace on the skip funcs did
NOT show `FUN_0045337b` firing from my posted ESC → **the posted key message
isn't reaching WndProc** (the `--hide-window` window likely doesn't dispatch
posted keystrokes through the engine's PeekMessage pump). Open next steps:
(a) hook WndProc `FUN_0047b2e7` and log `(msg,wParam)` to confirm delivery;
(b) try a non-hidden run or `SendMessage` (same-thread → synchronous WndProc);
(c) if delivery works but the arm still fails, trace `FUN_00453384`'s branch to
find the rejecting gate term at frame 456+.

### ROOT CAUSE of the non-arm: dialogue sub-state `DAT_0438b1c8`

`FUN_00453384`'s arm path is gated `if (DAT_0438b1c8 == 0) { …arm… } else
{ FUN_0046c2cb() → SE 0x143 / cVar4=0 → no arm }`. Watched live during the
cutscene: `DAT_06a49954` (esc-disabled) is 1 at f72 then **0** from f83 (NOT the
blocker), but **`DAT_0438b1c8` = 1 from ~f87 onward** (0 only at f72-83, briefly
2 at f84). So the arm gate's `b1c8==0` branch is never taken once the cutscene
is running → every arm attempt (DInput, direct call, WndProc-dispatch) is
rejected. In real play `b1c8` must return to 0 at skippable moments (settled line
awaiting input); the **Frida forced/turbo playthrough leaves the dialogue stuck
with `b1c8=1`** (lines not advancing/cycling like real play), so the gate always
rejects. This — not delivery — is why nothing armed.

**Next step (fresh session):** drive the dialogue *faithfully* so `b1c8` cycles to
0 — advance lines with correctly-timed A-presses to a settled TEXT_ANIM_END
state, screenshot-verify the line is up AND watch `b1c8==0`, THEN inject ESC and
trace the arm + the FUN_00453384 → FUN_00454191 call graph. A user-recorded
playthrough trace (proper line pacing) is the most reliable way to get that
state. Probe `arm_skip_at_frame` should fire only while `b1c8==0`.

### Indicative choreography (early direct-call probe; unfaithful, see caveat)

The arm only fires while **`DAT_0438b1c0 == 0`** (the title / new-game-load / fade
*into* the cutscene — the "black screen" the user spams ESC on). During the
dialogue proper (`b1c0==1`) the `if (b1c0==1){…}` sub-block of `FUN_00453384`'s
`cVar4` gate rejects it — which is why arming at frames 120/150/250 (deep in
dialogue) never armed, but **spamming `FUN_0045337b` from frame ~50 armed
immediately**. (User: "I can trigger it by spamming ESC at the black screen.")

Observed choreography (`--arm-skip-at-frame 50`, spam each frame until armed):
```
f51  b1c0=0  s98=2  s9c=2  sa0=1   ← prompt OPEN (sa0=1); FUN_004532df climbs s98, FUN_004536cb climbs s9c
f52  b1c0=0  s98=3  s9c=3          ← s98==3 …
f53  b1c0=9  s98=4  s9c=4          ← FUN_004536cb:127 sets DAT_0438b1c0=9 (skip CONFIRMED)
f54+ b1c0=9  s98→0xc(cap) s9c→0xc  ← teardown/close anim; FUN_00453384's b1c0==9 arm fires the real skip once s98>0xb
```
So: `DAT_06a499a0=1` = prompt open; `DAT_06a4999c` = the open/fade anim (render
gate, draws when >1); `DAT_06a49998` = confirm counter (==3 → state 9); state 9
+ `s98>0xb` → the actual event teardown/resume. Selection `DAT_06a4997c` stayed 0
(Yes) throughout — the user's screenshot shows the cursor on **Yes** by default.

**Render reference:** retail screenshot (`/mnt/c/Users/headpats/Documents/skip.png`,
mirrored to the feed) — a gold scroll banner **"Do you want to skip this event?"**
with **👉 Yes / No** over a darkened HOUSE. My Frida captures (`runs/skip-prompt-
golden/`) show the same prompt opening over the title + the `FUN_00454191`
render-to-texture **radial-blur** scene-snapshot transition.

Probe tooling (kept): `--arm-skip-at-frame N` in `frida_capture.py` → agent
`arm_skip_at_frame` spams `FUN_0045337b` each frame from N (input phase, not
mid-render) until `DAT_06a49998>0`. Reproduces the arm for choreography traces +
golden capture.

## Port status

- **Phase A (LANDED 2026-06-02):** `src/esc_dispatch.{c,h}` — `esc_pressed()`
  routes per `g_scene_state`: title → `ESC_RESULT_QUIT`; any non-title →
  `ESC_RESULT_SWALLOW`; `g_esc_disabled` (mirror of `DAT_06a49954`) swallows
  everywhere. `main.c` WM_KEYDOWN calls it. Fixes the wrong-quit bug. Unit-tested
  (`test_esc_dispatch.c`). PORT-DEBT: the (C) overlay-open suppress is title-only
  until the pause menu lands; `g_esc_disabled` has no producer yet.
- **Phase B (LANDED 2026-06-02):** `src/skip_event.{c,h}` — the prompt state
  machine (`skip_event_arm`/`_tick`/`_open`/`_close` + phase/selection getters)
  + `scene1_intro_dialogue_skip_to_end()` (force the prologue to D_DONE →
  free-roam). Arm wired into `esc_pressed()` (in-game ESC → `skip_event_arm(
  scene1_intro_dialogue_active())`); the modal tick wired into `sim.c` (prompt
  open → freeze dialogue, run prompt; CONFIRMED → skip_to_end). 12 host tests
  (`test_skip_event.c`), suite green; both exes build.
  - **Gated OFF live:** `g_skip_event_enabled` defaults to 0, so the arm is a
    no-op and in-game ESC keeps the Phase A swallow — an armed-but-unrendered
    prompt would freeze the dialogue with no visible Yes/No (soft-lock). Phase C
    flips it on with the banner render.
  - **Faithful vs PORT-DEBT:** the arm gate + open/teardown *structure* are from
    the disasm; the interactive Yes/No / confirm / cancel *input* is modelled to
    the user-confirmed observable behavior (cursor defaults Yes, Left/Right
    toggles, A confirms, B cancels). The exact engine choreography is **not
    statically legible** — the auto-confirm counter (`FUN_004532df`) climbs only
    while ESC is *disabled*, and the cancel counter `DAT_06a499c8` is **never set
    positive anywhere in the decompiled corpus** — so the real selection storage
    + confirm/cancel path resolve through state the static dump doesn't express.
    Reconcile against a live golden when Phase C lands.
- **Phase C (pending):** the faithful yes/no prompt render (gold banner +
  light scene-darken, per the user screenshot) + the live-traced confirm
  choreography, verified vs retail goldens. Flips `g_skip_event_enabled` on.
  BLOCKER: a clean retail prompt-over-HOUSE golden (reproduce `b1c8==0` on a
  real dialogue line — needs a faithful/user-recorded playthrough trace).

## Port mapping

| engine | port |
|---|---|
| WndProc ESC arm `FUN_0047b2e7` | `esc_pressed()` (esc_dispatch.c) + main.c WM_KEYDOWN |
| `DAT_06a49954` (esc-disabled) | `g_esc_disabled` |
| `DAT_0438b1c0` (sub-mode) | `g_scene_state` (coarse; finer values pending) |
| `FUN_0049a585` (quit gate) | title-only check (PORT-DEBT) |
| `FUN_0045337b` → `FUN_00453384` arm | `skip_event_arm()` (skip_event.c) |
| `DAT_06a499a0` (prompt-open) | `skip_event_open()` / `g_open` |
| `DAT_06a4999c` (render phase) | `skip_event_phase()` / `g_phase` |
| `DAT_06a4997c` (skip-kind=0) | `g_kind` (fixed 0; selection is `g_sel`, PORT-DEBT) |
| `FUN_004532df` confirm counter | observable A-confirm (PORT-DEBT: counter choreography) |
| skip teardown (`FUN_00473c03` …) | `scene1_intro_dialogue_skip_to_end()` |
| `FUN_00454191` (render) | pending (Phase C) |
