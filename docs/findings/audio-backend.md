# Audio backend — DirectMusic 8

**Status (2026-05-21):**
- init + BGM track-swap ported (BGM audible on Windows host)
- MCI debug command recorder ported (FUN_00451874, dormant in shipped data)
- volume sin-curve fade ported — `audio_fade_compute` + `audio_fade_apply`
  with per-channel slider state (BGM / SE-A / SE-B); the Win32 backend
  installs a hook that calls `IDirectMusicAudioPath::SetVolume`.
- **per-tick fade animation ported** (FUN_0049966a LAB_00499a00):
  `audio_fade_progress_centibel(phase, progress, duration, slider)`
  + `audio_fade_apply_progress(channel, ...)` in `src/audio_fade.{c,h}`;
  `src/music.c::music_step` advances `fade_progress` per tick, calls
  the new helper, sets `pending_swap_clear = 1` at fade end. Phase
  semantics corrected (the previous comment had 1/2 swapped): **phase 1
  = audible fade-OUT** (cos 1→0 across `progress`), **phase 2 = audible
  fade-IN** (cos 0→1). No trace event per frame to avoid swamping the
  JSONL log; the apply hook drives SetVolume directly.
- SE phase A: 110-entry resource ID table + `audio_play_se()` trace shell
- **SE phase B**: 2 SE AudioPaths created in `audio_init`, 109 WAV
  blobs embedded via windres custom-type `WAVE` + loaded via
  `IDirectMusicLoader::GetObject(DMUS_OBJ_MEMORY)`, `audio_play_se`
  drives a real PlaySegmentEx with QueryInterface upgrade to
  `IDirectMusicSegmentState8`.
- **Phase-B engine deviations reverted (2026-05-21):**
  - SE PlaySegmentEx uses `DMUS_SEGF_QUEUE` (0x80) — engine fidelity.
    BGM lives on a separate AudioPath so SE queueing doesn't preempt
    it.
  - Init-time `SetVolume(0, 0)` on the SE paths dropped.
  - **Observed regression on the user's Windows host:** SEs are
    inaudible after the revert (BGM still plays). The previous
    workaround (init-time SetVolume + SECONDARY flag) was audible.
    `fade_start` trace events confirm `audio_fade_apply` does fire
    per SE with centibel=0; PlaySegmentEx returns success. Likely
    missing: something the engine sets up at boot that we haven't
    ported yet (candidates: `FUN_004901c2` save-arena init touches
    `_DAT_056e5780` and other fields that may feed back into audio
    state; `recet.ini`'s `mu`/`se` aren't wired into the runtime
    sliders yet). Tracked as an open issue — not patched with a
    deviation; we'll find the missing piece.
- `--audio-trace` opt-in JSONL emitter live (bgm_swap + se_play + fade_start)

The engine uses DirectMusic 8 (not DirectSound directly). All WAV files
are loaded as **DirectMusic segments** through `IDirectMusicLoader8`,
which transparently wraps the raw `.wav` data in a synthesizable
segment. Playback goes through `IDirectMusicPerformance8::PlaySegmentEx`
on a `DMUS_APATH_DYNAMIC_STEREO` audio path with 64 performance
channels and `DMUS_AUDIOF_ALL`.

## Engine call sites

| function          | role                                       | port location |
|-------------------|--------------------------------------------|---------------|
| `FUN_00498ef4`    | "init daoudio ok" — full init, preload     | `src/audio.c:audio_init` |
| `FUN_00499200`    | Track-swap (Stop + Load + PlaySegmentEx)   | `src/audio.c:audio_play_track` |
| `FUN_00499583`    | Volume-apply (cos-curve fade)              | `src/audio_fade.c:audio_fade_compute` + `audio_fade_apply` (Win32 hook in `src/audio.c:audio_fade_apply_hook_win32`) |
| `FUN_00499c63`    | SE start/stop on per-channel SE AudioPaths | `src/audio.c:audio_play_se` (full path: pre-PlaySegmentEx Stop + SetVolume + QUEUE-flag play + QI upgrade) |
| `FUN_00499538`    | Phase-1 setter (fade-OUT + duration)       | exposed via `music_state_t::pending_fade_phase = 1` + `.fade_duration` |
| `FUN_0049954c`    | Phase-2 setter (fade-IN, keeps duration)   | exposed via `music_state_t::pending_fade_phase = 2` |
| `FUN_00451874`    | MCI debug command recorder                 | `src/audio_mci.c` (dormant — `DAT_0438ccb4` zero in shipped data) |
| `FUN_00503994`    | CRT-style cos() wrapper                    | replaced by libc `cos()` in `audio_fade.c` |
| `FUN_0049a558`    | Title-music language-table lookup          | inlined in `src/music.c::title_bgm_select` |
| `FUN_0049966a`    | Per-tick selector (sim_b) + fade tail      | `src/music.c::music_step` (tail: per-tick fade via `audio_fade_apply_progress`) |

