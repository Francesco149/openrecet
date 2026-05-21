# Phase A + B scenarios — input-trace replay + golden frame diff

Each subdirectory is one regression scenario. Layout:

    <name>/
        scenario.yaml         committed; capture_frames + rng_seed + budget
        trace.jsonl           committed; sparse input trace (see src/input_trace.h)
        golden/               gitignored; locally regenerated with --bless
            frame_NNNNN.bmp
            audio.jsonl
        golden-retail/        gitignored; --target retail --bless populates
            frame_NNNNN.bmp
            audio.jsonl
            trace.jsonl       what the retail engine actually polled

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

## Phase B — retail capture via Frida

`--target retail` instruments `vendor/unpacked/recettear.unpacked.exe`
via a Frida agent (`tools/frida/openrecet-agent.js`) instead of running
our own port. Hooks: `IDirect3DDevice8::Present` for frames, the audio
entry points (`FUN_00499200` BGM swap, `FUN_00499c63` SE play), and
`FUN_0047b73c` input poll. Output schemas match Phase A so the bless /
diff path is shared.

One-time host setup:

1. Download `frida-server-<ver>-windows-x86_64.exe` from
   https://github.com/frida/frida/releases matching the Python `frida`
   version in your nix shell (`python3 -c 'import frida;
   print(frida.__version__)'`).
2. Rename → `frida-server.exe`, run as Administrator. Listens on
   127.0.0.1:27042 by default.

Then:

    nix develop --command python3 tools/scenario-test.py \
        boot-idle --target retail --bless

This populates `tests/scenarios/boot-idle/golden-retail/`. Retail BMPs
are **not** bit-comparable to openrecet BMPs (different draw call
ordering, font system, GPU pipeline state) — the retail golden is the
ground-truth reference for human / contact-sheet review, not a regression
gate. Phase B re-runs against retail golden DO use bit-exact diff so
retail's own determinism breaking is caught.

Open Phase B limitations:

- No input injection yet: retail's recorded `trace.jsonl` reflects what
  the engine polled (i.e. human keyboard input). The scenario's input
  `trace.jsonl` is unused under `--target retail`.
- No RNG / pause pinning. Cross-run determinism on retail relies on no
  external interaction. Re-bless after each capture session if you want
  a stable baseline.
