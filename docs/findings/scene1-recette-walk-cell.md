# Recette's RENDERED walk cell diverges from retail (render-side, not phase)

**Status: OPEN — DEFINITELY NOT 1:1 (human-verified twice). Found 2026-06-04 (user eyeball).**

> ## ⛔ DO NOT mark this 1:1 again without explicit human verification (2026-06-04)
>
> A prior autonomous pass (commit `50ae50a`, **REVERTED**) wrongly closed this as
> "the cell is 1:1, the divergence is just foot-dust." That was wrong. The user
> re-verified by eye on feed montage **`20260604T042804_860e`** (the very montage
> that pass pushed as "proof"): on the flagged frame **db054=42** the port draws a
> **genuinely different pose** — right hand **bent upward** (retail: hand
> horizontal/down) and **left boot forward** (retail: different stride). The
> white-diff panel lights up the **entire silhouette** (head outline, arm, torso,
> legs) — dust cannot move a head outline or bend an arm. The aligned frame
> db054=78 is near-black (truly 1:1) save a small foot puff.
>
> **Why the "just dust" reasoning was wrong:** the closing pass argued that f42 and
> f78 have a *byte-identical watched record* (`anim1/af3/oct4/cnt28`), so the port
> *must* draw an identical sprite at both, so the f42 flag *must* be external
> (dust). But look at the **PORT panels themselves**: port-f42 (arm up) ≠ port-f78
> (arm low). The port draws **different poses at f42 vs f78** despite the "same"
> watched record. **Therefore the drawn cell is NOT a pure function of the watched
> fields `{anim, aframe, oct, cnt}`.** There is an unwatched input feeding the cell
> (see "Sharpened lead" below). The disasm that "proved" purity used a field
> mapping that does not match what the probe calls `aframe`. The divergence is
> **real**; it is the original `dc15080` finding, restored.

## Sharpened lead (2026-06-04, post-revert) — the cell-indexing FRAME is unwatched

The reverted pass disassembled the draw `FUN_0045a56f` (@`0x45a58f–0x45a5b6`) as:

```
cell = DAT_0438cee0[ DAT_005c5a54[FACING] + (char_id*0x359 + FRAME)*6 + ANIM*0x100 ]
```

That algebra is fine, but it **proves the opposite of what the pass concluded**:
the cell is a pure function of `{ANIM=param_1[0], FRAME=param_1[4], FACING=param_1[6]}`.
If those matched AND the table matched, port-f42 and port-f78 would be identical —
but they are visibly different poses. So **at least one of those three inputs
differs between f42 and f78 yet is reported "same" by the probe.** The prime
suspect: the probe's **`aframe`** is NOT `param_1[4]` (the table-indexing FRAME).
There is a separate frame counter driving the cell that `phase_probe` does not
watch (or watches at the wrong address).

**Next concrete step (do this, don't theorize):** instrument the *computed cell
index* `iVar14` directly — log it port-side at the FUN_0045a56f draw, capture it
retail-side via Frida at `0x45a5b6`, over the db054 40–84 window, and diff. Then
back-solve which of `{ANIM, FRAME, FACING}` (and which memory address) actually
drives it. Confirm `param_1[4]`'s true address and check whether the probe is
watching it. The pixel montage says WHICH frames; the index log says WHY.

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
