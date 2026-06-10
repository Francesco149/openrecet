# Conversation-pose driver — FUN_0048407f + the talk-event flag (DAT_0450f470)

> **PORTED 2026-06-02 PM** (`scene1_conversation_pose.{c,h}`; engine-quirks §86).
> The pose is user-verified 1:1 vs retail (`intro-iv2-gap`) modulo: (a) the
> known-deferred Tear position (confirmed-parity ledger), (b) the radial-burst
> billboard near Tear (still unported — see "Deferred" below), and (c) the blink
> **phase** at fixed-offset captures (the producer-timing PORT-DEBT in step 3 +
> engine-quirks §85; blink anim itself is faithful — only the pose-entry frame
> is offset because the port enters at the iv1_2-arm edge, not retail's end-of-
> shatter-transition flag clear). Next: blink-phase sync, then the radial lines.
>
> RE'd 2026-06-02 PM. Closes the "iv1_2 opening freeroam-sprite anims" gap
> (`opening-prologue.md` §"Remaining real deltas" #4): during the iv1_2 (and any
> face-to-face) conversation, retail poses the **HOUSE freeroam chibi actors** —
> Recette looks up at Tear and blinks; Tear turns to face Recette in her talking
> pose. The port renders both actors (walker `FUN_00456f56` → leaf `FUN_0045a56f`)
> but leaves them in the default idle anim. This doc is the port spec.

## TL;DR — the poses are named animations, set by a per-frame dispatch

The chibi sprites animate via per-character `.idx` animation tables
(`chr_sprite_meta`). The conversation poses are specific anim ids:

| actor | anim id | `.idx` name (SJIS) | meaning | frames |
|---|---|---|---|---|
| **Recette** (player) | **6** | 「ティアの話を聞くよ」 | *listening to Tear's talk* | 38(d20)→39(d6)→38(d32)→39(d6) loop |
| **Tear** (companion) | **4** | 「いいですか？（ルセットと会話）」 | *conversing with Recette* | (her talk pose) |

Recette anim 6 is a 4-entry loop alternating cell **38 (eyes open, held 20/32
ticks)** and cell **39 (eyes closed, 6 ticks)** — **that is the blink** the user
flagged (`intro-iv2-gap` cap_04 eyes-open → cap_05 eyes-closed). No separate
blink sprite; the blink is two frames of the look-up animation.

(`.idx` extracted for RE via `recettear-repacker/lnk_unpack` → `idx/recette.idx`,
`idx/tear.idx`; sheet = `recette.bmp` / `mint.bmp`. Real game assets — read at
runtime, never redistribute.)

## The driver: FUN_0048407f (795 B) — per-frame actor tick

Called from the INGAME sim dispatch (`FUN_004427d3` → … ; port: `scene1_sim.c`,
listed unported). It is the **master actor tick** — part of it is already split
across the port's `scene1_player_ctrl` / `scene1_companion_ctrl`; the MISSING
part is the conversation-pose branch. Structure (all.c:84547-84659):

