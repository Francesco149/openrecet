# Autonomous-session task queue — picked 2026-05-21

> Picked for a ~1-hour unattended session. Read in order. Each task has
> acceptance criteria the assistant can verify itself (no user
> screenshot / audio judgement needed). Stop where you stop; don't try
> to cram every item.
>
> Standing rules during autonomy:
> - Commit in logical units as you go (see `AGENT-WORKFLOW.md`).
> - Co-author trailer: `Claude Opus 4.7 (1M context) <noreply@anthropic.com>`.
> - Spawn Sonnet subagents for mechanical work; brief them with exact
>   signatures (see worktree-isolation note in `AGENT-WORKFLOW.md`).
> - Don't push / amend / skip hooks.
> - If a task gets stuck after one debug attempt, **leave a
>   `BLOCKED:` line in `PROGRESS.md` and move on** — do not flail.
> - Run `make -C tests run` before each commit that touches a decoder
>   or pure-C math.

## Decisions already made (do not relitigate)

- **SE WAV source:** Embed in our exe via windres at build time
  (matches engine's `FindResource` path; faithful to FUN_00498ef4
  resource loop).
- **Audio trace format:** JSONL via `--audio-trace <path>`, opt-in.
  Schema: `{"t_ms":<int>, "kind":<"bgm_swap"|"se_play"|"fade_start">,
  "name":<str>, ...kind-specific fields}`. Off by default.

## Task list (execute in order)

### 1. Per-pixel diff overlay (small, ~20 min)

Extend `tools/smoke-test.py::diff_runs` to also emit per-frame diff
overlays. For each `(golden, new)` pair, write
`runs/<...>/diff/frame_NNNNN.png` — the new frame as the base, with
pixels that differ from golden tinted red (clamp `|new - golden|`
across RGB, threshold ≥ 4, OR a red overlay at 50% alpha onto those
pixels). Then call `tools/contact-sheet.py --src <run>/diff/` to tile
into `runs/<...>/diff-overlay.png`.

**Acceptance:**
- Self-diff (a run vs itself) → all-zero red mask; overlay PNG is
  visually identical to the captures.
- Synthetic diff (write a script-only test that hand-modifies a few
  pixels in one frame) → red highlights exactly on the modified
  region.
- Smoke run vs the prior `runs/boot/openrecet-20260521T130752Z` →
  overlay PNG generates without errors; mean SSIM still ~0.999.

**Files:** `tools/smoke-test.py` only. No `src/` changes.

### 2. MCI debug bridge (small, ~15 min)

Port FUN_00451874 (47 bytes) as `src/audio_mci.{c,h}`. The function is
a bounded strncpy into a 2D char array at `DAT_06a47aac` (rows of
0x50=80 bytes each). The two ints `param_1`+`param_2` index the row.

**What to do:**
- Add `audio_mci_record_command(int channel, int row_index, const char *cmd)`.
- Mirror the engine's stop-on-NUL-or-80-bytes behavior exactly.
- Inspect callers via `tools/analyze/pe.py callers --target 0x00451874`
  to confirm the row-count (the global buffer's total size determines
  it). If callers indicate < 16 rows, hardcode that; otherwise file
  what was found in PROGRESS.md and pick a sensible default.
- Tests: `tests/test_audio_mci.c` covering NUL-early-exit, full-fill,
  and the 0x50 cap. Wire into `tests/Makefile`.

**Acceptance:** All new tests pass. `make -C tests run` reports green.
No `src/main.c` wiring needed — the function is gated on
`DAT_0438ccb4 != 0` (debug flag), which is zero in normal play.

### 3. Volume sin-curve fade (medium, ~30 min)

Port FUN_00499583 (231 bytes) + its callee FUN_00503994 (sin-curve
helper). The fade formula reads:
```
angle = (9 - frame_counter) * 1.2566371 / 9.0      // 1.2566371 = 2π/5 rad
vol   = FUN_00503994(angle, angle, target_volume)
```
FUN_00503994 takes `(angle_as_double, angle_as_float, target_vol)`
and returns the interpolated SetVolume argument. **First read
docs/decompiled/by-address/503994.c** to see what the actual
interpolation formula is — likely `sin(angle) * (target - silence) +
silence`, but confirm. The output is fed into
`IDirectMusicAudioPath8::SetVolume` (vtable +0x14).

**Module shape:**
- `src/audio_fade.{c,h}`
- `int32_t audio_fade_compute(int frame_counter_0_to_9, int32_t target_centibel);`
- `void audio_fade_apply(int channel)` — for now a stub that
  doesn't actually call SetVolume (no callers wired); just exercises
  the math.

**Tests (`tests/test_audio_fade.c`):**
- Frame 0 → silence (-10000 centibel).
- Frame 9 → target volume unchanged.
- Frames 1..8 → monotonically increasing values (assert each is
  strictly greater than the previous).
