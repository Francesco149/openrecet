# WinMain and engine bootstrap

**Status:** mapped — full subsystem init chain identified from
`docs/decompiled/`. The engine self-documents via `s_init_*` log
strings even though the logger is a no-op in the release build.

## Quick facts

| fact                         | value                            |
|------------------------------|----------------------------------|
| PE entry (`__tmainCRTStartup`) | `0x5046c7`                     |
| `WinMain`                    | `0x47bfb3` (`FUN_0047bfb3`)      |
| Window class name            | `"Azumanga Main Window"`         |
| Window title                 | `"RECETTEAR Ver 1.108"`          |
| WndProc                      | `0x47b2e7` (`FUN_0047b2e7`)      |
| Window style                 | `WS_OVERLAPPEDWINDOW` (`0xCF0000`) |
| Class style                  | `CS_OWNDC` (`0x40`)              |
| Icon resource ID             | `0x67`                           |
| Cursor                       | `IDC_ARROW` (`0x7F00`)           |
| Debug logger                 | `0x47aa31` — empty stub `return;` |
| Internal engine name         | "Azumanga" (EasyGameStation's engine) |

## Subsystem init order (from `WinMain`)

These are the calls in `FUN_0047bfb3`, in order, with the engine's own
self-documenting log strings:

| # | function             | log string             | what it does                                       |
|--:|----------------------|------------------------|----------------------------------------------------|
| 1 | `timeGetDevCaps` + `timeBeginPeriod(10)` | — | high-resolution timer via WINMM       |
| 2 | `FUN_00451790`       | —                      | **pre-window math init** — writes 6 named globals (camera at (10, 61, -203) + 5 flags), fills 8544-entry object table with `(0, 1.0, 0)`, randomizes 100 particles using the deterministic boot RNG (seed=1), and builds view+projection matrices via D3DXMatrixLookAtRH (degenerate eye=(0,0,0) — engine quirk) / D3DXMatrixPerspectiveFovRH (fov=π/4, aspect=4/3, near=10, far=2000). Ported as `src/prewindow.{c,h}` with helpers in `src/math3d.{c,h}`. |
| 3 | `FUN_00471050` → `FUN_005041ec` | —          | **RNG reseed from wall-clock time** — `FUN_005045eb` computes a custom 32-bit value from `GetLocalTime` + DST flag (via `GetTimeZoneInformation`), stored in `DAT_006023a0` (the LCG state). Engine uses MSVC-classic LCG (0x343fd / 0x269ec3) with its own state global. Ported as `src/rng.{c,h}` + `rng_seed_from_now`. |
| 4 | `FUN_00504384`/`wsprintfA` | —                | builds `recet.ini` path (`_splitpath(argv[0])` + `wsprintfA "%s%s/recet.ini"`) — ported into `src/recet_ini.c:recet_ini_default_path` with CWD-first fallback for the dev workflow |
| 5 | `FUN_0047aa30`       | —                      | 1-byte empty stub (`return;`) — vestigial; intentionally omitted in the port |
| 6 | `FUN_0047aa31`       | `"start"`              | log no-op                                          |
| 7 | `FUN_0047a474`       | —                      | **pre-window init: read `recet.ini`** — 28 input bindings + 25 setup scalars + 1 debug + 2 [config] keys; `screen` dispatches to (width, height). Ported as `src/recet_ini.{c,h}`; see `docs/formats/recet-ini.md` |
| 8 | `FUN_0047aa8b(hInst, nCmdShow)` | —          | **window class register + CreateWindowEx**         |
| 9 | `ShowWindow` + `UpdateWindow` | —             | show the main window                               |
|10 | `FUN_004341fe`       | `"init strage ok"` *(sic)* | **storage init** — opens `lnkdata.bin` / `lnkdatas.bin` |
|11 | `FUN_0047ac6a`       | —                      | **D3D8 device creation** — `Direct3DCreate8` + `CreateDevice` (see §"D3D8 device creation" below). On failure, shows a MessageBox and skips the rest. On success, logs `"init start"` as a section marker. |
|12 | `FUN_00451863`       | `"init print ok"`      | text/print system                                  |
|13 | `FUN_0047af52`       | `"init dinput ok"`     | **DirectInput 8: keyboard + multi-joystick**       |
|14 | `FUN_00454e69(d3d, dev)` | `"init render ok"` | **render-layer init** — fans `IDirect3DDevice8` + back-buffer-desc + `D3DCAPS8` out into the 24 engine "render layer" objects (4 + 20, each 0x2f0 bytes). See §"Render-layer init" below. Despite the label, the *device* itself is already created by step 11. |
|15 | `FUN_00475270`       | `"init indexfile ok"`  | **gameplay-table loader** — see `docs/findings/tables-loader.md` |
|16 | `FUN_0047c228`       | `"init fontsys ok"`    | **font system: clear 200-slot LRU cache.** Ported as `font_init()` in `src/font.c`. See §"Font system" below. |
|17 | `FUN_00498ef4`       | `"init daoudio ok"` *(sic)* | **audio init** — DirectMusic 8 perf + loader + 21 BGM segments + 109 SE WAVs. See `docs/findings/audio-backend.md`. |
|18 | `FUN_0047c474` *(conditional on `DAT_073dfd00`)* | — | **GDI atlas builder** — runs only if `font:` in config.idx raised the regen flag. Walks SJIS codepoints via `GetGlyphOutlineA(GGO_GRAY4_BITMAP)`, edge-dilates, writes `fontdata.bin` + `fontidx.bin`. Ported as `font_atlas_build_win32()` in `src/font_atlas.c`. |
|19 | `FUN_0047c3a5`       | `"fontsystem ok"`      | **font atlas loader** — reads the two `.bin` files back into `g_font_atlas`. Ported as `font_atlas_load()` in `src/font_atlas.c`. |
|20 | `FUN_00472f5d`       | `"read systemtex ok"`  | load UI textures                                   |
|21 | `FUN_004902fe`       | `"load savefile ok"`   | load save data                                     |
|22 | `FUN_0043609b` + `FUN_004733d5` | `"read titletex ok"` | load title screen textures                  |
|23 | `FUN_0049a3a3`       | —                      | enters main loop                                   |

Note the Japanese-English typos preserved in the log strings (`strage`,
`daoudio`) — these came straight from the original Japanese 2007 release.

## Main loop

```c
while (true) {
    while (true) {
        BOOL has = PeekMessageA(&msg, NULL, 0, 0, 0);
        if (has) break;
        if (DAT_073dfca0 == 0) {     // game paused flag (set in WM_ACTIVATE)
            WaitMessage();           // sleep until a message arrives
        } else {
            FUN_0047be92();          // ← GAME TICK
        }
    }
    BOOL got = GetMessageA(&msg, NULL, 0, 0);
    if (!got) break;                 // WM_QUIT
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
}
```

**`FUN_0047be92` is the game tick** — input poll, simulate, render, present.
**Status: scheduler ported (2026-05-21);** the four callees it dispatches
to (`FUN_0047b73c` input poll, `FUN_004536cb`/`FUN_0049966a` sim halves,
`FUN_004547ab` render) still land one-per-commit. See `src/tick.{c,h}`
and §"Game tick scheduler" below for the full RE writeup.

When the loop exits:
- `FUN_0047a804` — pre-shutdown
- `FUN_004349e4` — storage close
- `FUN_005036af(DAT_073dde2c)` / `FUN_005036af(DAT_073dde30)` — close two
  handles (likely `CloseHandle` thunks for mutex/event globals)
- `FUN_0047b255` — final cleanup

## Direct3D 8 device creation

**Function: `FUN_0047ac6a` at `0x47ac6a` (507 bytes).** Called from
`WinMain` right after the storage init succeeds; emits no log string of
its own (failure shows a MessageBox; success is followed by the
`"init start"` log line which gates the rest of the subsystem init).

### Sequence

```c
DAT_073dfcb8 = Direct3DCreate8(0xDC);       // D3D_SDK_VERSION (220)
if (!DAT_073dfcb8) return 0;
if (IDirect3D8::GetAdapterDisplayMode(0, &DAT_073dfc90) < 0) return 0;
//                              ^^^^^^^^^^^^
//                              global D3DDISPLAYMODE — DAT_073dfc9c is its Format field

memset(&DAT_073de268, 0, 13 * sizeof(uint32_t));   // 52 bytes = sizeof(D3DPRESENT_PARAMETERS)

if (DAT_0438b164 != 1) {                     // winmode != 1 → fullscreen
    pp.SwapEffect                    = D3DSWAPEFFECT_COPY;   // 3
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE; // 1 = vsync
    pp.FullScreen_RefreshRateInHz    = 0;
} else {                                     // winmode == 1 → windowed
    pp.SwapEffect                    = D3DSWAPEFFECT_DISCARD; // 1
}
pp.Windowed               = (DAT_0438b164 == 1);
pp.BackBufferFormat       = DAT_073dfc9c;        // from the display mode above
pp.BackBufferWidth        = DAT_005cbc04;        // see "Resolution lookup" below
pp.BackBufferHeight       = DAT_005cbc08;
pp.BackBufferCount        = 1;
pp.MultiSampleType        = D3DMULTISAMPLE_NONE; // 0
pp.EnableAutoDepthStencil = TRUE;                // unconditional
pp.AutoDepthStencilFormat = 0x50;                // D3DFMT_D16
// pp.hDeviceWindow = NULL  (never set — D3D falls back to the focus window)
// pp.Flags         = 0

IDirect3D8::GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps);   // result ignored apart from existence

// Try behavior-flag fallback chain until CreateDevice succeeds.
DWORD bf[] = { 0x44, 0x80, 0x20, 0 };
for (int i = 0; bf[i]; ++i) {
    if (SUCCEEDED(IDirect3D8::CreateDevice(
            0, D3DDEVTYPE_HAL,
            DAT_073dfc7c,                // hwnd
            bf[i],                       // BehaviorFlags
            &DAT_073de268,               // pPresentationParameters
            &DAT_073dfcbc))) break;      // → global IDirect3DDevice8 *
}
FUN_0047ac1d();                          // viewport-like float globals (default camera?)
// then zero 26 dwords from DAT_073de334 and seed a material-like struct with
// 1.0, 0.5, 0.4 diffuse/ambient/specular defaults.
return 1;
```

### `D3DPRESENT_PARAMETERS` field map

| field                            | offset | original value                                |
|----------------------------------|-------:|-----------------------------------------------|
| BackBufferWidth                  | 0x00   | `DAT_005cbc04` (from `screen` ini key)         |
| BackBufferHeight                 | 0x04   | `DAT_005cbc08` (from `screen` ini key)         |
| BackBufferFormat                 | 0x08   | `DAT_073dfc9c` (from `GetAdapterDisplayMode`) |
| BackBufferCount                  | 0x0C   | 1                                              |
| MultiSampleType                  | 0x10   | 0 (`D3DMULTISAMPLE_NONE`)                      |
| SwapEffect                       | 0x14   | windowed → 1 (DISCARD); fullscreen → 3 (COPY) |
| hDeviceWindow                    | 0x18   | NULL (uses focus window from `CreateDevice`)  |
| Windowed                         | 0x1C   | `winmode == 1`                                 |
| EnableAutoDepthStencil           | 0x20   | TRUE (unconditional)                           |
| AutoDepthStencilFormat           | 0x24   | 0x50 (`D3DFMT_D16`)                            |
| Flags                            | 0x28   | 0                                              |
| FullScreen_RefreshRateInHz       | 0x2C   | 0 (default)                                    |
| FullScreen_PresentationInterval  | 0x30   | fullscreen → 1 (`INTERVAL_ONE`/VSYNC); else 0  |

Note the **unusual fullscreen choice**: COPY + VSYNC. DISCARD would be
the conventional fullscreen pick. We have to match for compatibility but
should flag this if it ever shows up in pixel diffs.

### `CreateDevice` behavior-flag fallback chain

The engine tries `BehaviorFlags` in this order until one succeeds:

| value | meaning                                                       |
|------:|---------------------------------------------------------------|
| 0x44  | `D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED` |
| 0x80  | `D3DCREATE_MIXED_VERTEXPROCESSING`                            |
| 0x20  | `D3DCREATE_SOFTWARE_VERTEXPROCESSING`                         |

Note `D3DCREATE_FPU_PRESERVE` is **not** set — the engine accepts the
DX8 default of single-precision FPU mode.

### Resolution lookup — `[setup] screen` ini key

The `recet.ini` `[setup] screen` integer indexes a fixed table:

| `screen` value | width × height (decimal) |
|---------------:|--------------------------|
| 0              | 640 × 480                |
| 1              | 800 × 600 *(default)*    |
| 2              | 1024 × 768               |
| 3+             | 1280 × 960               |

Width/height stored in `DAT_005cbc04` / `DAT_005cbc08`, used by both
`CreateDevice` (above) and the window-rect calculation in
`FUN_0047aa8b`. The `[setup] winmode` int (default 1) drives windowed
vs fullscreen via `DAT_0438b164`.

### Globals after success

| global         | type                  | role                          |
|----------------|-----------------------|-------------------------------|
| `DAT_073dfcb8` | `IDirect3D8 *`        | the factory                   |
| `DAT_073dfcbc` | `IDirect3DDevice8 *`  | **the device** (used everywhere) |
| `DAT_073dfc7c` | `HWND`                | game window                   |
| `DAT_073dfc90` | `D3DDISPLAYMODE`      | adapter display mode (queried once) |
| `DAT_073de268` | `D3DPRESENT_PARAMETERS` | the present params (kept around for Reset) |

The dynamic `d3d8.dll` / `d3d8d.dll` load lives at
`all.c:143472-143479` — primary then debug-runtime fallback, the
standard MSDN pattern.

## DirectInput 8 init (`FUN_0047af52` — "init dinput ok")

**Status:** ✅ ported — `src/input.{c,h}`. Sequence below is the corrected
read after porting (the original transcription had `DIPROP_RANGE = ±5000`
on the joystick — that was wrong; ±1000 is set per-axis by the EnumObjects
callback, and the outer per-device SetProperty calls are AXISMODE+BUFFERSIZE).

```
DirectInput8Create(hInst, 0x800, IID_IDirectInput8A, &g_di, NULL)
  ↓ stored in DAT_073dfcc0
g_di->CreateDevice(GUID_SysKeyboard, &g_di_keyboard, NULL)   // vtbl+0x0C
  ↓ stored in DAT_073dfcc4
g_di_keyboard->SetDataFormat(c_dfDIKeyboard)                 // vtbl+0x2C
g_di_keyboard->SetCooperativeLevel(hwnd, 6)                  // FOREGROUND|NONEXCLUSIVE
g_di_keyboard->SetProperty(DIPROP_BUFFERSIZE = 100)          // vtbl+0x18, prop=1
g_di_keyboard->Acquire()                                     // vtbl+0x1C

g_di->EnumDevices(DI8DEVCLASS_GAMECTRL=4, LAB_0047b167 cb,
                  NULL, DIEDFL_ATTACHEDONLY=1)               // vtbl+0x10
for each enumerated joystick (count capped at 4, in DAT_073dfcd8):
    DAT_073dfcdc = i;                                        // selects target for FUN_0047b1f2
    g_di_joy[i]->SetDataFormat(c_dfDIJoystick)
    g_di_joy[i]->SetCooperativeLevel(hwnd, 6)
    g_di_joy[i]->EnumObjects(FUN_0047b1f2, NULL, 3=DIDFT_AXIS|DIDFT_POV)
        // cb sets DIPROP_RANGE = ±1000 per object via DIPH_BYID
    g_di_joy[i]->SetProperty(DIPROP_AXISMODE = DIPROPAXISMODE_ABS) // prop=2, DIPH_DEVICE
    g_di_joy[i]->SetProperty(DIPROP_BUFFERSIZE = 100)              // prop=1, DIPH_DEVICE
    g_di_joy[i]->Acquire()
```

### Joystick enumeration callback — `LAB_0047b167`

Ghidra did not decompile this — it shows up as a code label, not a
function. Read directly from objdump:

```c
BOOL CALLBACK enum_joy_cb(LPCDIDEVICEINSTANCEA lpddi, LPVOID pvRef) {
    if (FAILED(g_di->CreateDevice(&lpddi->guidInstance,
                                  &g_joys[g_joy_count], NULL)))
        return DIENUM_CONTINUE;            // CreateDevice failed → next

    g_caps.dwSize = sizeof(DIDEVCAPS);     // 0x2C in DAT_073de3e8
    if (FAILED(g_joys[g_joy_count]->GetCapabilities(&g_caps))) {
        g_joys[g_joy_count]->Release();
        g_joys[g_joy_count] = NULL;
        return DIENUM_CONTINUE;            // probe failed → release & next
    }
    g_joy_count++;
    return (g_joy_count < 4) ? DIENUM_CONTINUE : DIENUM_STOP;
}
```

Max of 4 joysticks is hard-coded in the `cmp 4; setl` final test —
explains the static layout of `DAT_073dfcc8..ccd`.

### Per-axis range callback — `FUN_0047b1f2`

```c
BOOL CALLBACK enum_obj_cb(LPCDIDEVICEOBJECTINSTANCEA lpdoi, LPVOID pvRef) {
    DIPROPRANGE r = { .diph = { .dwSize=0x18, .dwHeaderSize=0x10,
                                .dwObj=lpdoi->dwType,
                                .dwHow=DIPH_BYID },
                      .lMin=-1000, .lMax=1000 };
    g_joys[g_joy_cur]->SetProperty(DIPROP_RANGE, &r.diph);
    return DIENUM_CONTINUE;
}
```

`DAT_073dfcdc` (`g_joy_cur`) is the "current target joystick" written by
the outer init loop before each `EnumObjects` call.

Globals:
- `DAT_073dfcc0` — `IDirectInput8 *`
- `DAT_073dfcc4` — `IDirectInputDevice8 *` (keyboard)
- `DAT_073dfcc8…ccd` — `IDirectInputDevice8 *[4]` (joysticks)
- `DAT_073dfcd8` — joystick count
- `DAT_073dfcdc` — current-joystick index (for the EnumObjects callback)
- `DAT_073de3e8` — `DIDEVCAPS` scratch buffer used during enumeration

## Render-layer init (`FUN_00454e69` — "init render ok")

**Function:** `FUN_00454e69` @ `0x454e69` (154 bytes), called as
`FUN_00454e69(DAT_073dfcb8, DAT_073dfcbc)` = `(IDirect3D8*, IDirect3DDevice8*)`.
Despite the "init render" log string, **the device is already created** by
step 11; this function only seeds per-layer engine state.

### What it does

1. `g_d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &local_caps)` — fills a
   212-byte (`0xD4`) `D3DCAPS8` on the stack. (Vtable index 13 / offset
   `0x34` on `IDirect3D8`.)
2. Walks two arrays of "render layer" objects (each 0x2f0 bytes), calling
   the per-layer init helper `FUN_004038e4(this=layer, 0, dev, &caps)`:
   - **20 elements at `DAT_073da2f0`** (loop, stride `0x2f0`, stops at
     `DAT_073dddb0`)
   - **4 elements at `DAT_073cba20`** (unrolled in asm — `0x073cba20`,
     `0x073cbd10`, `0x073cc000`, `0x073cc2f0`)

Total: 24 layer objects. Why split into 4+20 is unclear yet — likely a
"system layers" vs "scene layers" partition; later code that names them
will tell us.

### Per-layer init — `FUN_004038e4` @ `0x4038e4`

Thiscall (`this` in ECX), 3 stack args (`__userpurge` 0/dev/caps_ptr):

```c
void layer_init(layer *this, void *user_ptr, IDirect3DDevice8 *dev,
                const D3DCAPS8 *caps) {
    this->_200 = user_ptr;        // always 0 from this call site
    this->_108 = dev;
    if (!dev) return;             // paranoia branch; never taken in practice
    IDirect3DSurface8 *bb;
    dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb);
    bb->GetDesc(&this->_10c);     // D3DSURFACE_DESC = 32 bytes
    bb->Release();
    memcpy(&this->_12c, caps, 0x35 * 4);  // 212 bytes — D3DCAPS8
}
```

### Layer struct (known offsets)

| offset  | size | field                | notes                               |
|--------:|-----:|----------------------|-------------------------------------|
| `0x000` | 264  | (unknown)            | TBD                                 |
| `0x108` |   4  | `IDirect3DDevice8 *` | set by init                         |
| `0x10c` |  32  | `D3DSURFACE_DESC`    | back-buffer description             |
| `0x12c` | 212  | `D3DCAPS8`           | full caps blob (copied verbatim)    |
| `0x200` |   4  | `void *`             | nulled at init — usage TBD          |
| `0x204` | 236  | (unknown)            | TBD                                 |
|   —     | 752  | total `0x2f0`        |                                     |

### Globals

| name               | type                 | role                              |
|--------------------|----------------------|-----------------------------------|
| `DAT_073cba20[4]`  | `render_layer_t[4]`  | "system" layers (4 unrolled inits)|
| `DAT_073da2f0[20]` | `render_layer_t[20]` | "scene" layers (20-element loop)  |

### Ported to

`src/layers.{c,h}`. The struct uses real `D3DCAPS8` / `D3DSURFACE_DESC`
fields with `_Static_assert` guards on the four known offsets +
`sizeof()` = `0x2f0`, so any mingw header drift in DX struct sizes fails
the build instead of silently corrupting the layout.

## Storage init (`FUN_004341fe` — "init strage ok")

**Updated 2026-05-19 after closer reading.** The function actually has
**two distinct format paths** — the JP and EN releases use different
on-disk encodings for the same index, with different sentinels:

| variant | filename       | header skip | byte transform     | hash sentinel              |
|---------|----------------|------------:|--------------------|----------------------------|
| JP      | `lnkdata.bin`  | 5 bytes     | `byte' = 0x01 - byte` (over the payload) | `0xC5E1` (`-0x3A1F`) |
| EN      | `lnkdatas.bin` | 0           | identity           | `0x8BAA` (`-0x7456`)        |

Recettear's EN Steam build is the trivial path; only the EN sentinel
applies in practice. The JP path is preserved in the engine and worth
implementing for completeness (and so a JP-locale install of the game
works under OpenRecet).

A discriminator global `_DAT_0438abdc` is set to `1` only when the JP
file was opened — call it `is_jp_format`.

```c
fp = fopen("lnkdata.bin", "rb");                    // JP filename — singular
if (fp) {
    is_jp_format = 1;
    size_t n = filesize(fp);
    buf = malloc(n);
    fread(buf, 1, n, fp);
    // Skip 5-byte JP header, transform payload bytes.
    for (size_t i = 5; i < n; ++i) buf[i] = (uint8_t)(0x01 - buf[i]);
    n_items = be32(&buf[5]);                        // n_items lives after the header
    if (lnkdatas_hash(n - 5, &buf[5]) != (int16_t)0xC5E1) fatal();
} else {
    fp = fopen("lnkdatas.bin", "rb");               // EN filename — plural
    if (!fp) fatal();
    size_t n = filesize(fp);
    buf = malloc(n);
    fread(buf, 1, n, fp);
    n_items = be32(&buf[0]);
    if (lnkdatas_hash(n, buf) != (int16_t)0x8BAA) fatal();
}
```

**Sentinels:**
- EN: `(int16_t)0x8BAA == -0x7456`
- JP: `(int16_t)0xC5E1 == -0x3A1F`

Hash is `lnkdatas_hash` from `src/lnkdatas_hash.{c,h}` (CRC-16/CCITT-FALSE).
Documented above under "Hash function". Ported to `src/storage.{c,h}` —
both paths implemented.

Credit: the dual-path detail was caught by a second read of the Ghidra
decompilation during translation — the first writeup of this file
missed it. Lesson logged: always re-read the source after a first pass,
especially around branch points where the decompiler may inline two
paths that look superficially similar.

## Window class registration (`FUN_0047aa8b`)

```c
WNDCLASSEXA wc = {
    .cbSize       = sizeof(WNDCLASSEXA),  // 0x30
    .style        = CS_OWNDC,             // 0x40
    .lpfnWndProc  = FUN_0047b2e7,
    .hInstance    = hInst,
    .hIcon        = LoadIconA(hInst, MAKEINTRESOURCE(0x67)),
    .hCursor      = LoadCursorA(NULL, IDC_ARROW),
    .lpszMenuName = (DAT_0438b164 != 0) ? MAKEINTRESOURCE(0xB7) : NULL,
    .lpszClassName = "Azumanga Main Window",
};
RegisterClassExA(&wc);
```

The window size comes from `DAT_005cbc04` (width) and `DAT_005cbc08`
(height) — globals set when `recet.ini` was parsed. `AdjustWindowRect`
extends them to client-area-sized window rect. In windowed mode
(`DAT_0438b164 == 1`) the position is read from saved coords
`DAT_0438b1a4` / `DAT_0438b1a8` if `DAT_0438b190` was set; otherwise
the default `CW_USEDEFAULT` (`-0x80000000`).

Menu resource `0xB7` is loaded when windowed — that's the Window menu
with options (probably the screen-size/config submenu).

## WndProc (`FUN_0047b2e7`) message handlers

| msg                  | id    | behavior                                                  |
|----------------------|-------|-----------------------------------------------------------|
| `WM_CREATE`          | 1     | `FUN_0040110f()`; if `DAT_0438cce4 != 1`, check menu item `0x9C44` |
| `WM_DESTROY`         | 2     | save window pos → `DAT_0438b1a4/a8`; `FUN_0040112a()`; `PostQuitMessage(0)` |
| `WM_ACTIVATE`        | 6     | toggle `DAT_073dfca0` (paused-flag); unacquire DInput devices on deactivate, reacquire on activate |
| `WM_CLOSE`           | 0x10  | confirm exit MessageBox if windowed; else fall through to `DefWindowProcA` |
| `WM_KEYDOWN`         | 0x100 | ESC (`0x1B`) → `FUN_00452911` (likely menu/pause); other keys ignored at WndProc level (input goes via DInput) |
| default              |       | `DefWindowProcA`                                          |

Note **input is via DirectInput**, not WndProc — except for the ESC
quick-exit key. This is normal for DInput-based games.

## What to do with this

For the phase-3 skeleton drop-in (`src/openrecet.exe`):

1. `WinMain` skeleton that calls `timeBeginPeriod(10)`, reads `recet.ini`,
   registers `"Azumanga Main Window"` class with our own no-op WndProc,
   creates a window titled `"RECETTEAR Ver 1.108"`, shows it.
2. `Direct3DCreate8(D3D_SDK_VERSION)` + `IDirect3D8::CreateDevice`. Match
   the original's device-creation args (TBD — trace `FUN_00454e69` and
   surrounding code next session).
