# Pixels pillar PRODUCER — real per-frame differ verdict (2026-07-16)

> Wave-0 follow-on of `docs/plans/parity-evidence-roadmap.md` §6 (EP-04/EP-05). The
> `pixels` adapter (`tools/parity/pixels.py`) shipped format-only ("a real headless
> producer … is wired in a later package"); THIS is that producer. M0's last
> UNPROVEN required pillar on `house-firstcust-arrprobe` now gets a REAL verdict.
> Vocabulary: `docs/reference/parity-vocabulary.md`. Commit `8514b9d`.

## Why

The `pixels` proof pillar needs a per-frame differing-pixel measurement, not a source
marker. The adapter consumes a normalized `pixel-metrics.json` (one `differ` per
identity-joined frame) but nothing PRODUCED it ⇒ `pixels` was always `NOT_CAPTURED`.
`house-firstcust-arrprobe`'s bundle was `identity PASS · render_program FAIL · pixels
NOT_CAPTURED` — the last required pillar with no evidence.

## What landed

**`replay.exe --render-dump <wanted.txt> <outdir>`** — a resident RGB dump for the
producer (mirrors `--verify-hashes`, but writes pixels instead of hashing them).
Each listed kept frame → `<outdir>/f<NNN>.raw` (8-byte `[w,h]` u32 header + BGRA, the
`v3ref`/`read_raw_rgb` format). **RT-correct:**

- `has_rt` container ⇒ render frames `0..max(wanted)` IN ORDER on the resident device
  WITHOUT reset, so cross-frame render-target content accumulates (pause backdrop,
  transitions) — same rule as `verify_hashes`/`render_history` — dumping only the
  wanted frames. (Per-frame isolated `--upto` spawns render each frame on a FRESH
  device ⇒ any RT filled in an earlier frame reads black ⇒ wrong pixels. That trap is
  why the producer must NOT use `--upto`.)
- RT-free container ⇒ render the wanted frames directly (each frame's
  Clear+preamble overwrites the prior ⇒ isolation is exact), no warm-up sweep.

One process per side; no per-frame spawn, no GB of raw dumps for the whole capture
(only the contract-window frames are rendered).

**`tools/parity/pixel_producer.py`** — split so the truth-defining core is testable
with NO Windows/replay dependency:

- `build_pixel_metrics(pairs, required, render_port, render_retail, source, mode)` —
  pure. Per required frame: render both sides, `differ = pixel_diff.amplified_diff`
  (retail=A/ground-truth, port=B — the project's ONE canonical differ metric),
  `total=H*W`, `meanabs`. Injected renderers ⇒ unit-tested.
- `wanted_and_map(pairs, required)` — pure: per-side kept-index work-lists + `lf→(p,r)`.
- `render_side_via_replay(container, wanted, out_dir)` — the `--render-dump` driver.
- `produce_for_window(window_dir, required)` — reads `pairs.json` → containers, stamps
  `source` = each `v3cap.bin` SHA-256 (**verified** equal to what `orv3_view` bakes
  into `view.json` ⇒ the EP-08/HOLE-2 provenance check PASSES on a re-driven window,
  not skipped), drives both sides, writes `<window_dir>/pixel-metrics.json`.

**FAIL CLOSED** — no render output / a port·retail dim mismatch / a required frame the
join never paired all RAISE; the producer never invents a `differ==0`.

**`tools/parity_pixels.py`** — producer CLI; two-step flow keeps the heavy serialized
retail drive in `orv3_window`:

    orv3_window.py <scen> --window OFF:COUNT --view      # capture (retail once, cached)
    parity_pixels.py <scen> --window OFF:COUNT           # → pixel-metrics.json
    parity_prove.py <scen> --window OFF:COUNT --env-json … --json   # pixels now real

**`tools/test_parity_pixels.py`** — 24 checks (faithful→PASS, one-pixel disproof
localizes, fail-closed on missing/mismatched/unpaired, provenance stamping, raw
round-trip). Parity suite `24+52+60` green.

## Result — a truthful FAIL, reconciled

`parity_pixels.py house-firstcust-arrprobe --window 0:1500` (contract join
`HOUSE_FREEROAM#1 [1,80]`, `mode: exact`): **80/80 frames differ.** The proof is now
**`identity PASS · render_program FAIL · pixels FAIL`** (first div @
`HOUSE_FREEROAM#1+1`, exit 1). Honest: this is our most human-confirmed-1:1 scene, yet
NOT bit-exact.

Reconciled `differ (>0)` vs the viewer's `gt8 (>8/ch)` on sample frames — the
producer is CORRECT (pairing right: `Pmean≈Rmean` every frame; no channel swap):

| offset | port#/retail# | differ(>0) | gt8 | maxd | meanabs | note |
|---|---|---|---|---|---|---|
| 2  | 1 / 2   | 517046 | 3 | 13 | 0.408 | near-BLACK fade frame (mean ~14): a global ±1 fade-ramp Δ lights the whole `>0` count |
| 40 | 39 / 40 | 3808   | 3 | 101| 0.002 | settled bright scene (~104): ~0.5% px differ by ±1 |
| 79 | 78 / 79 | 4860   | 5 | 99 | 0.003 | " |

So the scene is **visually 1:1** (`gt8` 3–5 px/frame — matches FRONT's "2–3 px
accepted residual", a `gt8`/eyeball figure) but **NOT bit-exact**: ±1 sub-perceptual
cross-target noise (`meanabs ≪ 1`), consistent with `render_program` FAIL (the command
streams differ ⇒ the pixels can't be bit-identical). The near-black off=2 fade frame
dominates the `differ` count because a whole dark screen at brightness ~14 differing
by ±1 is 66% of pixels while being invisible.

**Takeaway:** `mode: exact` = strict bit equality is the honest, correct gate here and
it legitimately FAILs cross-target. A future contract wanting "visually 1:1" would need
an R3-approved threshold mode (e.g. `gt8`) or a crop/mask — a schema extension, not a
comparator's silent tolerance (roadmap §3 rule 9). Not added now.

## Follow-ups (non-blocking)

- The arrprobe window is pre-EP08 (its `view.json` lacks baked container hashes) ⇒ the
  pixel/render **provenance is caveated (skipped), not verified** — the FRONT-noted
  one-time state that resolves on the next `orv3_window … --view` (which re-bakes the
  hashes; the producer's `source` already matches by construction).
- Next per the roadmap: **ST-00/ST-01** (canonical state model + save equality, M1).