## Object layout

The engine carries three audio paths (one BGM, two SE) and a fixed-size
table of 21 BGM segment slots:

```
DAT_09643100   IDirectMusicPerformance8*
DAT_09643104   IDirectMusicLoader8*
DAT_09643108   IDirectMusicAudioPath*    // BGM path
DAT_0964310c   IDirectMusicAudioPath*    // SE path A
DAT_09643110   IDirectMusicAudioPath*    // SE path B
DAT_09643038[21]  IDirectMusicSegment8*  // BGM segments (indexed by track ID)
DAT_09642e24[21]  IDirectMusicSegmentState*  // currently-playing segment-state per BGM slot
DAT_09642e7c[110] IDirectMusicSegment8*  // SE segments (from PE resources)
DAT_09642c6c[110] IDirectMusicSegmentState*  // SE segment-states
```

**Sizing correction (2026-05-21):** earlier notes (and the autonomous
session brief) said "27 SE entries". The actual count is **110**,
addressed by the ID table at `&DAT_005d1584..&DAT_005d18f4` (880 bytes
÷ 8-byte stride). See "SE resource layout" below for the full
breakdown.

```
```

The port (`src/audio.c`) groups them into a single static struct, but
the slot semantics are identical.

## Init sequence (`FUN_00498ef4`)

1. Zero the 0x6e-slot SE-stop-pending array at `DAT_0964308c`.
2. `CoInitialize(NULL)`.
3. `CoCreateInstance(CLSID_DirectMusicPerformance, IID_IDirectMusicPerformance8)`
   → performance.
4. `performance->InitAudio(NULL, NULL, hwnd, DMUS_APATH_DYNAMIC_STEREO=8,
   64, DMUS_AUDIOF_ALL=0x3f, NULL)`.
5. Three `performance->CreateStandardAudioPath(DMUS_APATH_DYNAMIC_STEREO,
   64, TRUE, &path[i])` calls for `path_bgm`, `path_se_a`, `path_se_b`.
6. `CoCreateInstance(CLSID_DirectMusicLoader, IID_IDirectMusicLoader8)` → loader.
7. `GetCurrentDirectoryA` + `MultiByteToWideChar` + `loader->SetSearchDirectory(
   GUID_DirectMusicAllTypes, cwd_w, FALSE)`.
8. Loop `iVar5 = 0 .. 0x54 step 4` (21 iterations):
   - `MultiByteToWideChar` on the BGM filename pointer from
     `PTR_s_bgm_retitle2010_wav_005d190c[iVar5/4]`.
   - `loader->LoadObjectFromFile(CLSID_DirectMusicSegment,
     IID_IDirectMusicSegment8, fname_w, &segments[iVar5/4])`.
   - **EN-build extra (`DAT_0438b170 == 1`)**:
     `seg->SetParam(GUID_StandardMIDIFile, 0xffffffff, 0, 0, NULL)`.
     `DAT_0438b170` is BSS-zero in the current Steam build, so this
     never fires; the port omits it (would need a `DAT_0438b170`
     setter to ever reach it).
   - `seg->SetRepeats(repeats)` — `repeats = 0xffffffff` (infinite)
     except for one-shot indices `{10, 11, 13, 19}` (treasure /
     fanfare / clear / staff) which get `0` (play once).
   - `seg->Download(performance)`.