3. Stub the init chain: each `FUN_*` becomes a function that just stores
   pointers + globals as the original does. We can verify "boot to title
   screen" passes the smoke test before filling in real subsystems.

## Hash function

`FUN_00474f14` (58 bytes) is a **CRC-16/CCITT-FALSE** variant: polynomial
`0x1021`, init `0xFFFF`, MSB-first (no reflection), final result bitwise
inverted (`~crc & 0xFFFF`).  The engine compares the result as a signed
16-bit value; `(int16_t)0x8BAA == -0x7456` is the "file valid" sentinel.
The function is called over the entire `lnkdatas.bin` buffer as read by
`fread` (header included — no skip).

```
crc = 0xFFFF
for each byte b in buffer:
    crc ^= (b << 8)
    for 8 iterations:
        if MSB(crc) == 0:  crc = crc << 1
        else:              crc = (crc << 1) - 0x1021
return ~crc & 0xFFFF          # engine: (int16_t)(~crc)
```

Ported to `src/lnkdatas_hash.c` / `src/lnkdatas_hash.h` (C11, mingw32)
and `tools/extract/lnkdatas_hash.py` (pure Python).  Output matches
`recettear-repacker/crc.py` (UnrealPowerz) exactly; both return `0x8BAA`
on the unmodified EN `lnkdatas.bin`
(sha256 `6c5b93cf7a68be348f76435dd529eb822ca923b3b9d2da4c0b04d157d3a847c5`).