```c
FUN_0048407f():
  iVar2 = DAT_0438b1e0 * 0x2dfc8;          // per-save base (DAT_0438b1e0 = save slot)
  FUN_00483e7b();                          // per-frame counter bump (anim/RNG pump)
  if (*DAT_068dd2f0 < 1) {                  // *dd2f0 == 0  → HOUSE stage (port: stage mode 0)
    if (DAT_0438cc08 != 4) {               // scene mode != 4 (port stubs cc08 = 0)
      if ((&DAT_0450f470)[iVar2] == 0) {   // ★ talk-event flag CLEAR → CONVERSATION POSE
        // face each other on the X axis:
        if (Tear.x (_DAT_056da1f0) <= Recette.x (DAT_056da1d8)) {
          player_angle (_DAT_056db05c) = -PI/2 (0xbfc90fdb);  player_octant (DAT_056dab00)=2;  comp_octant (DAT_056dab58)=6;
        } else {
          player_angle = +PI/2 (0x3fc90fdb);                   comp_octant=2;  player_octant=6;
        }
        if (DAT_056daafc != 6) {           // player STATE != conversation → enter
          daaf8=daaf0=daaf4=0; DAT_056daae8 = 6;  DAT_056daafc = 6;   // anim 6, state 6
        }
        if (DAT_056dab54 != 4) {           // companion STATE != conversation → enter
          dab50=dab48=dab4c=0; _DAT_056dab40 = 4;  DAT_056dab54 = 4;  // anim 4, state 4
        }
      } else {                             // flag SET → free-roam idle
        if (DAT_056daafc != 0) { …; DAT_056daae8 = 0; DAT_056daafc = 0; }  // player idle
        if (DAT_056dab54 != 0) { …; _DAT_056dab40 = 0; DAT_056dab54 = 0; } // comp idle
        FUN_00470970();
      }
    }
    FUN_0046f621();
    if (DAT_0438cc08 == 4) FUN_0047019f();
  } else {                                  // *dd2f0 >= 1 (non-HOUSE / dungeon): reset NPCs
    for (i=0;i<3;i++) if (DAT_056da1cc[i] != -1) reset actor[i] state/anim → 0;
    if (DAT_0438b4c8==4 && DAT_0438b4cc==99) FUN_0044f078();
  }
  // step each active actor's anim (engine chr_anim_tick == port chr_anim_tick):
  for (rec=&DAT_056daae8, gate=&DAT_056da1cc; gate != &DAT_056da1d8; gate++, rec+=0xb)
      if (*gate != -1) FUN_00482a71(rec);
  if (DAT_056da1c8 != 1) FUN_0048a4d1();    // companion controller (port: companion_ctrl)
  …companion wing-sparkle dust emit every 4th frame (FUN_00447f4f)…  // port: companion_ctrl
  FUN_004708f7();                           // end-of-tick talk-flag housekeeping
  DAT_056db054 = DAT_056db054 + 1;          // per-frame phase counter (port: companion_ctrl)
```

### Actor state-record globals (player = actor 0, stride 0xb dwords = 0x2c B)

| global | field | player (actor 0) | companion (actor 2 → +0x16 dw) |
|---|---|---|---|
| `DAT_056daae8` | **anim id** (`CHR_ACTOR_ANIM`) | `daae8` | `dab40` |
| `DAT_056daaf4`/`aaf8`/`aaf0` | frame / timer / counter | reset-on-enter | `dab4c`/`dab50`/`dab48` |
| `DAT_056daafc` | **state** (0 free, 6 talk) | `daafc` | `dab54` |
| `DAT_056dab00` | facing octant | `dab00` | `dab58` |
| `_DAT_056db05c` | facing angle (rad) | `db05c` | — |

Octants 2 / 6 are the side-facing (横) directions; ±PI/2 is the matching yaw.
`DAT_056da1cc[i]` (stride 0xb) is the per-actor char-id (-1 = slot empty), the
liveness gate. Player x/z = `DAT_056da1d8`/`e0`; Tear x/z = `_DAT_056da1f0`/`f8`.

## The gate: DAT_0450f470[save] — the talk-event flag (0 = in conversation)

`(&DAT_0450f470)[DAT_0438b1e0 * 0x2dfc8]` — one byte per save slot. **0 → the
face-to-face conversation pose is active; non-0 → free-roam.** BSS-zero, so the
producer must explicitly SET it after the event to release the pose.