9. Loop the SE table (`DAT_005d1584[110]` → `DAT_005d18f4`):
   - `FindResourceA(NULL, MAKEINTRESOURCE(rid), (LPCSTR)&DAT_005d1ac8)`
     — the third arg is `"WAVE"` (a **custom named** resource type
     stored at `&DAT_005d1ac8`), not the standard `RT_WAVE` (25) or
     `RT_RCDATA` (10) numeric type. windres-side: `<id> WAVE "<file>"`.
   - `LoadResource` → `LockResource` → `SizeofResource`.
   - Fill `DMUS_OBJECTDESC` (`dwSize = 0x350`, `dwValidData = DMUS_OBJ_CLASS
     | DMUS_OBJ_MEMORY = 0x402`, `guidClass = CLSID_DirectMusicSegment` —
     inlined as 4 dwords on the stack).
   - `loader->GetObject(&desc, IID_IDirectMusicSegment8, &se_segments[i])`.
   - `se_seg->Download(performance)`.
10. Set `DAT_096430fc = 1` (gates the swap path).

**Port status:** steps 1-8 ported, 9-10 in the SE follow-up commit. Step
10's `DAT_096430fc` flag is mirrored by `g_audio.all_loaded`.

## Track-swap (`FUN_00499200`)

```c
int FUN_00499200(int param_1)
{
    if (DAT_096430fc == 0) return 0;                   // not inited yet
    if (param_1 == -2) {                               // STOP sentinel
        sprintf(local_104, "&DAT_005d1ad0 fmt", DAT_005d1960);
        performance->Stop(NULL, NULL, 0, 0);
        return 1;
    }
    if (DAT_005d1960 == param_1) return 0;             // already playing
    DAT_005d1960 = param_1;
    if (segments[param_1] == NULL) return 0;
    if (segment_states[param_1] != NULL) {             // release prior state
        segment_states[param_1]->Release();
        segment_states[param_1] = NULL;
    }
    if (DAT_09643114 == 0) FUN_00499583();             // volume-apply if not fading
    HRESULT hr = performance->PlaySegmentEx(
        segments[param_1], NULL, NULL, 0, 0,
        &segment_states[param_1], NULL, path_bgm);
    return SUCCEEDED(hr);
}
```

The port mirrors this in `src/audio.c::audio_play_track`. One
deviation:

- The `sprintf` to `local_104` builds a debug-log path (`"%s%d"` style)
  that the engine's no-op logger swallows. Skipped.

