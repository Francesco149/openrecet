# Recette's RENDERED walk cell — RESOLVED: cell is 1:1; the "divergence" was a CAPTURE-LABEL off-by-one

**Status: RESOLVED 2026-06-04 — walk cell is 1:1, HUMAN-CONFIRMED.** The user
confirmed "that capture is fully 1:1" on the capture-time-aligned turbo montage
(feed 2026-06-04). The real root cause was a **capture tooling bug**, not the engine:
the Frida agent read the `--watch` state (db054/aframe) at `input_poll` — **one
sim-tick before the frame was rendered** — so every retail screenshot was labeled
with the prior tick's state. Aligned by that off-by-one label, port↔retail screenshots
mismatched at every walk-cycle transition (turbo's sim/Present decoupling made it
run-to-run variable), and the "counter-wrap residual" was the same off-by-one. Fixed
by reading the state at the CAPTURE instant (Present onEnter) → `frames_meta.jsonl`
(commit 715b74c). With capture-time alignment the SAME turbo capture is bit-1:1 with
the port (arms-diff ≤1.4, many 0.00) on every frame including the wraps.

> ## ⚠️ The 50ae50a mistake (kept as a lesson)
>
> A first autonomous pass (commit `50ae50a`, **REVERTED**) closed this as "the cell
> is 1:1, the divergence is just foot-dust." That reasoning was WRONG (the divergence
> was in the arms/dress, not dust) even though the *conclusion* (cell is 1:1) happened
> to be right for the wrong reason. The lesson stands: **do not assert 1:1 without
> explicit human verification AND a proven mechanism.** This close has both — the user
> confirmed the capture-time-aligned montage, and the mechanism (capture-label
> off-by-one) is proven by the fix making port↔retail bit-match.
>
> The original eyeball evidence (feed `20260604T042804_860e`): on flagged frame
> **db054=42** the port draws a
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

## 2026-06-04 PM re-investigation — measured findings (NOT yet resolved)

Worked entirely from the cached `runs/phase-probe/house-walk-down-dense` capture
plus a fresh 250-frame extension. Everything below is data, not eyeball.

**The anim RECORD is bit-exact every frame.** Port vs retail `(anim,counter,aframe,oct)`
match on ALL frames db054 40–84 (and the 250-frame run: `p.aframe` ALIGNED). And
`FUN_0045a56f` computes the drawn cell *directly* from the watched `aframe`
(`param_1[4]` = 0x56daaf8). So the cell SHOULD be a pure function of the matching
record — yet the rendered crop diverges on ~18% of frames.

**The divergence is real and in the ARMS/DRESS, not dust.** Per-band mean|Δ| on a
matching frame: head 0.0, arms 0.0, dress ~3, feet ~12 (feet = the dust floor). On
a flagged frame: head ~6, arms ~26, dress ~23, feet ~15. The arms/dress light up;
the feet (dust) are the SAME on matched and flagged frames. So the reverted
"it's just foot-dust" claim is **wrong** — confirmed by the user's eye too
(arm bent up / boot forward).

**But it is NOT a fixed wrong-cell bug — it splits into three effects:**

1. **Capture-sync JITTER (±1 frame) at cycle transitions.** The flagged frames
   *move between two runs of the same scenario*: the 45-frame run flagged db054
   {42,51,60,69}; the 250-frame run flagged {41} and cleared {42,51,60,69}. A
   deterministic engine cell bug would flag the SAME frames every run. Shifting
   flags = the port and retail *screenshot streams* are ±1 frame out of phase at
   walk-cycle transitions (invisible on plateaus, a 1-cell mismatch at edges).

2. **A STABLE 1-frame component at the counter-WRAP frames.** db054 {50,59,68}
   (last frame of each aframe plateau, counter at max, about to wrap) diverge in
   BOTH runs. This is the candidate *real* signal: on the wrap frame retail's
   drawn cell may advance to the next aframe while the port's doesn't (or vice
   versa) — a draw-vs-anim-tick ORDER difference of 1 frame. Entangled with (1),
   so not yet proven.

3. **A cumulative RNG desync from db054≈88.** `phase_probe` VERDICT = LOGIC DRIFT:
   `rngcalls` DESYNC at db054=88 (port consumes +1 RNG call), after which the
   crop diverges continuously (88 frames straight in the 250-run). This is the
   known free-roam RNG over-consumption ([[scene1-rng-stream-parity]]),
   garbling all *late* frames — separate from the walk cell.