- Spot-check one intermediate value against a hand-computed reference.
- Render the 0..9 curve to `runs/audio-fade-curve.png` via
  `tools/plot/curve.py` (new tiny PIL-based helper, ~30 LOC). PNG
  goes in `.gitignore` (covered by `runs/`).

**Acceptance:** All new tests pass. `audio-fade-curve.png` exists and
shows a smooth rising curve.

### 4. `--audio-trace` JSONL emitter (small, ~25 min)

Add the opt-in audio event log per the locked decision above.

**Wiring:**
- `src/main.c`: parse `--audio-trace <path>` next to `--capture-to`.
  Open the file in append mode; pass the FILE* (or path) to
  `audio_init`.
- `src/audio.{c,h}`: `audio_trace_open(const char *path)` /
  `audio_trace_emit_bgm_swap(int track, const char *name)` /
  (placeholder for `_se_play` until task #5 lands).
- Emit timestamps as `timeGetTime()` ms since boot (matches the rest
  of the audio code's clock).
- `tools/smoke-test.py`: pass `--audio-trace
  <run_dir>/audio-trace.jsonl` when a new CLI flag `--audio-trace` is
  set on the smoke harness.

**Acceptance:**
- `./build/openrecet-debug.exe --audio-trace /tmp/at.jsonl
  --max-duration-ms 3000` produces a JSONL file with at least one
  `bgm_swap` line (title music) and the line parses as valid JSON.
- New `tests/test_audio_trace.c` covers the JSON serializer
  (escape `"`, escape `\`, ASCII only — no need for full UTF-8;
  filenames are ASCII).

### 5. SE backend port (BIG, ~45 min once started)

Port FUN_00499c63 (SE trigger, 477 bytes) plus the SE half of audio
init (the second half of FUN_00498ef4 that creates 2 SE audio paths
and loads 27 RT_RCDATA WAVs).

**Sub-steps (commit each one):**

a. **Resource extraction & embedding** (~15 min).
   - Write `tools/extract/se-wavs.py`: use pe.py to enumerate the
     RT_RCDATA resources in `vendor/unpacked/recettear.unpacked.exe`,
     emit them as `vendor/unpacked/se-extracted/NNN.wav` (gitignored).
   - Generate `src/se_resources.rc` listing all 27 with their original
     resource IDs (the engine looks them up by ID, see
     `498ef4.c` L91 `FindResourceA(NULL, (LPCSTR)(uint)*local_8, ...)`
     — `local_8` walks the ID table at `&DAT_005d1584`).
   - Add `windres` invocation to `src/Makefile`.
   - SE name table extracted to `src/audio_se_names.h` (parallel to
     `audio_bgm_filenames[]`).

b. **SE init — 2 audio paths + 27 segment loads from resources**
   (~15 min). Mirror `498ef4.c` L62-122 inside `audio.c`:
   - Two `CreateStandardAudioPath` calls (DMUS_APATH_DYNAMIC_3D? confirm
     against vtable). Pointers stashed at the new globals matching
     `DAT_0964310c` and `DAT_09643110`.
   - For each of 27 SE entries: build a DMUS_OBJECTDESC with the
     resource pointer + size, call `IDirectMusicLoader::GetObject`
     (+0xc) with the GUID at `&DAT_0051a0c0` (DMUS_Segment GUID), then
     `Download` onto the SE audio path.

c. **`audio_play_se(int channel)`** (~10 min). Port FUN_00499c63
   straight. The volume blend uses the fade curve from task #3 — if
   task #3 hasn't landed yet, call SetVolume with `target_centibel`
   directly (TODO comment).

d. **Wire `--audio-trace`'s `se_play` event** (~5 min). One emit at
   the top of `audio_play_se`.

**Acceptance:**
- Build clean, smoke run boots clean (no SE callers yet, so audibly
  identical).
- New test `test_audio_se_table.c` verifies the 27-entry name table
  + resource ID list against extracted truth.
- Manual probe: hand-call `audio_play_se(0)` from somewhere temporary
  (NOT committed) and confirm via `--audio-trace` JSONL that the
  se_play event fires. Remove the probe before commit.

### 6. Audio-backend doc refresh (small, ~10 min)

Update `docs/findings/audio-backend.md` to cover:
- Fade curve formula + the 9-frame ramp shape.
- SE audio path vtables + the 27-entry resource layout (from task #5).
- The `--audio-trace` JSONL schema (one-line definition + example).

**Acceptance:** Doc lines up with `src/audio*.c` as of latest
commits; cross-refs to FUN_004995xx all resolve.

## End-of-session policy

When the timer runs out (or after task #6 lands):
1. Write a fresh `PROGRESS.md` entry summarizing what landed and
   what's still open from this queue.
2. If anything blocked, the `BLOCKED:` line you wrote earlier should
   already be in PROGRESS — confirm the diagnosis briefly.
3. Don't try to "finalize" half-done work. Either commit a
   well-bounded slice or revert the WIP cleanly.
4. Drop a one-line note into the session-starter memory updating
   "current state" so the next session boots with accurate context.