The engine's `FUN_00499583` call is now real: `audio_fade_apply(
AUDIO_FADE_CHANNEL_BGM)` runs ahead of PlaySegmentEx. We always apply
(the engine's `DAT_09643114 == 0` gate stays true — the per-tick fade
animation that toggles it lives in `FUN_0049966a`'s tail and isn't
ported yet).

## Volume sin-curve fade (`FUN_00499583`)

Cos-arc ramp from "silence" up to a target volume across 10 frame
counter values (9 → 0). Hot loop:

```c
if (frame_counter == 0) {
    audio_path_bgm->SetVolume(-10000, 0);   // hard silence
    return;
}
float angle = (9.0f - frame_counter) * 1.2566371f / 9.0f;   // 1.2566371 = 2π/5
float ratio = FUN_00503994(angle);                          // cos()
int32_t centibel = ftol(ratio * target_vol_normalized * 9600.0f - 9600.0f);
audio_path_bgm->SetVolume(centibel, 0);
```

Constants verified at `&DAT_005196b4 = 9.0f`, `&DAT_00519ff4 = 9600.0f`,
`&DAT_00519ff8 = 1.2566371f`. The `target_vol_normalized` factor
(`&DAT_005d1580`, a [0,1] float) is the music-system master volume
the user sets in `recet.ini` (`mu=0..9` → divided by 9 by the ini
parser). For our port (`src/audio_fade.c`) the signature is reframed
in centibel space:

```c
result = cos(angle) * (target_centibel + 9600) - 9600;
```

so that frame 9 returns `target_centibel` unchanged. Algebraically
equivalent to the engine math when
`target_centibel = target_vol_normalized * 9600 - 9600`.

**9-frame ramp shape** (target=0):

| frame | angle (rad) | cos(angle) | centibel |
|------:|------------:|-----------:|---------:|
|     0 |    n/a      |    n/a     |   -10000 |   (engine's frame-0 fast path; the math curve's asymptote is only -9600)
|     1 |     1.117   |    0.438   |    -5391 |
|     2 |     0.977   |    0.560   |    -4220 |
|     3 |     0.838   |    0.668   |    -3185 |
|     4 |     0.698   |    0.766   |    -2246 |
|     5 |     0.559   |    0.848   |    -1459 |
|     6 |     0.419   |    0.914   |     -827 |
|     7 |     0.279   |    0.961   |     -374 |
|     8 |     0.140   |    0.990   |      -94 |
|     9 |     0.000   |    1.000   |        0 |

Plotted at `runs/audio-fade-curve.png` via
`tools/plot/render_audio_fade_curve.py`.

**Engine inconsistency note:** the frame-0 fast path uses -10000
hard-silence while the math curve's asymptote is only -9600. The
port preserves the difference. Not sure if intentional, but other
DirectMusic players accept either value equivalently for the
"silent" end of the perceptual range.

`FUN_00503994` is decompiled as a 9-byte stub but the actual function
is a CRT-style `cos()` with FPU control-word juggling and full
NaN/edge-case handling. Behaviorally equivalent to libc `cosf()`;
our port skips the FPU plumbing and calls `cos(double)` directly.

## SE resource layout

The 110-entry ID table at `&DAT_005d1584..&DAT_005d18f4` is two
disjoint ID ranges:

- Slots **0..68**: IDs `0x013d..0x0182` with two out-of-order
  pairs — slot 2 is `0x0135` (the rest of the [0x13d..] range comes
  in order after it), and slots 39/40 are `0x0166`/`0x0165` (swapped
  relative to neighbors). Slot 13 skips `0x0149`.
- Slots **69..109**: IDs `0x029d..0x02c6` in order, with `0x02c3`
  skipped between slots 107 and 108.

The C copy of the table is at `src/audio_se_names.c::audio_se_resource_ids[]`,
sourced from `tools/extract/se-wavs.py` which both writes the table
and dumps every found WAV to `vendor/unpacked/se-extracted/`. The
vendor cross-check test (`test_audio_se_table_matches_vendor_bytes`)
re-reads the table from the exe at boot and diffs against the C
copy, so any future drift gets caught at test time.

Per-slot AudioPath assignment is **data-driven** via the +4 column of
each row in the engine, not round-robin: `FUN_00499c63` routes to path
A when `row.channel_flag == 0` and path B otherwise, plus runs a
cross-slot voice-stealing scan that Stops every SE sharing the same
non-zero flag. In shipped vendor data the +4 column is all-zero for
all 110 rows (verified by reading `&DAT_005d1584..&DAT_005d18f4` from
the unpacked exe — 880 bytes, every `u32` at offset +4 is `0`). So
the path-B route + the voice-stealing scan are **dead code at runtime**
— every SE in vendor data routes to path A. See engine-quirks #46 for
the full writeup. Phase B will still create both SE AudioPaths to
match `audio_init`'s shape (engine fidelity), but the live
`audio_play_se` will only ever dispatch onto path A.

`PlaySegmentEx` is invoked with `dwFlags = DMUS_SEGF_QUEUE = 0x80`
(vs BGM's default `0`), which would queue same-path SEs if the
voice-stealing scan didn't preempt them. Since the scan never fires,
the queueing flag is also effectively dormant in vendor data — the
per-slot Stop right before each PlaySegmentEx (`Stop(performance,
se_segments[slot], 0, 0, 0)`) handles repeat-trigger reset on its own.
The QUEUE flag is harmless for BGM because BGM lives on a separate
AudioPath (`path_bgm`) — queueing is scoped per-AudioPath, so SE
queues on `path_se_a` only.

The `IDirectMusicSegmentState` returned by PlaySegmentEx is
`QueryInterface`-upgraded to `IDirectMusicSegmentState8` and stored at
`DAT_09642c6c[slot]`. The original pointer is `Release`d immediately
afterwards. Phase B must mirror this — storing the un-upgraded type
would leak the QI'd reference on the next trigger.

## Audio trace (opt-in JSONL)

Off by default. When `--audio-trace <path>` is set on the exe (or
the `--audio-trace` flag is passed to `tools/smoke-test.py`), one
NDJSON line is appended per audio event:

```json
{"t_ms":198,"kind":"bgm_swap","track":0,"name":"bgm/retitle2010.wav"}
{"t_ms":198,"kind":"fade_start","channel":0,"slider":9,"centibel":0}
{"t_ms":342,"kind":"se_play","slot":12,"name":"se_012_id0148"}
{"t_ms":342,"kind":"fade_start","channel":1,"slider":9,"centibel":0}
```

- `t_ms` — `timeGetTime()` ms since `audio_trace_open()`. The trace
  opens *before* `audio_init`, so the first BGM swap typically logs
  near zero.
- `kind` — string discriminator. `"bgm_swap"`, `"se_play"`, and
  `"fade_start"` (one per audio_fade_apply call). The fade_start
  event fires *just before* the matching bgm_swap or se_play, so
  pairing them is straightforward.
- `name` — JSON-escaped per `audio_trace_json_escape()`:  `\"`, `\\`,
  `\n`, `\r`, `\t` mapped explicitly; other non-ASCII bytes become
  `\u00XX`. Filenames in our tables are ASCII so the escape stays
  small in practice.