Ghidra decompiles `uVar1` as a plain `uint` (32-bit), so the inner loop
appears to use 32-bit arithmetic with no explicit masking — this is not a
bug in the decompilation; in practice the register never grows beyond 16
bits because the init value and both branches only ever add/subtract
quantities ≤ `0x1020`.  A masked-to-16-bit C implementation is therefore
equivalent and avoids reliance on unsigned overflow.

## Game tick scheduler

**Function: `FUN_0047be92` at `0x47be92` (289 bytes).** Called from the
PeekMessage idle loop whenever the game isn't paused. The scheduler is
a fixed-timestep dispatcher with input running at display rate and sim
at a selectable rate (60/30/20/12/6 FPS).

### Time arithmetic

All math runs in *thirds of a millisecond* — the engine multiplies
`now_ms` by 3, carries a `% threshold` residue across iterations, and
indexes a frame-budget table whose entries are scaled by 3 to match.
This gives sub-ms precision on the frame budget without floating-point.

`FUN_0047be2f` (the time source) returns `QPC * 1000 / QPF` truncated
to 32 bits, falling back to `timeGetTime()` if either QPC value is
zero. Ported as `tick_now_ms` in `src/tick.c`.

### Speed-threshold table — `0x005cbc58`

| index | bytes        | thirds of ms | ms     | FPS  |
|------:|--------------|-------------:|-------:|-----:|
|     0 | `32 00 00 00`|         0x32 |  16.67 |   60 |
|     1 | `64 00 00 00`|         0x64 |  33.33 |   30 |
|     2 | `96 00 00 00`|         0x96 |  50.00 |   20 |
|     3 | `fa 00 00 00`|         0xfa |  83.33 |   12 |
|     4 | `f4 01 00 00`|        0x1f4 | 166.67 |    6 |

