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

**Implication for capture:** the skip is **keyboard-ESC-only** (WndProc, not the
DInput button mask) AND needs `DAT_0438bf7c==0` (transition settled). Neither the
input-segtrace nor a turbo direct-call reproduces that cleanly. Options to get
Phase C goldens: (a) a non-turbo retail run + direct-call timed to a
`DAT_0438bf7c==0` window, (b) inject a real `WM_KEYDOWN/VK_ESCAPE` to the retail
HWND from the agent, or (c) a user screenshot of the prompt.

Probe tooling (kept): `--arm-skip-at-frame N` in `frida_capture.py` → agent
`arm_skip_at_frame` → direct-calls `FUN_0045337b` once at frame N in
Present.onEnter.

## Port status

- **Phase A (LANDED 2026-06-02):** `src/esc_dispatch.{c,h}` — `esc_pressed()`
  routes per `g_scene_state`: title → `ESC_RESULT_QUIT`; any non-title →
  `ESC_RESULT_SWALLOW`; `g_esc_disabled` (mirror of `DAT_06a49954`) swallows
  everywhere. `main.c` WM_KEYDOWN calls it. Fixes the wrong-quit bug. Unit-tested
  (`test_esc_dispatch.c`). PORT-DEBT: the (C) overlay-open suppress is title-only
  until the pause menu lands; `g_esc_disabled` has no producer yet.
- **Phase B (pending):** the skip-event state machine + `scene1_intro_dialogue_
  skip_to_end()` (functional prologue skip).
- **Phase C (pending):** the faithful yes/no prompt render + the live-traced
  confirm choreography, verified vs retail goldens.

## Port mapping

| engine | port |
|---|---|
| WndProc ESC arm `FUN_0047b2e7` | `esc_pressed()` (esc_dispatch.c) + main.c WM_KEYDOWN |
| `DAT_06a49954` (esc-disabled) | `g_esc_disabled` |
| `DAT_0438b1c0` (sub-mode) | `g_scene_state` (coarse; finer values pending) |
| `FUN_0049a585` (quit gate) | title-only check (PORT-DEBT) |
| `FUN_00453384` / `FUN_004532df` | pending (Phase B) |
| `FUN_00454191` (render) | pending (Phase C) |