- `channel` — 0 = BGM, 1 = SE-A, 2 = SE-B (matches
  `AUDIO_FADE_CHANNEL_*` constants).
- `slider` — current [0,9] slider value for the channel.
- `centibel` — the SetVolume value applied; 0 = full target, -10000
  = hard silence (frame-0 fast path), intermediate values trace the
  cos curve.

Schema is locked in `src/audio.h`. The harness writes the log to
`runs/<scenario>/<run_id>/audio-trace.jsonl`, parsable line-by-line
with `json.loads()`.

## Music-bridge handshake

`music.c` exposes a `music_swap_fn_t g_music_swap_fn` pointer. The
selector's swap-dispatch path calls it (when non-NULL) on a track change.
`audio_init` installs `audio_play_track_adapter` into it; `audio_shutdown`
clears it. Tests can drive `g_music_swap_fn` with a stub to verify the
selector's dispatch without linking the Win32 backend.

This bridge is what keeps the test build (host gcc + ASan) free of the
Windows-only DirectMusic headers. `src/audio.c`'s portable bits
(filename table, one-shot lookup) sit outside the `#ifdef _WIN32` guard
so tests can verify the data table directly.

## Verified behavior (boot 2026-05-21)

```
audio: init ok — 21 BGM segments preloaded
music: swap #1 → track 0 (frame 1)
```

User confirms title BGM is audible on Windows host via WSLInterop. Boot
smoke (`tools/smoke-test.py --scenario boot --duration 4 --capture`)
runs to completion with 4 frames captured.

## Engine GUIDs and constants reference

Extracted via `tools/analyze/pe.py` against `vendor/unpacked/recettear.unpacked.exe`:

| VA          | symbol                              | value                                  |
|-------------|-------------------------------------|----------------------------------------|
| `0x005d190c`| BGM filename pointer table (21×4 B) | → `bgm/retitle2010.wav` etc.            |
| `0x005d1584`| SE resource-id table (110×8 B)      | IDs 0x13d..0x182 + 0x29d..0x2c6 — custom "WAVE" type |
| `0x005d1ac8`| SE resource type-name string         | `"WAVE"` — passed to FindResourceA     |
| `0x005196b4`| audio fade divisor                   | `9.0f`                                 |
| `0x00519ff4`| audio fade scale                     | `9600.0f`                              |
| `0x00519ff8`| audio fade angle scale               | `1.2566371f` (= 2π/5 rad)              |
| `0x0051a0b0`| `IID_IDirectMusicSegmentState8`     | `{A50E4730-0AE4-48A7-9839-BC04BFE07772}`|
| `0x0051a0c0`| `IID_IDirectMusicSegment8`          | `{C6784488-41A3-418F-AA15-B35093BA42D4}`|
| `0x0051a0d0`| `IID_IDirectMusicPerformance8`      | `{679C4137-C62E-4147-B2B4-9D569ACB254C}`|
| `0x0051a0e0`| `IID_IDirectMusicLoader8`           | `{19E7C08C-0A44-4E6A-A116-595A7CD5DE8C}`|
| `0x0051a310`| `GUID_StandardMIDIFile`             | `{06621075-E92E-11D1-A8C5-00C04FA3726E}`|
| `0x0051a4c0`| `GUID_DirectMusicAllTypes`          | `{D2AC2893-B39B-11D1-8704-00600893B1BD}`|
| `0x0051a540`| `CLSID_DirectMusicLoader`           | `{D2AC2892-B39B-11D1-8704-00600893B1BD}`|
| `0x0051a5a0`| `CLSID_DirectMusicSegment`          | `{D2AC2882-B39B-11D1-8704-00600893B1BD}`|
| `0x0051a5b0`| `CLSID_DirectMusicPerformance`     | `{D2AC2881-B39B-11D1-8704-00600893B1BD}`|

