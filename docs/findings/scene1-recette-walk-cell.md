# Recette's RENDERED walk cell — RESOLVED: the cell is 1:1, the "divergence" is foot-dust

**Status: RESOLVED 2026-06-04 (autonomous follow-up). The lead's premise was
WRONG — there is no render-side walk-cell divergence.** The drawn player cell IS
a pure function of the bit-exact anim record, and it matches retail. The
`recette_anim_probe` detector was measuring the **foot-dust** (records-A 0xe, the
already-deferred sibling) drifting over the body crop, not a walk cell.

## What the lead claimed (and why it was wrong)

The earlier lead (commit dc15080) said: the player anim *record* counters
(`ANIM/COUNTER/FRAME/FACING` at `&DAT_056daae8[0]`) are bit-exact 1:1 vs retail,
yet on ~18% of `house-walk-down-dense` frames Recette is drawn on a *different
walk cell*, so "the DRAWN cell is sourced from state the record-watch misses."

That last step does not hold. Tracing the actual draw (user directive: log the
cell the code computes, trace the whole path) shows the cell is a **pure function
of the watched record** — so if the record matches, the cell matches. The pixel
difference the detector flagged comes from **outside the sprite**.

## The code path (FUN_004552d0 → FUN_0045a56f), confirmed by disasm

The free-roam player is drawn by the shop-walker dispatch `FUN_004552d0`
(port: `scene1_shop_walker.c`), which at its player loop calls
`FUN_0045a56f(&DAT_056daae8 + actor*0xb, char_id, …)` (port:
`scene1_chr_sprite_render` → `chr_sprite_build_quads`). The cell index is computed
at the **top of FUN_0045a56f** (objdump @ `0x45a58f–0x45a5b6`):

```
ecx = DAT_005c5a54[ param_1[6] ]                 ; facing → within-frame bank base
eax = (char_id*0x359 + param_1[4]) * 6           ; (char*stride + FRAME)*6
eax = ecx + eax*2 + (param_1[0] << 8)            ; + ANIM*0x100
edi = DAT_0438cee0[ eax ]                         ; <-- iVar14 = the drawn cell
```

So `cell = f(ANIM=param_1[0], FRAME=param_1[4], FACING=param_1[6], char_id)` only
— exactly the fields `phase_probe` watches (all bit-exact) plus a constant
`char_id`. The port's `chr_meta_lut(char_id, anim, frame, bank)` reproduces this
(per-char block stride `0x5058 B = 0x1416 dw = 0x359*6 dw`; bank =
`DAT_005c5a54[facing]-0x16`). **No unwatched state (STATE 0x56daafc, TIMER
0x56daaf0, the FUN_0048b850 ring) feeds the cell.** The lead's three candidate
"hidden cell sources" are all irrelevant to the cell.

## The decisive measurement (cached `runs/phase-probe/house-walk-down-dense`)

`house-walk-down-dense` drives Recette to **walk in place against a wall**
(`px=-0.300 py=0.000 pz=9.500`, `vz=0.1435`, constant). So the walk cycle
advances while her world position is fixed — which gives a clean controlled test:

`db054=42` (flagged DIV, 18% top-half) and `db054=78` (aligned) have a
**byte-identical player record** (`anim1 / aframe3 / oct4 / cnt28`) **and
identical position**. The player sprite is therefore rendered pixel-identically
on both frames on each side. Yet the detector flags 42 and not 78 → the flag
cannot be the sprite.

Numeric proof on dust-free vs dust-prone patches (mean |Δ|/px, port↔retail):

| patch | port f42-vs-f78 | retail f42-vs-f78 | PvR @ f42 | PvR @ f78 |
|---|---|---|---|---|
| **FACE** (solid, dust-free at f78) | **0.00** | 15.94 | 15.94 | **0.00** |
| DRESS-core | 10.72 | 28.41 | 31.71 | **0.00** |

- `FACE port f42-vs-f78 = 0.00`: the port draws the face **pixel-identically**
  for the identical record (cell = pure function ✓).
- `FACE PvR @ f78 = 0.00`: when no dust intervenes, the **port face == retail
  face bit-exactly** — a clean 1:1 confirmation of the walk cell.
- `FACE PvR @ f42 = 15.94 = retail f42-vs-f78`: the entire frame-42 "divergence"
  is **retail's own** frame-to-frame change — a semi-transparent dust mote over
  the face at f42 that is gone by f78. The port's dust (different RNG/timing)
  didn't cover that spot that frame.

Whole-crop self-diffs corroborate: `port f42-vs-f78 = 13.3%`, `retail = 32.6%`
strong-diff — **both sides vary frame-to-frame at identical player state**,
i.e. the variation is dynamic surroundings, not the cell.

Visual (feed montage 2026-06-04, + `/tmp/walkcell_{body,head,legs,feet}.png`):
body / dress / feet **silhouettes are identical** at 8–14×; the diff is a soft
**dust cloud** at the feet/lower dress (different position/density per side) that
puffs **up into the torso and over the head** — that upward dust is the "arc"
the top-half metric caught. The user's "legs a different walk step" read was the
dust cloud partly obscuring the dress hem, not a different cell.

## Consequences

- **Recette's drawn walk cell is CONFIRMED 1:1** (not just the record counters —
  the rendered sprite too, since it is a pure function of the matching record).
  Ledger updated accordingly.
- The remaining `house-walk-down-dense` body-crop divergence is the
  **foot-dust** (records-A 0xe) — the explicitly **deferred** sibling
  ([[project_freeroam_smoke_effect]]; separate session per user). Nothing new to
  fix here.
- `tools/recette_anim_probe.py` measures dust, **not** walk cells — its top-half
  box does not actually avoid the dust (puffs rise into the torso/head). It is
  annotated as such (re-scoped to a documentation/repro tool); do not treat its
  flags as cell divergences.

## Secondary (sub-threshold, also dust): character tint

A small per-frame dress-tint wobble (sum|Δ|≤24, below the detector's >30
threshold) tracks the dust too — the frame-42 port dress shows a green/tan bump
(G 83→94) = translucent dust over the red dress, not a missing colour pulse.
FUN_004552d0 L397–435 *does* compute a `db054`/`sin`-driven player-only colour
(actor 0), passed as the colour arg to FUN_0045a56f; the port draws flat
`0xff808080`. That path is a real (minor) candidate gap but is **not** what
caused the flagged frames here — leave as a separate low-priority item, do not
conflate with the (closed) walk-cell question.

Cross-refs: `docs/phase-debugging.md`, `tools/phase_probe.py`,
`docs/decompiled/by-address/{4552d0,45a56f}.c`, [[reference_phase_probe_tool]],
[[project_confirmed_parity_ledger]], [[project_freeroam_smoke_effect]].
