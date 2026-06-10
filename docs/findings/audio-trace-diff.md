# Audio-trace diff — detecting sound divergences from traces

**Goal:** tell whether the port triggers the same sound effects / BGM as retail
for a given scenario, *from the captured traces alone* — no need to boot the
port and listen. Mirrors the d3d-trace pipeline (`render_diff.py`) but for the
audio path.

This is how we catch gaps like "the whole item-display interaction is silent in
the port": retail's trace shows the SE/voice triggers, the port's shows none →
the diff lists exactly what's missing.

Related: `audio-backend.md` (the DirectMusic backend itself, port + retail),
`engine-quirks.md` §45/§46/§88 (SE table quirks).

## The three layers (cloned from d3d-trace)

| Layer            | Port                                   | Retail (Frida)                          |
|------------------|----------------------------------------|-----------------------------------------|
| **emit**         | `src/audio.c` `audio_trace_emit_*` (`--audio-trace <path>`) | `tools/frida/openrecet-agent.js` `installAudioHooks()` |
| **transport**    | direct JSONL write (fflush per line)   | `send({kind})` → `tools/frida_capture.py` writes `audio.jsonl` |
| **diff**         | `tools/audio_diff.py` (identity+count) — both sides |

### What gets hooked

Both sides trace the engine's three sound-trigger chokepoints — the SAME
functions the port ports, so the comparison is apples-to-apples:

| Event       | Port function          | Retail `FUN_`        | Record                                   |
|-------------|------------------------|----------------------|------------------------------------------|
| BGM swap    | `audio_play_track`     | `FUN_00499200` 0x499200 | `{kind:"bgm_swap", track}`            |
| resource SE | `audio_play_se`        | `FUN_00499c63` 0x499c63 | `{kind:"se_play", slot, name:"se_NNN_idXXXX"}` |
| voice/file SE | `audio_play_se_file` | `FUN_0049933c` 0x49933c | `{kind:"se_play", slot:-1, name:path}` |

- `bgm_swap` is **deduped on both sides** — emitted only on an *actual* track
  change. The port emits after its `current_track` guard; the agent reads
  `DAT_005d1960` (current track) at the hook entry and skips same-track calls.
- Resource-SE `name` is the port's `se_%03d_id%04x` label. The agent rebuilds
  it by reading the resource id from `DAT_005d1584[slot]` (8-byte stride), so
  both sides print identically.
- The retail `--silent-audio` capture default does NOT suppress these events
  (it only clamps `SetVolume`), so a capture records what *would* play.

### Record schema (`audio.jsonl`, one JSON object per line)

```
{"t_ms":U,"frame":N,"kind":"bgm_swap","track":N,"name":"bgm/town.wav"}
{"t_ms":U,"frame":N,"kind":"se_play","slot":N,"name":"se_012_id0148"}
{"t_ms":U,"frame":N,"kind":"se_play","slot":-1,"name":"bin/se/.../tea.bin"}
{"t_ms":U,"frame":N,"kind":"fade_start","channel":C,"slider":S,"centibel":V}  (port-only)
```

- `frame` — engine frame index. Port: `g_tick.frame_count` via
  `audio_trace_set_frame()`, called per-tick from the main loop next to
  `d3d_trace_begin_frame` (`src/main.c`). Retail: the agent's manual frame
  counter. **Added 2026-06-10** — before that the port stamped only `t_ms`.
- `t_ms` — `timeGetTime()`-since-trace-open (port) / `Date.now()` delta (retail).
  Wall-clock; not used for alignment.
- `fade_start` is a port-only volume-apply side effect; the diff ignores it
  unless `--include-fades`.

## Why the diff aligns by identity+count, NOT by frame

Both sides stamp `frame`, but you **cannot** align audio by frame number,
because the absolute frame ORIGINS differ and the offset is **not constant**
across a trace that spans a load. Concretely, in `item-display-2` the retail
trace fires title-menu SEs at abs frames ~68/148 but the in-house SEs at abs
~14616+ — retail plays a ~14k-frame load/intro the port skips. So the pre-load
port↔retail skew (~tens of frames) differs from the post-load skew
(~thousands). A single offset can't normalize it; correct per-event frame
alignment needs the trace-studio load-aware coordinate transform
(`model/segments.py cap_index_of_abs`), label space.

`audio_diff.py` sidesteps that entirely: it groups each side by **what plays**
and compares **trigger counts**:

- identity `("bgm", track)` / `("se", slot)` / `("se_file", path)`
- retail count > port count → **MISSING-IN-PORT** (the deficit)
- port count > retail count → **EXTRA-IN-PORT**
- equal → matched

This is immune to phase/load skew and answers the real question directly
("which sounds is the port missing, and how many times"). Frames are reported
as per-side context only.

**Limitation (known):** count-matching can't catch a sound that plays the right
number of times but at the *wrong moment* (a timing drift). That needs
label-space alignment and is a future layer — fold the trace-studio abs→label
transform into the diff, then per-event timing becomes meaningful. For the
current front (a *silent* interaction) count is exactly right.

## Running it

```sh
# from a trace-studio session (resolves retail/ + port/ audio.jsonl):
nix develop --command tools/audio_diff.py --session item-display-2

# explicit files (e.g. a scenario-test --target both run):
nix develop --command tools/audio_diff.py \
    --retail <run>/retail/audio.jsonl --port <run>/port/audio.jsonl

# machine-readable summary (trace_studio ingest):
tools/audio_diff.py --session item-display-2 --summary-json out.json
```

Exit code: 0 aligned · 1 divergence · 2 structural error.

### Where the traces come from

- **trace-studio sessions** (the core loop): `export_trace` passes
  `--audio-trace → port/audio.jsonl`; `frida_capture` writes `retail/audio.jsonl`.
  Both are produced automatically on every `capture`/`recapture`.
- **scenario-test** `--target both`: already passes `--audio-trace` to the port
  and collects `audio.jsonl` per target.