(The bytes at `0x005cbc68..0x005cbc70` happen to read `1, 1, 2` but are
unrelated globals — the engine indexes the table with `DAT_0438ccd8`
which is set only to `0..4` by the unmapped F-key handler. Don't
extend the table on the basis of those bytes.)

### Dispatch order

```c
1. speed = pending_speed                  // latch (DAT_0438ccd8 ← DAT_0438ccdc)
2. now_ms = tick_now_ms()                 // QPC/QPF*1000 ms
3. delta_thirds = (now_ms*3) - (prev_ms*3) + leftover_thirds
4. if delta_thirds >= table[0]:
       input_poll()                       // FUN_0047b73c — always at ≥60 Hz
5. threshold = table[speed]
6. if delta_thirds < threshold:
       sleep_ms = ?                       // adaptive (below), maybe 0 = busy spin
       Sleep(sleep_ms); return 1
7. leftover_thirds = delta_thirds % threshold
8. prev_ms = now_ms
9. if state ∈ {0, 2}:
       state_alt = state_seed             // DAT_073dfca8 ← DAT_073dfcb0
       for i in 0..speed:
           sim_a()                        // FUN_004536cb
           sim_b()                        // FUN_0049966a
       if !d3d || !device: return 0
       render()                           // FUN_004547ab
       frame_count++
       if state == 2: state = 1           // auto-transition
       flag_dddd0 = 0
       flag_dddfa = 0
10. return 1
```

