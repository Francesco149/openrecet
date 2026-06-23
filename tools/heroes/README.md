# README hero shots

The screenshots in the top-level README (`docs/img/hero-*.png`) are regenerated
from here — no hunting for traces or frame numbers. Each hero is **one OpenRecet
screenshot** (no side-by-side: the port is 1:1 on what we showcase, so a single
faithful frame is the point).

## Regenerate

```sh
# full rebuild from the committed scenarios: (re)captures every shot's source,
# then writes all heroes. PORT-only — no Frida / retail host needed.
nix develop --command python3 tools/heroes/regen-heroes.py --rerun

# fast recompose from the existing runs/caches (no capture) — only works while
# this session's runs/ (and runs/studio-v3-cache/) artefacts are still around:
nix develop --command python3 tools/heroes/regen-heroes.py

# one shot, pushed to the live feed to eyeball:
nix develop --command python3 tools/heroes/regen-heroes.py --shot hero-haggle --push
```

To **tweak** a shot, edit its entry in `heroes.yaml` (a different `cap`/`frame`,
add a `zoom` crop) and re-run. To **add** a shot, append a new entry (pick a
`source`, below) and re-run.

## The two frame sources

Each shot's `source:` picks where the pixels come from:

- **`scenario`** (default, *faithful*) — drives `scenario-test.py <scenario>
  --target openrecet` (the port only, no Frida) and reads the real 1024×768
  framebuffer frame. Frame chosen by `cap` (anchor-relative index into
  `run.json` `captured_frames`, stable across load-frame jitter) or an explicit
  `frame`. The scenario needs discrete `{capture}` ops. **Use for anything that
  must be pixel-faithful** — god-rays, lighting, the HUD, transparency/overlay
  effects (e.g. the iv1_2 spell-circle).

- **`v3shot`** (fast, cache-backed) — renders via `orv3_shot.py <scenario>:port
  --frame N`, replaying the Trace Studio v3 captured d3d stream from
  `runs/studio-v3-cache/`. No re-drive when a cache exists; `--rerun` rebuilds
  the **port** cache (no Frida) with `port_capture.py` over the scenario's
  `{caprange}` (sliced by `window:`). **Caveat:** the v3 proxy doesn't capture
  `SetRenderTarget`/`CopyRects` yet, so render-target effects (pause/menu
  captured-screen backdrops, radial-blur transitions, post-fx) render
  **empty/black** — don't pick an RT-effect frame here; use `source: scenario`.

## What's persisted (committed)

Everything needed to regenerate the heroes from a clean checkout:

- **`heroes.yaml`** — the recipe: per shot, its `source`, `scenario`, and the
  deterministic `cap`/`frame` (+ optional `zoom`, and `window` for v3shot).
- **`tests/scenarios/<scenario>/`** — the deterministic input traces
  (`scenario.yaml` + `trace.jsonl`), with their `{capture}`/`{caprange}` ops and
  phase/RNG pins: `intro-dialogue-lines`, `house-idle-npc-drift`,
  `house-customer-tutorial`.

The `runs/` and `runs/studio-v3-cache/` artefacts are an *ephemeral,
gitignored convenience*; `--rerun` recreates them from the committed scenario.

## The shots

| hero | source | scenario | beat | shows |
|---|---|---|---|---|
| `hero-iv1_2-dialogue` | scenario | `intro-dialogue-lines` | cap 18 | iv1_2 "Patience, Recette…" line: dialogue box, both standees, spell-circle FX, top HUD over the live map (README top hero) |
| `hero-house-freeroam` | scenario | `house-idle-npc-drift` | cap 0 | HOUSE free-roam: the 3D shop (geometry/lighting/god-rays), Recette idle, top HUD, Change-Camera hint, back-window NPC |
| `hero-haggle` | v3shot | `house-customer-tutorial` | frame 2920 | in-shop price-haggle UI: the "Bargain!" gauge, item base price, customer, Item-Details hint |
