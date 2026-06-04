# Recette's RENDERED walk cell diverges from retail (render-side, not phase)

**Status: OPEN — next-session autonomous investigation. Found 2026-06-04 (user eyeball).**

## The finding (and the correction it forces)

`phase_probe house-walk-down-dense` reports the PLAYER anim **record** fields
(`ANIM 0x56daae8 / COUNTER 0x56daaf4 / FRAME 0x56daaf8 / FACING 0x56dab00`,
i.e. `&DAT_056daae8[0]`) as **bit-exact 1:1** vs retail on every frame. That was
read as "Recette's anim phase is 1:1" (commits a79f8b0/cb9f465) — **too strong.**

The user eyeballed the phase+RNG-aligned walk montage and found Recette on a
**different walk cell** on ~18% of frames. Confirmed at **db054=60**: both sides
log `aframe=1 anim=1 cnt=10 oct=4` AND her on-screen X matches (centroid dX≈0px),
yet her **legs are a different walk step** (port feet near-together/one boot
forward; retail mid-stride). So:

- The divergence is **NOT** a phase-counter desync (the record counters are
  aligned), **NOT** position (centroid matches), and **NOT** an off-by-one capture
  skew (it hits non-transition frames — db054 50, 59, 68 — and the divergent
  frames are irregular, not periodic; user-confirmed).
- Therefore the **rendered** player cell is **not a pure function of the record
  fields phase_probe watches.** The walker draw is reading some OTHER state for
  the cell.

**So: the player anim is 1:1 at the record-counter level but the DRAWN walk cell
diverges on ~18% of frames.** Correct the ledger/claims accordingly.

## Divergent frames (house-walk-down-dense, 45-frame window db054 40–84)

`db054 = 42, 50, 51, 59, 60, 68, 69, 84` — 8/45 (18%). (User flagged f=2,10,11,19,20
= db054 42,50,51,59,60; the metric also caught 68,69,84 — "there's more", confirmed.)
No single aframe value is implicated (flagged at aframe 0/1/2/3), so it is not a
static cell→sprite mapping bug; it is dynamic/frame-specific.

## The autonomous detector — `tools/recette_anim_probe.py`

```sh
nix develop --command python3 tools/phase_probe.py house-walk-down-dense   # aligned frames
nix develop --command python3 tools/recette_anim_probe.py house-walk-down-dense --push
```

Crops a tight **Recette-only** box `(486,610,566,734)` that EXCLUDES Tear + her
wing-glow (Tear stands to Recette's right, x>~570), diffs port vs retail, and
flags a frame when the **top-half** strong-diff fraction (mean|abs|>30/255, head+
torso rows) exceeds **8%** — the user's "diff mostly white up to the top = different
anim cell" heuristic. Calibration: ALIGNED frames score **0.0%** top-half,
DIVERGENT frames **16–19%** (clean separation). It builds a 4× `[PORT|RETAIL|diff
×6]` montage of the flagged frames (`runs/recette-anim-probe/<scenario>/`) to
eyeball — read the UPPER body; the feet are obscured by foot-dust.

## Investigation leads (next session)

The drawn player cell comes from the shop-walker (`FUN_004552d0`) / chr-sprite
walker (`FUN_00456f56`), which read a sprite-state record. Candidates for the
cell source that diverges while the live actor-record matches:

1. **The FUN_0048b850 motion/sprite-history ring** (`DAT_056da3dc` rec-hist /
   `DAT_056da1fc` pos-hist, 40 slots). If the walker draws from a ring SLOT (a
   delayed/historical record) rather than the live actor[0] record, a port ring-
   fill/index difference would change the drawn cell while the live record matches.
   The ring fill is `player_ctrl.c` (s_rec_hist/s_pos_hist) — check the slot the
   draw selects + the fill order vs retail.
2. **The STATE field `0x56daafc`** (CHR_ACTOR_STATE) — phase_probe does NOT watch
   it. Add it to `STD_WATCHES` (+ the port pos-log) and re-diff; if it differs on
   the flagged frames, that is the cell discriminator.
3. **A sub-frame / finer anim timer** the render samples (the record TIMER
   `0x56daaf0` is also unwatched — add it too).

Method: extend `phase_probe.STD_WATCHES` with STATE (`0x56daafc`) + TIMER
(`0x56daaf0`) + the ring slot index, re-run, and see which differs ONLY on the 8
flagged db054. OR instrument the actual blit: capture the cell index the
walker passes to the sprite draw on both sides (port d3d-trace / a draw probe;
retail Frida on the walker) over the window and diff. Whichever field/slot differs
exactly on `{42,50,51,59,60,68,69,84}` is the render-side cell source to fix.

**Don't stop at the pixel diff — confirm in the CODE (user directive 2026-06-04):**
for the flagged frames, log the actual walk-cell index the rendering code
*computes* on BOTH sides at the same sim frame (not just the record field), and
**trace the whole code path** from the actor record → the walker draw → the cell
passed to the sprite blit. The pixel diff says WHICH frames diverge; the code
trace says WHERE in the path port and retail compute a different cell. Pin the
exact instruction/branch (port d3d-trace / draw probe + retail Frida on
`FUN_004552d0`/`FUN_00456f56`) so the fix targets the real divergence, not a
guess. The visual detector (`recette_anim_probe.py`) and the code trace are
complementary: use both.

Cross-refs: `docs/phase-debugging.md`, `tools/phase_probe.py`,
`docs/findings/scene1-tear-visual-diffs.md`, [[reference_phase_probe_tool]],
[[project_confirmed_parity_ledger]]. Deferred sibling divergences (separate
session, user): background-window NPCs, foot-dust/smoke, Tear's wing particles.