### Adaptive sleep when delta < threshold

The engine computes `remaining = (threshold - delta) - 10` (in 1/3 ms):

| condition                       | sleep      |
|---------------------------------|------------|
| `delta < threshold - 10` and `remaining < 11` | `remaining/3 + 1` ms (1..4) |
| `delta < threshold - 10` and `remaining ≥ 11` | `5` ms |
| `delta ≥ threshold - 10`        | no Sleep — busy-spin |

There's a dead clamp `if (0x1e < remaining) remaining = 0x1e;` inside
the `remaining < 11` branch — unreachable given the outer guard.
Probably a leftover from an earlier formula; preserved as a comment
in `src/tick.c` for the record.

### Engine globals

| name           | role                                                       |
|----------------|------------------------------------------------------------|
| `DAT_0438ccd8` | current frame's latched speed (0..4)                       |
| `DAT_0438ccdc` | pending speed (set by F-key handler, not yet mapped)       |
| `DAT_073de618` | `now_ms` — last sampled time                               |
| `DAT_073de61c` | `prev_ms` — time of last successful tick                   |
| `DAT_073de620` | `delta_thirds` — current frame's accumulated budget        |
| `DAT_073de624` | `leftover_thirds` — residue carried across iterations      |
| `DAT_073dfca4` | tick state machine: 0=normal, 1=skip, 2=just-resumed       |
| `DAT_073dfca8` | "alt" state, written each frame from `state_seed`          |
| `DAT_073dfcb0` | `state_seed`, source for the above (purpose TBD)           |
| `DAT_073dfcfc` | title-scene BG-scroll tick (NOT a global counter — see engine-quirks §"Frame counter pauses on scene transition") |
| `DAT_073dddd0` | per-frame flag, cleared on each ticked frame (purpose TBD) |
| `DAT_073dddfa` | per-frame flag, cleared on each ticked frame (purpose TBD) |

