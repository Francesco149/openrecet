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
| 2 | `FUN_00451790`       | —                      | very early init (TBD)                              |
| 3 | `FUN_00471050`       | —                      | thunk → `FUN_005041ec` (early init step)           |
| 4 | `FUN_00504384`/`wsprintfA` | —                | builds path → reads `recet.ini`                    |
| 5 | `FUN_0047aa30`       | —                      | (TBD)                                              |
| 6 | `FUN_0047aa31`       | `"start"`              | log no-op                                          |
| 7 | `FUN_0047a474`       | —                      | (TBD — pre-window init)                            |
| 8 | `FUN_0047aa8b(hInst, nCmdShow)` | —          | **window class register + CreateWindowEx**         |
| 9 | `ShowWindow` + `UpdateWindow` | —             | show the main window                               |
|10 | `FUN_004341fe`       | `"init strage ok"` *(sic)* | **storage init** — opens `lnkdata.bin` / `lnkdatas.bin` |
|11 | `FUN_0047ac6a`       | —                      | second-stage init                                  |
|12 | `FUN_00451863`       | `"init print ok"`      | text/print system                                  |
|13 | `FUN_0047af52`       | `"init dinput ok"`     | **DirectInput 8: keyboard + multi-joystick**       |
|14 | `FUN_00454e69`       | `"init render ok"`     | per-layer render state init                        |
|15 | `FUN_00475270`       | `"init indexfile ok"`  | index-file load                                    |
|16 | `FUN_0047c228`       | `"init fontsys ok"`    | font system init                                   |
|17 | `FUN_00498ef4`       | `"init daoudio ok"` *(sic)* | **audio init**                                |
|18 | `FUN_0047c474` *(conditional on `DAT_073dfd00`)* | — | optional audio path                  |
|19 | `FUN_0047c3a5`       | `"fontsystem ok"`      | font follow-up                                     |
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
That's the heart of the engine; it'll be one of the bigger functions to
study.

When the loop exits:
- `FUN_0047a804` — pre-shutdown
- `FUN_004349e4` — storage close
- `FUN_005036af(DAT_073dde2c)` / `FUN_005036af(DAT_073dde30)` — close two
  handles (likely `CloseHandle` thunks for mutex/event globals)
- `FUN_0047b255` — final cleanup

## Direct3D 8 device creation

`Direct3DCreate8(0xdc)` is called from `all.c:77975` — `0xdc = 220 =
D3D_SDK_VERSION` (the value DX8's headers macro-define). The return value
is stored in **`DAT_073dfcb8`** (the global `IDirect3D8 *`).

The `d3d8.dll` / `d3d8d.dll` dynamic load is at `all.c:143472-143479`
(two paired `LoadLibraryA` calls) — the classic primary + debug-fallback
pattern. The function containing that block is the d3d8 wrapper / loader;
we'll trace it next session for the full `CreateDevice` call.

## DirectInput 8 init (`FUN_0047af52` — "init dinput ok")

```
DirectInput8Create(hInst, 0x800, IID_IDirectInput8A, &g_di, NULL)
  ↓ stored in DAT_073dfcc0
g_di->CreateDevice(GUID_SysKeyboard, &g_di_keyboard, NULL)   // vtbl+0x0C
  ↓ stored in DAT_073dfcc4
g_di_keyboard->SetDataFormat(c_dfDIKeyboard)                 // vtbl+0x2C
g_di_keyboard->SetCooperativeLevel(hwnd, 6)                  // FOREGROUND|NONEXCLUSIVE
g_di_keyboard->SetProperty(DIPROP_BUFFERSIZE = 100)          // vtbl+0x18
g_di_keyboard->Acquire()                                     // vtbl+0x1C

g_di->EnumDevices(DI8DEVCLASS_GAMECTRL=4, FUN_0047b167 cb,
                  NULL, DIEDFL_ATTACHEDONLY=1)               // vtbl+0x10
for each enumerated joystick (count in DAT_073dfcd8):
    g_di_joy[i]->SetDataFormat(c_dfDIJoystick)
    g_di_joy[i]->SetCooperativeLevel(hwnd, 6)
    g_di_joy[i]->EnumObjects(FUN_0047b1f2, NULL, 3=DIDFT_AXIS|DIDFT_POV)
    g_di_joy[i]->SetProperty(DIPROP_RANGE = ±5000)           // axis 2
    g_di_joy[i]->SetProperty(DIPROP_DEADZONE = 100)          // axis 1
    g_di_joy[i]->Acquire()
```

Globals:
- `DAT_073dfcc0` — `IDirectInput8 *`
- `DAT_073dfcc4` — `IDirectInputDevice8 *` (keyboard)
- `DAT_073dfcc8…` — `IDirectInputDevice8 *[]` (joysticks)
- `DAT_073dfcd8` — joystick count

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

## Open subsystems to investigate next

| function       | tag                | priority                                |
|----------------|--------------------|-----------------------------------------|
| `FUN_0047be92` | game tick (main loop body) | ⭐⭐⭐ — heart of the engine     |
| `FUN_00475270` | "init indexfile"   | ⭐⭐ — likely bmpdata.bin loader        |
| `FUN_00498ef4` | "init daoudio"     | ⭐⭐ — confirms audio backend (DSOUND?) |
| `FUN_00474f14` | CRC hash           | ✅ ported — `src/lnkdatas_hash.{c,h}`, `tools/extract/lnkdatas_hash.py` |
| `FUN_005041ec` | early init         | ⭐                                       |
| `FUN_00451790` | very early init    | ⭐                                       |
| `FUN_0040110f` | WM_CREATE handler  | ⭐                                       |
| `FUN_00452911` | ESC-key handler    | ⭐                                       |
