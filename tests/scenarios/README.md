# Phase A scenarios — input-trace replay + golden frame diff

Each subdirectory is one regression scenario. Layout:

    <name>/
        scenario.yaml      committed; capture_frames + rng_seed + budget
        trace.jsonl        committed; sparse input trace (see src/input_trace.h)
        golden/            gitignored; locally regenerated with --bless
            frame_NNNNN.bmp
            audio.jsonl

The `golden/` directory contains BMPs of the rendered title screen
(and later, other scenes). Those frames embed vendor textures
(RECETTEAR logo, BG art) — checking them in would redistribute
copyrighted asset bytes. So `golden/` is gitignored; first time you
clone, run `--bless` to generate it from your own copy of the game:

    nix develop --command python3 tools/scenario-test.py <name> --bless

After that, normal runs diff bit-exact against the local golden and
fail loudly on any mismatch:

    nix develop --command python3 tools/scenario-test.py             # all
    nix develop --command python3 tools/scenario-test.py boot-idle   # one

When a code change intentionally moves pixels (e.g. a render-path
port), re-bless the affected scenarios and review the resulting BMP
diff before committing the new trace metadata.

Bit-exact diff is fine here because:
- input is fully deterministic (replayed from trace.jsonl)
- RNG is pinned (`rng_seed` in scenario.yaml)
- tick clock is virtual (20 ms per loop iteration, no Sleep)
- WM_ACTIVATE pause is pinned off

Cross-host portability: the harness was tuned on a Windows host
running under WSLInterop. Different GPU drivers may produce slightly
different pixels — re-bless after switching hosts.

## Adding a scenario

1. `mkdir tests/scenarios/<name>/` and write `scenario.yaml` +
   `trace.jsonl`. Defaults work for most cases (60 frames, seed 1).
2. `python3 tools/scenario-test.py <name> --bless` to generate goldens.
3. Eyeball the generated BMPs under `<name>/golden/` — make sure the
   scene actually shows what you wanted at each capture frame.
4. Commit `scenario.yaml` and `trace.jsonl` only. `golden/` is local.