### Ported to

`src/tick.{c,h}`. The pure-C `tick_step_with_now` takes the four
callees as function pointers so the dispatcher can stand alone (and
so tests can mock them under ASan). The Win32 wrapper
`tick_step_win32` bundles QPC + Sleep on top. Eighteen unit tests
cover the time math, sleep branches, state-machine transitions, and
the no-device early-return path.

## Input poll (`FUN_0047b73c`)

First of the four tick callees to land. Polls the keyboard + up to 4
joystick DInput devices, decodes POV-hat / stick axes / button
bytes, maps each "pressed" hardware input through a binding table
(populated from `recet.ini` `[option] padNM=...` / `skillNM=...`)
into a 14-bit output mask, OR'd into `DAT_073dddd0`. The pressed
bits get cleared at the bottom of `FUN_0047be92` after the render
callback returns — that gives the engine a per-frame accumulator
that captures input across multiple polls per render (relevant when
`speed > 0`).

### Output bit layout — `DAT_073dddd0`

| binding slot | source ini key | mask        | role                     |
|-------------:|:---------------|------------:|:-------------------------|
|   0          | `pad[N][0]`    | `0x0004`    | D-pad UP                 |
|   1          | `pad[N][1]`    | `0x0001`    | D-pad RIGHT              |
|   2          | `pad[N][2]`    | `0x0008`    | D-pad DOWN               |
|   3          | `pad[N][3]`    | `0x0002`    | D-pad LEFT               |
|   4          | `pad[N][4]`    | `0x0010`    | button A                 |
|   5          | `pad[N][5]`    | `0x0020`    | button B                 |
|   6          | `pad[N][6]`    | `0x0040`    | button C                 |
|   7          | `pad[N][7]`    | `0x0080`    | button D                 |
|   8          | `pad[N][8]`    | `0x0100`    | button E                 |
|   9          | `skill[N][0]`  | `0x0200`    | skill 0                  |
|  10          | `skill[N][1]`  | `0x0400`    | skill 1                  |
|  11          | `skill[N][2]`  | `0x0800`    | skill 2                  |
|  12          | `skill[N][3]`  | `0x1000`    | skill 3                  |
|  13          | `skill[N][4]`  | `0x2000`    | skill 4                  |

Note the D-pad mask order (`0x04`/`0x01`/`0x08`/`0x02`) doesn't
follow a binary-rotation pattern — that's just where the original
chained `if`s landed, and downstream readers (camera cursor at
`FUN_004540ae`, lines 50410-50420 of `all.c`) use these exact bits.

### Phase B input injection point

