# Audio backend — DirectMusic 8

**Status:** init + BGM track-swap ported (2026-05-21). SE / volume-fade
/ MCI debug bridge still stubbed.

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
| `FUN_00499583`    | Volume-apply (sin-curve fade)              | **stubbed** |
| `FUN_00499c63`    | SE start/stop on per-channel SE AudioPaths | **stubbed** |
| `FUN_00451874`    | MCI "VOL %d" debug ringbuffer write        | **stubbed** (only fires when `DAT_0438ccb4 != 0`) |
| `FUN_0049a558`    | Title-music language-table lookup          | inlined in `src/music.c::title_bgm_select` |
| `FUN_0049966a`    | Per-tick selector (sim_b)                  | `src/music.c::music_step` |

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
DAT_09642e7c[27]  IDirectMusicSegment8*  // SE segments (from PE resources)
DAT_09642c6c[27]  IDirectMusicSegmentState*  // SE segment-states
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
9. Loop the SE table (`DAT_005d1584[27]` → `DAT_005d18f4`):
   - `FindResourceA(NULL, MAKEINTRESOURCE(rid), MAKEINTRESOURCE(0xa))`
     (`RT_RCDATA` = 10).
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

The port mirrors this in `src/audio.c::audio_play_track`. Two
deviations:

- The `sprintf` to `local_104` builds a debug-log path (`"%s%d"` style)
  that the engine's no-op logger swallows. Skipped.
- `FUN_00499583` (volume-apply) is stubbed for this commit. PlaySegmentEx
  still works at default volume.

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
| `0x005d1584`| SE resource-id table (27×8 B)       | RIDs 309..341 (RT_RCDATA)              |
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

## Next steps

In rough order of impact:

1. **SE backend** — port the SE-init loop (27 `RT_RCDATA` resources) +
   two SE AudioPaths + `FUN_00499c63` (per-channel start/stop).
   Unblocks UI sound cues (cursor moves, button clicks).
2. **Volume animation** — port `FUN_00499583` sin-curve fade. Needed
   for the title-screen fade-out band (frames 0x1b6d..0x1ba7) and any
   in-game fade transitions. Hooks into `g_music.target_volume` which
   the selector already computes.
3. **Shutdown save-back** — `FUN_0047a804` writes `se` / `mu` / `winx` /
   `winy` back to `recet.ini` via `WritePrivateProfileStringA`. Lands
   with the full shutdown-chain port.
4. **Engine bug-for-bug compatibility** — the EN-build `SetParam(
   GUID_StandardMIDIFile, ...)` call (gated on `DAT_0438b170 == 1`)
   never fires in the current Steam build. If a future build sets that
   flag, the port needs to add the call back; for now it's a documented
   omission.