Writers (all.c):
- **`FUN_00470a46`** (0x470a46, 766 B) — the **new-game intro event timeline**,
  sequenced by the `DAT_0438b924` event timer relative to base `DAT_005c7df0`.
  Beats: base → `FUN_0044ba2c(10)` (arm iv1 dialogue); base+0x104 →
  `FUN_0044ba2c(10,2)` (arm iv1_2); base+0x122 → `FUN_004526f5(0,0x1e)`
  (shatter/melt transition, **render-deferred** per `opening-prologue.md`
  gap #16); then once `FUN_004528b3()!=0` (transition done) → `FUN_0045281c()`,
  **`DAT_0450f470[save]=0`** (release → conversation pose ON), `DAT_0438b924=0xc0`,
  set facing (Tear.x vs Recette.x), and **directly pose both actors**:
  `FUN_00482a51(&DAT_056daae8, 6)` (player anim 6), `FUN_00482a51(&DAT_056dab40,
  4)` (companion anim 4). [`FUN_00482a51(record, anim_id)` = the anim-SET helper
  (set id + reset frame); `FUN_00482a71` = the per-frame anim-STEP.] So the pose
  is armed at the END of the intro transition and then **maintained every frame
  by FUN_0048407f** while the flag stays 0. The producer thus depends on the
  intro event timer + the deferred shatter transition — it is its own chip.
  Pair with `FUN_004708f7` (0x4708f7, read each tick at the FUN_0048407f tail) +
  `FUN_00470970` / `FUN_00470d44` (the 0x4708–0x470d "talk manager" cluster).
- **`FUN_004852fb`** (0x4852fb) — `[save] = 1` under a stage condition
  (`DAT_0450fb84[..]==8` …) → releases the pose on scene transition. Its sibling
  `FUN_0048526d` (0x48526d) is the same conversation-enter setup minus the flag.
- Readers also in `FUN_0048670f` (the 11.5 KB player state machine) and
  `FUN_0048a833` (companion controller body).

`DAT_068dd2f0` = the stage-palette pointer (`*dd2f0 == 0` ⇔ HOUSE; the port
already models this as stage mode 0, see `scene1_maplight.c`). `DAT_0438cc08` =
scene-mode enum (port stubs 0; `!= 4` holds in HOUSE free-roam).

## Port plan

The actors already render + idle-animate; the gap is **selecting anim 6/4 +
facing during the conversation**, then **releasing back to idle**. Faithful port:

1. **New module** `scene1_conversation_pose.c` = FUN_0048407f's conversation
   branch (set player anim 6 / state 6 / facing-Tear, companion anim 4 / state 4
   / facing-Recette via the Tear.x≤Recette.x test; reset both to 0 when the flag
   is set). Pure-C, host-testable on the two records.
2. **Wire the state into the controllers**: `scene1_player_ctrl` (anim
   `moving?1:0` at L942) and `scene1_companion_ctrl` (idle/moving at
   `co_set_anim`) must **not** override anim while `state == 6` / `4` (the engine
   only re-selects free-roam anims out of the talk state). Gate their anim arm on
   `state != talk`.
3. **The producer** `FUN_00470a46`/`FUN_004708f7` (clears/services the flag). The
   faithful trigger is the talk-event manager; the port already tracks the iv1_2
   dialogue lifecycle (`scene1_intro_dialogue`), so the flag's clear/set edges
   line up with dialogue-active — but port the real `FUN_00470a46` path rather
   than proxying, per the full-port rule. Confirm the trigger condition by
   reading `FUN_00470a46`'s callers.
4. **Validate** vs `intro-iv2-gap` retail goldens: Recette must cycle anim-6
   frames 38/39 (eyes open→closed, cap_04→cap_05) facing Tear; Tear anim 4 facing
   Recette. Anchor to capture index (the scenario already pairs port|retail).

Deferred / still open: the **radial-burst billboard** near Tear in the pre-box
window (NOT the iv1_2.ivt `giku.tga`/`hatena.tga` standee effects — those ride
the big portraits later in the script). Source TBD — likely a talk-manager
effect spawn; identify alongside `FUN_00470a46`. **NOTE (2026-06-02, user):** the
radial lines also have a **phase desync** vs retail — like the blink, their
animation is offset, almost certainly the SAME root cause (the port enters the
pose / talk-event state at a different intro offset than retail; §85 + the
producer PORT-DEBT). When porting the radial-burst, give it its own per-effect
TAS anchor (à la `CONV_POSE_BLINK`) and verify the animation is 1:1 anchored to
that edge, separately from chasing absolute timing.

## Blink-phase investigation (2026-06-02 PM) — CONV_POSE_START/END anchor

Added a `CONV_POSE_START`/`CONV_POSE_END` TAS anchor (player state `daafc`→/from 6)
to BOTH targets (`anchor_trace.c` + the Frida agent) and ran `intro-iv2-blink`
(pose-anchored variant). Mapping the port + retail anchor timelines by their
shared structural anchors (`runs/scenarios/intro-iv2-blink-both-20260602T160927Z`):

| event | port frame | retail frame | rel. to that target's HF#2 |
|---|---|---|---|
| HF#1 (iv1_1 load done) | 1890 | 2759 | — |
| **CONV_POSE_START (iv1_1!)** | *(none)* | **2710** | retail poses *during iv1_1* |
| inter-script `LOADING_START` | 3370 | 4240 | — |
| CONV_POSE_END / START (load blip) | *(n/a)* | 4240 / **4241** | HF#2 − 58 |
| HF#2 (iv1_2 load done) | 3438 | 4299 | — |
| **CONV_POSE_START (iv1_2 entry)** | **3439** | (= 4241) | port HF#2 **+1** |

**Two findings.** (1) Retail's talk flag `DAT_0450f470` is **BSS-zero (pose ON)
from the intro start**, so retail poses the chibis across the WHOLE intro
(iv1_1 + iv1_2), only blipping off for 1 frame at the inter-script load
(`END@4240`/`START@4241`). The port poses **only during iv1_2** (the
`generation>=2` proxy). (2) For iv1_2, retail re-enters the pose at the
inter-script **load START** (4241, HF#2 − 58), but the port re-enters only after
the load **completes** and the iv1_2 dialogue goes active (3439, HF#2 + 1) — so
relative to HF#2 the port's blink cycle (which resets on entry) starts **~59
frames late**. That is the phase the user saw.

**Exact HF#2-anchored phase alignment is §85-blocked**: it depends on the
port's synthetic inter-script load *duration* (68 frames vs retail's 59), which
is not byte-reproducible. So the durable methodology is to anchor blink captures
to the pose's own cycle. Anchoring to `CONV_POSE_START` (the pose-entry reset)
turned out to land in retail's load fade and only catch the eyes-open hold, so
`intro-iv2-blink` instead anchors to **`CONV_POSE_BLINK`** after `HOUSE_FREEROAM`
(iv1_2 load done), post-fade.

**The blink anim is 1:1 (user-confirmed).** Anim 6 = `38(d20) 39(d6) 38(d32)
39(d6)` (verified against the real `recette.idx`), so per 64-tick cycle there
are TWO eyes-closed (cell 39) frames — frame 1 (next blink +38) and frame 3
(next blink +26). The CONV_POSE_BLINK anchor logs the SAME interval pattern on
both targets — port `[38,26,38,26]`, retail `[38,26,38,26]` — so the advance
logic is identical, NOT off. A first montage *looked* mismatched only because an
"any eyes-closed" anchor caught a frame-1 blink on the port but a frame-3 blink
on retail (port's next blink at +38, off-window; retail's at +26 = cap_13). Fixed
by firing CONV_POSE_BLINK on **frame 1 only** — a unique once-per-cycle marker
(both sides now fire it exactly 64 frames apart) so port + retail land on the
SAME blink. The aligned `intro-iv2-blink` montage (`+0..+44`) is user-confirmed
to match in every frame, proving the blink is 1:1 regardless of the §85 offset.

**Open fix (toward absolute parity), in priority order:** (a) hold the pose
across the whole intro (trigger on `intro_dialogue_active`, not `generation>=2`)
so the port poses during iv1_1 like retail; (b) re-enter the iv1_2 pose at the
inter-script load START (mirror retail's 1-frame `daafc` blip) rather than after
the load — both pieces of modelling the real `DAT_0450f470` flag lifecycle
(the `FUN_00470a46` producer), still gated on the deferred shatter transition.

> **(a) DONE 2026-06-06** (commit d97c530, engine-quirks §113). Gate dropped from
> `generation>=2` to plain `scene1_intro_dialogue_active()`: the port now poses
> during iv1_1 too and fires `CONV_POSE_START`/`END` **×2** with the inter-script
> blip (`active()` is false during `D_LOAD`, so the blip falls out for free — this
> also covers (b)'s blip structurally). Verified on `intro-prologue`: port anchor
> stream is now `HF → CONV_POSE_START → [load: CONV_POSE_END] → HF → CONV_POSE_START
> → CONV_POSE_END → FREEROAM_START`, matching retail's *count + END positions*.
>
> **REMAINING (the last ordering gap):** retail's `CONV_POSE_START` fires **during
> the load, ~49 frames before `HOUSE_FREEROAM`** (the chibi actors are spawned +
> posed under the load overlay), but the port spawns the player actor only at
> load-END (HF) so its `CONV_POSE_START` lands one load late — `… LOADING_END → HF
> → CONV_POSE_START` vs retail `… CONV_POSE_START → LOADING_END → HF`. A
> retail-recorded new-game trace's `{wait CONV_POSE_START}`-chain therefore still
> desyncs by a script. Closing it needs the **HOUSE preload to make the player/
> companion actors live mid-load** (before HF), not the pose gate — a `scene1_preload`
> spawn-timing change, tracked separately. (Continue/Load traces skip the prologue
> entirely, so they already replay 1:1 cross-target without this.)

## Tutorial-dialogue tail: the re-arm must lag the script-end by 1 frame (2026-06-10)

A pose-release timing gap in the SHOP-DISPLAY tutorials (iv1_5/iv1_6), distinct
from the prologue work above. The conversation pose is released (`CONV_POSE_END`)
when the next dialogue's load begins, so the pose-release timing == the next-load
arm timing. The port armed iv1_6's `LOADING_START` the **same** frame iv1_5's
script completed (last `CONV_POSE_BLINK`→`CONV_POSE_END` = 8f port vs **9f**
retail → a constant d=−1 on every iv1_6 anchor, d=−2 after iv1_6's tail). **Retail
defers the re-arm by 1 frame:** its gate `DAT_0438b1c8` clears 1→0 in
`FUN_004536cb`'s outer-loop tail (`b1c8==1 && FUN_0046c320()`, all.c:50515/50631)
*after* that frame's `FUN_0044bd0d` dispatch already ran and saw it still busy
(item-display-2 call-trace: iv1_5 `FUN_0046c320`-done @f15933 → iv1_6
`FUN_00452d07` load-spawn @f15934). The port's `scene1_tutorial_dispatch_tick`
runs after `scene1_intro_dialogue_tick`, but the port cleared its gate-equivalent
(`D_TUT`→`D_IDLE`) the completion frame, so the dispatch armed same-frame. **Fix
(`c8a40df`):** a one-frame `D_TUT_DONE` settle latch in `scene1_intro_dialogue.c`
(`_busy()` stays true so the dispatch skips; `_posing()` keeps the pose on for
that frame — retail's blip-off lands at the next `LOADING_START`, not at
completion; next tick → `D_IDLE` → arm). Closes the iv1_5-tail AND iv1_6-tail
slips. Verified: iv1_6 anchors bit-aligned to retail (+733/+734/+1166 from iv1_5
`CONV_POSE_START`), over-threshold 861→529. Full RE: `shop-display-menu-RE.md` #8.

## Cross-refs
- `opening-prologue.md` §"Remaining real deltas" #4 (the gap), §"What actually
  drives the prologue" (FUN_0048407f first noted as the actor animator).
- `scene1-char-sprite-render.md` (the walker/leaf renderer that draws the result;
  notes FUN_004427d3→FUN_0048407f as "the cutscene controller").
- `chr_sprite_meta.{c,h}` (the `.idx` animation-block model the anim ids index).
- `engine-quirks.md` §71/§73/§81 (companion render + wing anim), §75 (player move).