`tools/frida/openrecet-agent.js` attaches `Interceptor.attach` LEAVE
to `FUN_0047b73c`. By that point the engine has already populated
`DAT_073dddd0` from real DInput; the agent then *overwrites* the
slot with the trace mask for the current `g_manual_frame_counter`
(the agent's own per-Present counter — see engine-quirks §"Frame
counter pauses on scene transition (Phase B)" for why we don't use
the engine's `DAT_073dfcfc`). Two reasons this works without further
state-forcing:

- LEAVE fires after the function return but before the caller's
  next instruction (`Interceptor` rewrites the prologue trampoline),
  so `FUN_004536cb` (sim_a) and the button-state ring at the top of
  the same function see the *forced* value, not the polled one.
- The pressed-bit clear at the bottom of `FUN_0047be92` still
  happens, so each frame starts from a clean slate — the trace
  defines what stays in `DAT_073dddd0` after the LEAVE-hook
  re-fires next frame. (Implementation: the agent keeps a sticky
  `g_input_last_forced` between sparse entries, identical to
  `src/input_trace.c::input_trace_lookup`.)

Writing u16 to the 32-bit-aligned slot leaves the upper 16 bits
untouched, which doesn't matter — every reader masks `& 0x3fff` or
narrower (the binding table tops out at bit `0x2000`).

### Virtual-button number space

Every binding value compares against a *virtual* button id that
laces 4 joysticks × 20 buttons + the 1-based 40-entry kbd DIK table
into a single integer:

| range          | source                                |
|---------------:|:--------------------------------------|
|  `1..40`       | keyboard, via `DAT_005cbc2f` DIK table |
|  `0x27..0x3a`  | joystick 0 — D-pad 0..3, buttons 4..19 |
|  `0x3b..0x4e`  | joystick 1                            |
|  `0x4f..0x62`  | joystick 2                            |
|  `0x63..0x76`  | joystick 3                            |

A `pad[0][0] = 1` binding therefore matches the keyboard's DIK_UP
(table entry 1 = `0xc8`), and a `pad[1][0] = 40` matches joystick 0
button index 0 (D-pad UP, virtual code `0x27`). The `+ -1` in
`binding[k] + -1 == iVar6` is what biases the table to a 1-based
index.

### POV-hat decoder

DirectInput reports the POV value as angle × 100 in degrees, with
`-1` (`0xFFFFFFFF`) meaning "centered". The engine recognises 8
discrete positions via equality checks:

| value (decimal) | hex      | D-pad bits |
|----------------:|:--------:|:----------:|
|             0   | `0x0000` | UP         |
|          4500   | `0x1194` | UP + RIGHT |
|          9000   | `0x2328` | RIGHT      |
|         13500   | `0x34bc` | RIGHT + DOWN |
|         18000   | `0x4650` | DOWN       |
|         22500   | `0x57e4` | DOWN + LEFT  |
|         27000   | `0x6978` | LEFT       |
|         31500   | `0x7b0c` | UP + LEFT  |

No tolerance band — a non-snapped POV (e.g. `4501`) reads as
"centered" (no bit). Most game pads snap natively; the engine
relies on that.

### Stick axes — ±500 dead-zone

After the POV decode, the engine OR's in stick-derived D-pad bits:

```c
if (lY < -500) bits[UP] = 1;
if (lX >  500) bits[RIGHT] = 1;
if (lY >  500) bits[DOWN] = 1;
if (lX < -500) bits[LEFT] = 1;
```

`DIPROP_RANGE` was set to ±1000 in init, so 500 = 50% deflection.
Only `lX` and `lY` are checked — `lZ`, rotation axes, and sliders
are unused.

### Button decoding

Reads `rgbButtons[0..15]` and tests bit 7 (DI's "pressed" flag).
16 buttons is exactly half of a `c_dfDIJoystick2.rgbButtons[32]`
array; the engine doesn't expose buttons 16..31 to the binding
system.

### Keyboard DIK table at `0x005cbc2f`

A 41-byte table (index 0 = `0x00` sentinel, indices 1..40 are valid
DIK codes). Extracted via
`python3 tools/analyze/pe.py bytes 0x005cbc2f 41`:

| binding index | DIK code | key       |
|--------------:|---------:|:----------|
|   1           | `0xc8`   | DIK_UP    |
|   2           | `0xcd`   | DIK_RIGHT |
|   3           | `0xd0`   | DIK_DOWN  |
|   4           | `0xcb`   | DIK_LEFT  |
|   5           | `0x36`   | DIK_RSHIFT |
|   6           | `0x9d`   | DIK_RCONTROL |
|   7           | `0x39`   | DIK_SPACE |
|   8           | `0x2a`   | DIK_LSHIFT |
|   9           | `0x1d`   | DIK_LCONTROL |
|  10           | `0x48`   | DIK_NUMPAD8 |
|  11           | `0x4d`   | DIK_NUMPAD6 |
|  12           | `0x50`   | DIK_NUMPAD2 |
|  13           | `0x4b`   | DIK_NUMPAD4 |
|  14..39       | ...      | A..Z (skipping 0x2c=Z at index 39) |
|  40           | `0x00`   | unused (dead last byte)            |

### Quirks documented

- #40: both controllers funnel into single output slot
- #41: 4 outer joystick iterations but only 2 populated bindings
- #42: Poll-failure retry checks Acquire's return for a code Acquire never sets
- #43: each joystick is re-Poll'd four times per frame

### Ported to

`src/input.{c,h}` (added pure-C decoders alongside the existing
init/shutdown). Twenty new unit tests cover POV-hat (all 8
directions), stick dead-zone, button decoding, keyboard DIK
mapping, binding application with per-joystick virtual base, and
the `recet_ini` → `g_input_bindings` flattening. Wired into
`src/main.c` as the `tick_callbacks.input_poll` function pointer.
Smoke boot remains clean (debug magenta unchanged).

## Font system (FUN_0047c228 / 0047c474 / 0047c3a5 / 0047cbcb / 0047cf22 / 0047ca05)

Seven functions form the engine's text rendering pipeline; all seven
are ported. The pipeline:

```
WinMain:
  font_init()                  ← FUN_0047c228   (cache state zero + slot_id seed)
  audio_init()                 ← FUN_00498ef4
  font_atlas_build_win32()     ← FUN_0047c474   (conditional regen via g_config.font_set
                                                 OR ./font/fontdata.bin missing)
  font_atlas_load()            ← FUN_0047c3a5   (./font/ → cwd search chain)

Scene render:
  draw_text(x, y, str, argb, scale)            ← FUN_0047ca05
    for each codepoint:
       font_slot_alloc(b0, b1)                  ← FUN_0047cbcb   (200-slot LRU, age-gated eviction)
       if new: font_slot_upload(slot, dev)      ← FUN_0047cf22   (D3D8 CreateTexture + LockRect + ARGB expand)
       SetTexture(0, g_font.textures[slot])
       render_quad_add(dst, src, tex_dims, argb)
       render_quad_flush(dev)
```

### Atlas format (on-disk byte layout)

| file              | content                                                |
|-------------------|--------------------------------------------------------|
| `fontdata.bin`    | flat blob of glyph bytes, variable size per codepoint  |
| `fontidx.bin`     | fixed-size 40-byte records, one per codepoint slot     |

40-byte record (struct font_atlas_record):

| off  | field       | source                              |
|------|-------------|-------------------------------------|
| 0x00 | data_offset | running cursor into fontdata.bin    |
| 0x04 | data_size   | `cjBuffer` from GetGlyphOutlineA    |
| 0x08 | cell_inc_x  | `gmCellIncX + 8 + pad-to-4(blackBoxX)` |
| 0x0c | line_height | `tmHeight + 8`                       |
| 0x10 | origin_x    | `gmptGlyphOrigin.x`                  |
| 0x14 | ascent      | `tmAscent`                           |
| 0x18 | origin_y    | `gmptGlyphOrigin.y`                  |
| 0x1c | tex_width   | `gmBlackBoxX + 8 + pad-to-4`         |
| 0x20 | tex_height  | `gmBlackBoxY + 8`                    |
| 0x24 | reserved    | always zero                          |

Each glyph byte encodes (high nibble = alpha 0..15, low nibble = edge
intensity 0..15). The texture upload expands this to A8R8G8B8:
RGB = alpha << 4 (replicated), A = edge << 4. Result: bright white
body pixels at full alpha + dark edge pixels at partial alpha — the
familiar outlined-glyph look.

### Codepoint → record-id mapping

The fontidx is indexed by writer-iteration order:

| range            | codepoint id formula                              |
|------------------|---------------------------------------------------|
| 0..0xff          | id = byte                                         |
| 0x100..0x21f     | id = 256 + position in special-codepoint table    |
| 0x220+           | id = (sjis_value - 0x861f)                        |

The 288-entry special-codepoint table at PE VA 0x005cbc7c contains the
full-width ASCII + kana that the engine wants reachable via "single
codepoint" lookup (faster than a SJIS double-byte walk for common
chars). Embedded byte-for-byte in `src/font_atlas.c`.

### 200-slot LRU cache

Each slot is 28 bytes wide. Engine table at `DAT_073de664..DAT_073dfc44`.
Parallel `IDirect3DTexture8*` pointer table at `DAT_073dde44` (200 × 4
bytes).

Engine slot fields (per FUN_0047c228 + readers):
- `+0`: slot_id (set at init to its own index)
- `+4`: cp_byte0
- `+5`: cp_byte1
- `+8`: in_use (1 = allocated, 0 = free)
- `+12`: age (incremented every sim_a frame by FUN_0047c29d)
- `+16..27`: record_id + pad

Allocator's three-phase dance:
1. Scan for existing match (same cp_byte0/1) → reset age, return
2. Scan for first free slot → allocate
3. Scan for first slot with age > 3 → evict, Release texture, allocate

Engine's allocator returns a `slot - 12` pointer for offset-arithmetic
convenience in draw_text; we drop the trickery and return plain int
slot indices. Same observable behavior.

Per-frame: `font_age_tick` (FUN_0047c29d) bumps `age` on every in_use
slot. Wired into `sim_step_a` after the button-state ring update.

### Engine quirks documented

- **Atlas regen polarity** (FUN_0047c474 line 329): Ghidra renders the
  kanjioff check as `if (DAT_005cbc70 == 0) break;` which would skip
  all kanji on the vendor default (kanjioff = 0). Vendor retail
  obviously renders kanji, so the byte-level check must be `!= 0`.
  Our port matches the sane semantic.

- **Phantom 0x883f glyph**: phase-1 of the atlas walk renders codepoint
  0x883f as its first iteration. 0x883f isn't a valid SJIS double-byte
  (low byte 0x3f outside the valid second-byte range), so GDI returns
  an empty glyph. Slot 544 in fontidx ends up referencing zero bytes.
  Harmless — no reader ever asks for 0x883f.

- **TGA-then-D3DX upload**: engine wraps each glyph in a 32-bpp TGA
  with a Truevision XFILE footer and feeds it to
  `D3DXCreateTextureFromFileInMemoryEx`. We use CreateTexture +
  LockRect directly with D3DFMT_A8R8G8B8 — same on-GPU result, no
  D3DX dependency.

- **Slot-overlap return pointer**: allocator returns `slot - 12`, so
  `piVar4[3]` reads slot[i].slot_id and `piVar4[1]` reads slot[i-1]'s
  pad20 — which the upload step deliberately writes as slot[i]'s
  effective_width. Net effect: the last 12 bytes of slot[i-1] act as
  the first 12 bytes of slot[i]'s render-time scratch.

- **draw_text src rect is constant**: `[1, 1, 41, 41]` regardless of
  per-glyph texture size. Default ADDRESSU/V is WRAP, so textures
  smaller than 41×41 (most of them) get tiled. Our port uses
  `[0, 0, tex_w, tex_h]` (full texture per glyph) — different per-pixel
  output, identical layout, no wrap artifact.

### Output paths

The atlas regenerator writes to **`./font/`** (relative to cwd), not
the engine's `./fontdata.bin` + `./fontidx.bin` next to recet.ini.
This is a deliberate divergence — lets a hybrid install (user running
retail to generate atlas, then openrecet) avoid file collisions. The
loader's search chain hits `./font/` first, falls back to `./` for
the case where retail dropped its own files.

## Open subsystems to investigate next

| function       | tag                | priority                                |
|----------------|--------------------|-----------------------------------------|
| `FUN_0047be92` | game tick scheduler | ✅ ported — `src/tick.{c,h}` (callees still stubbed) |
| `FUN_0047b73c` | input poll          | ✅ ported — `src/input.{c,h}` (poll added next to init) |
| `FUN_004536cb` | sim step A          | ⭐⭐ — bare-slice ported (`src/sim.{c,h}` button ring + state==0 dispatch). Non-title scenes still stub. |
| `FUN_0049966a` | sim step B          | ⭐⭐ — per-tick (2 of 2)                 |
| `FUN_0049a59e` | title sim           | ⭐⭐ — bare path ported (`src/scene_title.c::scene_title_sim`). A-press transitions still stub. |
| `FUN_004547ab` | frame render        | ⭐⭐⭐ — replaces the magenta clear stub |
| `FUN_00475270` | "init indexfile"   | ✅ ported — `src/tables*`               |
| `FUN_00498ef4` | "init daoudio"     | ⭐⭐ — confirms audio backend (DSOUND?) |
| `FUN_00474f14` | CRC hash           | ✅ ported — `src/lnkdatas_hash.{c,h}`, `tools/extract/lnkdatas_hash.py` |
| `FUN_005041ec` | RNG reseed         | ✅ ported — `src/rng.{c,h}` (`rng_seed_from_now`) |
| `FUN_00451790` | pre-window init    | ✅ ported — `src/prewindow.{c,h}` + `src/math3d.{c,h}` |
| `FUN_0040110f` | WM_CREATE handler  | ⭐                                       |
| `FUN_00452911` | ESC-key handler    | ⭐                                       |