The mingw-w64 `libdxguid.a` exports these symbols natively, so the port
links against `-ldxguid` (already in `LIBS` in `src/Makefile`).

## Music-past-the-title: retail ground truth (2026-06-02)

Retail capture (`runs/bgm-probe`, house-walk-down trace, seed default, turbo,
16000 frames) watching `current_track` (`DAT_005d1960`) + `g_scene_state`
(`DAT_0438b1c0`):

```
frame      1  state=0 (title)   track=0   bgm/retitle2010 — title theme
frame     72  state=1 (INGAME)  track=0   *** title theme PERSISTS into the game ***
frame  11984  state=1 (INGAME)  track=9   bgm/close.wav — shop-CLOSED / home theme
```

Two findings that shape the port wiring (`music_step_default` currently pins
`scene_state=0`, so it never leaves the title track):

1. **The title theme keeps playing through the whole opening prologue** — new
   game does NOT immediately swap BGM. The selector's swap is suppressed during
   the scene-load + dialogue window (the loading sub-state gate
   `DAT_0438beb0`/`be94` and `FUN_00452911` global-pause keep the swap from
   firing), so `retitle2010` carries the new-game cutscene. The BGM only changes
   when real free-roam control begins.

2. **HOUSE free-roam BGM = track 9 (`bgm/close.wav`)** — the closed-shop / home
   theme. The selector's state-1 stage branch (FUN_0049966a `all.c`-equiv
   `49966a.c:86-150`) is:

   ```
   switch (DAT_068dd3fc[DAT_0438b4dc * 0x6cf]) {   // scene_type; stage idx DAT_0438b4dc
     case 0..4:                                     // town/home-type stages
       if (DAT_0438cc08 == 4)  track = FUN_0045e7b7() ? 18 : 8;  // shop OPEN → open / fever
       else                    track = 9;                        // shop CLOSED → close
       break;
     case 5,9 → 5(ruins); 6,0xe,0xf → 4(forest); 7,0xc → 0x14(water);
     case 10,0xb → 3(cave); 0xd,0x10 → 2(sougen); 0x11..0x14 → 0x11(lastd01)
   }
   ```

   The HOUSE is stage 0; its `scene_type` field `DAT_068dd3fc[0]` is **0**
   (already read by `scene1_postload.c:482` for the camera view-mode — it's the
   SAME field at offset 0, not a separate music-id column). So HOUSE + shop
   closed (`DAT_0438cc08 != 4`, which is the case during the prologue + initial
   free-roam) → track 9. Once the player opens the shop (`cc08 == 4`) it becomes
   track 8 (`open`) or 18 (`feaver`, the fever-sale variant).

**LANDED (2026-06-02).** `music_step_default` now feeds the live `g_scene_state`
+ HOUSE stage inputs (`scene_type=0`, `shop_open = player_ctrl_cc08()==4`) to the
selector, and `music_select_track`'s state-1 branch returns
`music_stage_track(scene_type, shop_open, fever)` — the full engine stage switch
(close 9 / open 8 / fever 18 for types 0..4, dungeon BGM for 5+). The swap is
gated (via `g_music.global_pause`) on
`worker_load_busy() || scene1_intro_dialogue_in_progress()` — the latter is a new
gapless NEW_GAME→D_DONE bracket (cc08 is unusable as the prologue marker because
the port sets it to the free-roam value at HOUSE entry, with iv1_2 playing over
live free-roam). Verified end-to-end: house-walk-down port run swaps track 0→9 at
the frame after `CONV_POSE_END` (prologue end), i.e. the title theme holds through
the whole prologue then the close theme lands at free-roam — matching the retail
structure. The absolute swap frame differs from retail's 11984 only by the port's
synthetic-load prologue timing (PORT-DEBT, engine-quirks §85). The fever variant
+ the dungeon stages await those scenes porting (they have no live `scene_type`
producer yet — HOUSE is hardcoded type 0).

## Next steps

In rough order of impact:

1. ~~**recet.ini → slider seeding.**~~ **Done 2026-05-21** in `src/main.c`
   immediately after `audio_init`: `g_ini.mu` → BGM slider, `g_ini.se` →
   SE-A slider, SE-B left at default 9 (dormant in vendor data, no
   recet.ini key for it). Did NOT resolve the SE-inaudible regression
   on user host — vendor recet.ini ships `se=9` so the slider value
   doesn't move, the regression has another root cause. Engine-quirk
   to note: `FUN_0047a804` writes `g_ini.mu`/`.se` back to recet.ini
   on shutdown but the engine never copies live slider values into
   `g_ini.*`, so retail's saveback is just write-back of whatever the
   ini said at boot — saveback faithfulness requires both (a) the
   shutdown chain port and (b) a slider→`g_ini` mirror that the engine
   itself lacks. Park the mirror until settings-menu producer lands.
2. ~~**Per-tick fade animation**~~ **Done 2026-05-21** —
   `audio_fade_progress_centibel(phase, progress, duration, slider)`
   in `src/audio_fade.{c,h}` is the pure two-axis cos product
   (`cos(angle_progress) * cos(angle_slider) * 9600 - 9600`).
   `music_step` calls `audio_fade_apply_progress` per tick when
   `fade_phase != 0`, increments `fade_progress`, clears phase + sets
   `pending_swap_clear` at the end. Phase-clear gate `DAT_0438cd70`
   (carry-over flag) is BSS-zero in observed boot/play traces —
   port pins it to "always clear"; revisit if a future scene flips it.
   17 new tests (475 total). Engine-quirk: the music.h comment had
   phase 1/2 labels swapped (said 1=in, 2=out); the assembly at
   0x499a2b vs 0x499a9e shows the opposite. Corrected.
3. ~~**Settings menu slider producer**~~ **Done 2026-05-21** — title-
   menu Options submenu (FUN_0049a59e state 2, *not* the in-game
   pause variant FUN_0047fc44) ports as part of the title scene.
   `src/scene_title.c::scene_title_sim` gains a settings-step
   submodule that maps row 0/1/2 to BGM/SE-A/SE-B via
   `audio_fade_set_slider`; row 0 also calls `audio_fade_apply(BGM)`
   to re-attenuate the running music. New `audio_play_se_by_id`
   helper bridges the engine's SE-id call sites (0x143 / 0x146) to
   the existing slot-indexed `audio_play_se`. 19 new tests (513
   total). Settings render is deferred until font-system lands —
   see `docs/findings/title-settings-submenu.md` "What's deferred".
   The in-game pause variant (FUN_0047fc44) shares the same slider
   state and will reuse the same audio_fade infrastructure when the
   in-game scenes port.
4. **Shutdown save-back** — `FUN_0047a804` writes `se` / `mu` / `winx` /
   `winy` back to `recet.ini` via `WritePrivateProfileStringA`. Lands
   with the full shutdown-chain port. Companion to the settings-menu
   producer above — the engine's save-back never reads live slider
   values, just whatever the ini said at boot, so persistent volume
   changes need both (a) this saveback and (b) a slider→`g_ini`
   mirror call from the settings exit-save handler.
5. ~~**Filename-based SE feedback (FUN_0049933c)**~~ **Done 2026-06-02** —
   ported as `src/audio.c::audio_play_se_file(const char *path)`, the
   single-slot DirectMusicLoader `LoadObjectFromFile` → `SetRepeats(0)`
   → `Download` → `PlaySegmentEx(QUEUE)` chain on **SE path B** with the
   SE-B slider volume (engine-quirks §87). Reached from the pure-C
   opening-dialogue interpreter (`IVE_OP_SE`) via the `g_ive_se_play_fn`
   bridge, so the `.ivt` `se:<bin>` voice lines + one-off SEs play (Tear's
   `tea_mataku`, Recette's `re_fue`, the `piko` chime, …). The clips are
   loose `bin/se/.../*.bin` RIFF/WAVE files read from the install dir at
   runtime. The settings-menu SE-B inc/dec feedback row (`re_sys01a_b`)
   can now reuse this instead of the 0x146 substitute — left as a small
   follow-up since the settings render is still deferred.
6. **Engine bug-for-bug compatibility** — the EN-build `SetParam(
   GUID_StandardMIDIFile, ...)` call (gated on `DAT_0438b170 == 1`)
   never fires in the current Steam build. If a future build sets that
   flag, the port needs to add the call back; for now it's a documented
   omission.
