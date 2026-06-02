# Conversation-pose driver — FUN_0048407f + the talk-event flag (DAT_0450f470)

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
- **`FUN_00470a46`** (0x470a46, 766 B) — `[save] = 0` → **starts** the
  conversation pose. This is the talk-event entry; pair with `FUN_004708f7`
  (0x4708f7, read each tick at the FUN_0048407f tail) + `FUN_00470970` /
  `FUN_00470d44` (the 0x4708–0x470d "talk manager" cluster).
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
effect spawn; identify alongside `FUN_00470a46`.

## Cross-refs
- `opening-prologue.md` §"Remaining real deltas" #4 (the gap), §"What actually
  drives the prologue" (FUN_0048407f first noted as the actor animator).
- `scene1-char-sprite-render.md` (the walker/leaf renderer that draws the result;
  notes FUN_004427d3→FUN_0048407f as "the cutscene controller").
- `chr_sprite_meta.{c,h}` (the `.idx` animation-block model the anim ids index).
- `engine-quirks.md` §71/§73/§81 (companion render + wing anim), §75 (player move).