**Decisive test still owed (do NOT close without it):** instrument the actual
walk-cell INDEX the code computes on BOTH sides — port: log `cell`/`aframe`-at-draw
in `chr_sprite_build_quads` keyed by db054; retail: Frida hook `FUN_0045a56f`
@0x45a5b6 logging `param_1[4]` + `iVar14` per call — and diff by db054. That
removes ALL capture-sync ambiguity: if the computed cell indices match every
frame, the cell IS 1:1 and the pixel blips are capture timing (effects 1+2 are
tooling); if they differ at wrap frames, effect 2 is a real engine draw/tick-order
bug to fix. **Until that test runs, the walk cell stays OPEN / NOT-confirmed.**

## 2026-06-04 PM — NO-TURBO test (user-directed) — walk cell matches; turbo was the artifact

User wrote down retail's ground-truth walk sequence (RF → Lf-slight → LF → Rf-slight,
repeating cleanly into the wall) and confirmed the PORT follows it; predicted the
divergence was a retail-side **turbo** capture artifact. Re-captured retail with
`--turbo` OFF (`frida_capture … --duration-ms 120000`, no turbo) and compared to the
port keyed by walk phase `(aframe,counter)` (db054 isn't reset without the phasepin,
so phase is the universal key):

| comparison | result |
|---|---|
| port vs **NO-TURBO** retail | **arms-diff 0.00 (bit-identical) on 31/36 walk frames** |
| port vs **TURBO** retail | diverges on every phase (capture frame↔phase mapping unreliable) |
| residual (both turbo + no-turbo) | only the 4 **counter-wrap** frames cnt 9/18/27/36 (~22–28) |

So the big "different walk cell" divergence **was a turbo-side capture jitter** — the
screenshot stream slips ±1 frame at cycle transitions, which a fixed-crop diff reads
as a different cell. With turbo off the walk poses are pixel-identical. (Also confirms
the records were right all along: bit-exact, no walk reset, inputs registered every
frame — the input-drop and RNG hypotheses are both out; RNG doesn't pick the cell.)

**Remaining real residual (small):** the last frame of each aframe plateau (counter
at max, about to wrap) still diverges WITHOUT turbo — a 1-frame draw-vs-anim-tick
order effect at the wrap, or a sub-frame capture effect. Localized to 4 frames/cycle.
Tracked here; not the headline divergence.

**1-minute no-turbo walk (user-directed drift test, 2026-06-04):** captured retail
no-turbo over ~1 min of walking-in-place (trace `house-walk-1min-noturbo`, two 200-frame
windows: start + ~58s in), diffed vs the port reference by walk phase (af,cnt):

| window | divergent | (af,cnt) | run-len | magnitude |
|---|---|---|---|---|
| EARLY (start) | 22/200 | {(0,9),(1,18),(2,27),(3,36)} | all **1** | 23–28 |
| LATE (~58s in) | 22/200 | {(0,9),(1,18),(2,27),(3,36)} | all **1** | 23–28 |

Identical early vs late → the wrap-frame lag is **fixed at exactly 1 frame, does NOT
drift/accumulate** over a minute. The 4 wrap frames per cycle are the ONLY divergence;
everything else is bit-black. Feed montage 2026-06-04 (EARLY vs LATE cycle).

**Both follow-ups RESOLVED 2026-06-04 — same root cause:**
1. ~~Fix the wrap-frame 1-frame lag~~ — was NOT an engine bug. Same capture-label
   off-by-one: the watch was read one tick before the render, so the wrap frame (where
   aframe advances) was mislabeled. Vanishes with capture-time alignment.
2. ~~Make turbo capture not jitter~~ — **DONE (715b74c):** the Frida agent now reads the
   watched state at the capture instant (Present onEnter) and writes it to
   `retail/frames_meta.jsonl`; `recette_anim_probe` (and any screenshot-diff consumer)
   aligns retail frames by that capture-time db054 instead of the per-tick `watch.jsonl`.
   The same turbo capture is then bit-1:1 with the port. (phase_probe's verdict diffs
   *records*, not screenshots, so it is unaffected and keeps using `watch.jsonl`.)

The `OPENRECET_CELL_LOG` port instrument (env-gated, logs the computed cell index per
draw) stays in `scene1_chr_sprite.c` as a debug tool — it proved the port's cell is a
deterministic pure function of the record, which let us localize the bug to capture
labeling rather than the engine.

**Status:** walk cell looks 1:1 on the plateau (no-turbo bit-exact + user ground-truth
+ bit-exact records + deterministic port cell-log). **Awaiting user confirmation on the
no-turbo montage before any ledger change** — per the standing rule, NOT self-closing.
Tooling follow-up: phase_probe/recette_anim_probe need turbo-jitter-robust frame↔phase
alignment (align screenshots by (aframe,counter), not raw db054) so this class of
capture artifact stops masquerading as a render divergence.

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
