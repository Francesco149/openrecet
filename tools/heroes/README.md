# README hero shots

The labelled `OpenRecet | Retail` comparison images in the top-level README
(`docs/img/*.png`) are regenerated from here — no hunting for traces or frame
numbers.

## Regenerate

```sh
# full rebuild from the committed scenarios: captures port + retail fresh,
# then composes every hero. Needs the Frida retail host (cutestation.soy:27042).
nix develop --command python3 tools/heroes/regen-heroes.py --rerun

# fast recompose from the latest existing both-runs (no capture) — only works
# while this session's runs/ dirs are still around:
nix develop --command python3 tools/heroes/regen-heroes.py

# one shot, pushed to the live feed to eyeball:
nix develop --command python3 tools/heroes/regen-heroes.py --shot house-comparison --push
```

## What's persisted (committed)

Everything needed to regenerate the heroes from a clean checkout:

- **`heroes.yaml`** — the recipe: for each shot, its `scenario`, the
  anchor-relative `cap` index, optional `zoom` crop, and `show_fps`.
- **`tests/scenarios/<scenario>/`** — the deterministic input traces
  (`scenario.yaml` + `trace.jsonl`) the shots are captured from:
  `house-idle`, `intro-sigh`, `intro-iv2-gap`, `intro-dialogue-lines`.

Frame selection is by **cap index**, which is anchor-relative (absorbs
load-frame jitter), so the same index frames the same beat on every run.

The `run_dir:` pins in `heroes.yaml` are an *ephemeral convenience* (re-compose
without re-capturing); they live under gitignored `runs/`. When they're gone,
`regen-heroes.py` falls back to the newest matching both-run, and `--rerun`
recreates them from the committed scenario.

## The shots

| hero | scenario | cap | shows |
|---|---|---|---|
| `house-comparison` | `house-idle` | 2 | HOUSE free-roam: Recette idle, 3 back-window NPCs, top HUD, Change-Camera hint, FPS |
| `hero-iv1_1-sigh` | `intro-sigh` | 2 | iv1_1 "sigh" (tameiki) effect sprite mid-fade, full-screen cutscene (no HUD) |
| `hero-iv1_2-pose` | `intro-iv2-gap` | 3 | iv1_2 opening: Recette look-up pose, zoomed onto her sprite |
| `hero-iv1_2-dialogue` | `intro-dialogue-lines` | 18 | iv1_2 dialogue line with the top HUD drawn over the live HOUSE map |
