# Background-window NPCs (formerly the misnamed "ambient motes")

> 2026-06-02.  The shop's back window shows townsfolk drifting past — "multiple
> NPCs, sometimes 3 at a time."  This subsystem was originally ported under the
> name **"ambient floor motes"**, which was a **misidentification**: it is the
> engine's **background-NPC system**, not ambient particles.

## What it is

Up to 6 NPCs (`DAT_005c7dd4` == 6) drift horizontally at floor level behind the
shop and are seen through the back window.  Each NPC draws:

1. a **dark contact shadow** on the street (`FUN_0046f648`) — the faint moving
   shadows visible through the window (often the only part not occluded by the
   window's lower frame), and
2. a **bright character sprite** (`FUN_0046f737`) — the actual visible townsperson.

## The bug that hid them

The port simulated the drift (`FUN_0046f2a3`) and drew the shadows
(`FUN_0046f648`) from early on, but the **bright sprite render `FUN_0046f737`
was a hidden stub** — `scene1_shop_walker.c`'s L457 "between-pass sweep" TODO
(`sw_pass_between_TODO`) was a no-op, yet the port ledger marked `0x46f737` ✓.
So the port drew only the shadows.  Diagnosed 2026-06-02 via a port d3d-trace at
a free-roam frame (every quad accounted for; none were NPC sprites) on a
user-recorded trace (`openrecet-trace-25120`), and confirmed by the user
recalling a past session where setting the "motes" red+huge made them visible
through the window.

## Engine functions

| VA | port | role |
|---|---|---|
| `FUN_0046f621` | `scene1_bg_npc_tick` | warmup pump (180× on first call, 1×/frame after) |
| `FUN_0046f2a3` | `scene1_bg_npc_sim_once` | spawn + drift + sprite-anim step (the sporadic free-roam RNG consumer) |
| `FUN_0046f648` | `scene1_bg_npc_shadow_render` | dark contact-shadow blobs (shade.bmp, ×darken) |
| `FUN_0046f737` | `scene1_bg_npc_sprite_render` | **bright character billboards (newly un-stubbed)** |
| `FUN_00482a51` | `bg_npc_anim_set` (inline) | set sprite-anim id (reset frame/counter/timer) |
| `FUN_00482a71` | `chr_anim_tick` (reused) | advance sprite-anim frame |

## Record + render facts

- **Record:** SoA base `DAT_073a7f80`, stride 0x19 dw (0x64 B).  The leading 11
  dwords are a chr-actor sprite-state header (`CHR_ACTOR_*` layout: anim@0,
  timer@2, counter@3, frame@4, state@5, facing@6 = the drift "flip", flags
  7/8/9, age@10).  Drift fields: x@0x2c, y@0x30, z@0x34 (room depth −11..−15),
  dir@0x44, visible@0x48, type@0x4c, speed@0x50, pause@0x54, vthresh@0x58,
  mode@0x5c, prob@0x60.  Modelled in `scene1_bg_npc_t` (`arec[11]` header +
  named drift fields).
- **type → sheet char id:** `DAT_005c7ce0[type*2]` (static `(char_id,key)`
  registry @ 0x5c7ce0).  The 6 live types `DAT_005c7dd8` = {0,1,6,7,9,8} map to
  sheet ids **{10, 35, 39, 36, 37, 38}** → `bmp/chr/chr{10,35..39}.bmp`
  (512×512), loaded via `scene1_preload_load_chr_sheet`.
- **Bright billboard:** world = `billboard(DAT_0438cdf8) × Scale(0.03) ×
  Translate(x,y,z)`, sheet `DAT_073a9b18[char]`, colour `0xff7f7f7f`, drawn via
  the shared chr-sprite leaf `scene1_chr_sprite_render` (`FUN_0045a56f`).  Wired
  at the shop-walker L457 slot (after `FUN_004705a3`, before `FUN_00470d44`).

## Status / deferred

- Sprites **render + animate**, user-verified on screenshots (2026-06-02, feed).
- **Exact NPC identity / anim phase vs retail not yet pinned** — the port draws a
  valid walking sprite; whether the per-frame char/anim/phase matches retail at a
  given anchor is a faithfulness follow-up (cf the deferred Tear/foot-dust phase
  items in confirmed-parity-ledger.md).  The frame advance reuses `chr_anim_tick`
  (the formdata LUT) rather than re-deriving `FUN_00482a71`'s raw index.
- **No separate genuine ambient-mote/particle effect exists** today — "motes" was
  this NPC system all along.  However a **minor true ambient particle effect is
  expected to re-emerge later**: the occasional tiny dots seen in port↔retail
  diffs are likely a real (very faint) ambient effect not yet found.  Track it
  when it surfaces; do not re-conflate it with this NPC system.
