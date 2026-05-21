# `recet.ini` — engine configuration

Top-level INI file shipped next to `recettear.exe`. Read once during
boot by `FUN_0047a474` (the "pre-window init" step in the engine
bootstrap sequence — see `docs/findings/winmain-and-bootstrap.md`).
The values determine window size + windowed/fullscreen mode + input
bindings + a handful of debug knobs.

**Spec source:** `docs/decompiled/by-address/47a474.c` (33
`GetPrivateProfileIntA` calls) and the byte tables at `0x005c81d8`
(pad defaults) + `0x005c8204` (skill defaults) in the unpacked binary.

**Port:** `src/recet_ini.{c,h}` — pure-C parser (no Win32 dep) plus
a Win32 `fopen`+parse wrapper. Loaded in `src/main.c:WinMain` before
`create_main_window` so the window can be sized from `screen`.

## File shape

Plain Windows INI. Sections in `[brackets]`, keys as `key=value`,
optional `;` or `#` comments. Values are signed decimal small ints
(GetPrivateProfileIntA's parsing; we use `atoi`). Section + key names
are case-insensitive. Missing section / missing key / missing file all
fall back to a hard-coded default — the engine never errors out.

## Sections + keys (in engine read order)

### `[option]` — input bindings (28 keys)

Two controllers × (9 pad bindings + 5 skill bindings), stored as
shorts at engine addresses `0x0438cce8` (ctrl 0) and `0x0438cd04`
(ctrl 1), stride `0x1c` between controllers. Each key in the ini
is `padNM` or `skillNM` where N is controller index (0..1) and M
is the binding slot.

| key       | default | source byte (unpacked exe)     |
|-----------|--------:|--------------------------------|
| pad00..08 | 1,2,3,4,39,37,16,35,36 | `0x005c81d8` + offset, +1 |
| pad10..18 | 40..48                 | `0x005c81e3` + offset, +1 |
| skill00..04 | 0,0,0,0,0            | `0x005c8204` + offset, +1 (0xff+1 truncated) |
| skill10..14 | 0,0,0,0,0            | `0x005c8209` + offset, +1 |

The pad default table is `0xb` bytes per controller × 2 controllers
= 22 bytes total, but only 9 of each 11 are read (the inner loop
indexes 0..8). The stride-padding bytes (0x0d, 0x1f, 0x30, 0x31) are
dead storage.

### `[setup]` — display / render / misc (25 active keys)

| key         | default | engine field      | notes |
|-------------|--------:|-------------------|-------|
| aspect      | 1       | `DAT_0438cce4`    | |
| winmode     | 1       | `DAT_0438b164`    | 1 = windowed, else fullscreen |
| fps         | 0       | `DAT_0438ccdc`    | |
| dispfps     | 0       | `DAT_0438cce0`    | |
| sfnouse     | 0       | `DAT_0438b1b0`    | |
| texmode     | 0       | `DAT_0438b174`    | |
| mapmode     | 0       | `DAT_0438b1ac`    | |
| demomode    | 0       | `DAT_0438b1b4`    | |
| usemipmap   | 0       | `DAT_0438b178`    | |
| usetree     | 0       | `DAT_0438b17c`    | |
| uselighttex | 0       | `DAT_0438b180`    | |
| texlevel    | 0       | `DAT_0438b184`    | |
| toorioff    | 0       | `DAT_0438b188`    | |
| windowpos   | 0       | `DAT_0438b190`    | nonzero gates the winx/winy save-back in `FUN_0047a804` |
| winx        | 0       | `DAT_0438b1a4`    | saved by shutdown when windowpos != 0 |
| winy        | 0       | `DAT_0438b1a8`    | |
| nolight     | 0       | `DAT_0438b194`    | |
| nolight_s   | 0       | `DAT_0438b198`    | |
| easydisp    | 0       | `DAT_0438b19c`    | also overwrites `bgnodisp` after the loop |
| s_easydisp  | 0       | `DAT_0438b1a0`    | |
| usefog      | 0       | `DAT_0438cd60`    | |
| screen      | 0       | (dispatched)      | maps to (width, height) — see below |

The `screen` key isn't stored as-is; the engine immediately dispatches
through a 4-arm switch:

| screen | width | height |
|-------:|------:|-------:|
| 0      | 640   | 480    |
| 1      | 800   | 600    |
| 2      | 1024  | 768    |
| else (incl. 3+) | 1280 | 960 |

Result lives at `DAT_005cbc04` (width) and `DAT_005cbc08` (height) —
fed to D3D `BackBufferWidth` / `BackBufferHeight` during device
creation.

### `[debug]` — single key

| key      | default | engine field    | notes |
|----------|--------:|-----------------|-------|
| camfree  | 0       | `DAT_0438cd5c`  | (see quirk #38) |

### `[config]` — audio volumes (clamped)

| key  | default | engine field    | clamp |
|------|--------:|-----------------|-------|
| se   | 9       | `DAT_0438ce7c`  | [0, 9] |
| mu   | 9       | `DAT_0438ce80`  | [0, 9] |

Persisted across runs: `FUN_0047a804` (shutdown — not yet ported)
writes both back via `WritePrivateProfileStringA`. Same function
conditionally writes `[setup] winx`/`winy` back if `windowpos != 0`
at shutdown time.

## Keys present in vendor `recet.ini` but **never read** by the loader

Documented as engine quirks #37-#40 (see
`docs/findings/engine-quirks.md`). Listing here for orientation:

| key                | section  | what the engine does with it |
|--------------------|----------|------------------------------|
| `bgnodisp`         | [setup]  | Auto-derived: `bgnodisp = easydisp` after the read loop. The ini value is ignored. |
| `pfnouse`          | [setup]  | Never read anywhere in the binary. |
| `fontmode1`        | [setup]  | Never read. |
| `fontmode2`        | [setup]  | Never read. |

So the shipping `recet.ini` carries vestigial keys from an earlier
engine revision. Our port matches the engine: keys that aren't in
the active read set are silently ignored, just like
`GetPrivateProfileIntA` ignores missing keys.

## Path resolution

**Engine:** `_splitpath(argv[0], drive, dir, fname, ext)` then
`wsprintfA(path, "%s%s/recet.ini", drive, dir)` — i.e. always next
to the exe.

**Port:** CWD-relative `recet.ini` first (matches the dev-workflow
convention where the exe lives in `build/` but is run from
`vendor/original/` so `storage_init` can find `lnkdatas.bin`), then
falls back to next-to-exe via `GetModuleFileNameA` + tail-strip
(matches deployment shape where the exe sits alongside the data
files). Either-or — we never merge two ini files.

## What the port does not yet cover

- **`FUN_0047a804` save-back** (`[config] se`/`mu` always, `[setup]
  winx`/`winy` when `windowpos != 0`). Belongs to the shutdown
  sequence — landed when the shutdown chain is ported.
- **Boot trace** logs the resolved values to stderr in
  `WinMain` for smoke-test introspection:
  ```
  recet.ini: winmode=1 screen=2 (1024x768) se=9 mu=9
  ```
- **Input bindings consumption.** The pad/skill values are loaded
  into `struct recet_ini` but not yet read by anything — the input
  subsystem (`src/input.c`) currently only initialises DInput
  devices. Wiring lands with the input-poll port.
