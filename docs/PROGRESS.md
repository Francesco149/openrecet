# OpenRecet — Progress Log

Reverse-chronological log of meaningful changes. Auto-generation TBD once
the test harness has coverage metrics worth reporting.

## 2026-05-21 — Input poll ported (FUN_0047b73c)

First of the four tick callees now lands real code instead of a NULL
stub. `tick_callbacks.input_poll` is wired to `input_poll` in
`src/input.c`, which mirrors the engine's keyboard + multi-joystick
DInput poll, decodes raw button state, and OR's it through the
recet.ini binding table into `g_input_state[0].buttons` each frame.

**What landed:**

- **`src/input.c` — three new pure-C decoders.** `input_joystick_decode`
  fans a `DIJOYSTATE2`-like input into a 20-bit "pressed" array
  (4 D-pad bits OR'd from POV-hat + stick axes, 16 buttons). POV is
  the standard DInput angle-times-100 encoding with explicit cases
  for all 8 cardinals and diagonals; centered (-1 / 0xFFFFFFFF) gives
  zero. Stick dead-zone is fixed ±500 on `lX`/`lY` (range was set to
  ±1000 in init, so 50% deflection). `input_apply_joystick_block`
  matches binding values against a per-joystick virtual-button range
  (`0x27 + joy_idx * 0x14`) and OR's the slot's bit into the output
  mask; `input_apply_keyboard_block` does the equivalent via the
  41-byte DIK lookup at `0x005cbc2f`.
- **`src/input.c:input_dik_table[40]` + `input_binding_mask[14]`.**
  Bytes extracted via `tools/analyze/pe.py bytes 0x005cbc2f 41`.
  Binding-slot bit layout (UP=0x04, RIGHT=0x01, DOWN=0x08, LEFT=0x02,
  A=0x10..E=0x100, skill0..4=0x200..0x2000) matches downstream
  readers — verified the camera-cursor code at lines 50410-50420 of
  `all.c` reads exactly these bits.
- **`src/input.c:input_bindings_load`.** Flattens
  `recet_ini.pad[2][9]` + `skill[2][5]` into the engine's
  interleaved per-controller layout (`pad[N][0..8]` then
  `skill[N][0..4]`, 14 shorts per controller block). 4 blocks total —
  blocks 2..3 stay zero (the engine's outer joystick loop reads BSS
  past the 2-controller end; see quirk #41).
- **`src/input.c:input_poll`.** Win32 wrapper that queries each
  acquired DI device, decodes raw state via the helpers above, and
  walks the 4 (joystick) / 2 (keyboard) binding blocks. Pre-clears
  the button accumulator at poll start — at the default
  `speed=0 / 60FPS` path this is bit-identical to the engine's
  "clear after render" pattern; at higher speeds the engine
  accumulates multiple polls per render and we don't. Revisit when
  the FUN_004547ab render port lands a post-render clear hook.
- **Init-side fix.** Switched the joystick `SetDataFormat` from
  `c_dfDIJoystick` (80 bytes) to `c_dfDIJoystick2` (272 bytes) to
  match the engine's custom DIDATAFORMAT at `0x0051c4cc`
  (`dwDataSize = 0x110`). The 80-byte format would have made
  `GetDeviceState(sizeof(DIJOYSTATE2), &st)` fail with
  `DIERR_INVALIDPARAM` — the previous boot smoke didn't hit this
  because nobody was calling GetDeviceState yet.
- **`src/main.c`.** Wires `input_bindings_load(&g_ini)` after
  `input_init`, and replaces the NULL `tick_cb.input_poll` with the
  real `input_poll` function. 4 engine quirks documented (#40-43).
- **Tests.** 20 new tests in `tests/test_input_poll.c` cover POV-hat
  all 8 directions, stick dead-zone, button-high-bit-only decoding,
  binding application with per-joystick virtual base, keyboard DIK
  mapping (with default vendor bindings), and the recet.ini
  flattening round-trip. Total: 336 passing (was 316).
- **Smoke boot.** `tools/smoke-test.py --scenario boot --duration 4
  --capture`: exit=0, 4 frames captured, all solid debug magenta —
  unchanged from the pre-input-poll baseline.

**Engine quirks documented (#40-43):**
- #40: both controllers' bindings funnel into player-0's single
  output slot (`(local_8 / 2) * 0x2a` integer divide).
- #41: joystick scan iterates 4 outer binding blocks but only 2 are
  populated; blocks 2..3 read BSS zero bindings and never match.
- #42: Poll-failure retry loop checks Acquire's return against
  `DIERR_NOTACQUIRED`, a code Acquire never produces; effectively a
  single-iteration loop.
- #43: each joystick is `Poll()`'d four times per frame (once per
  binding block); port collapses to one Poll + per-block apply for
  the same bit-for-bit output.

**Deferred until the next big port:**
- Post-render input clear with multi-poll accumulation semantics —
  needs `tick.c` to grow a callback hook; lands with `FUN_004547ab`
  (frame render).
- Sim halves `FUN_004536cb` / `FUN_0049966a` — they're the first
  readers of `g_input_state[0].buttons` and will exercise this
  port end-to-end.

## 2026-05-21 — Game-tick scheduler ported (FUN_0047be92 + FUN_0047be2f)

Heart of the engine's main loop is now driven by our own code instead
of the magenta-clear placeholder. The scheduler dispatches at the
configured fixed-timestep frame rate (60 FPS by default, selectable
via the speed table at `0x005cbc58`); the four callees it hands off to
— input poll, two sim halves, frame render — are stubbed for now and
land one-per-commit.

**Subsystems landed:**

- **`src/tick.{c,h}` — FUN_0047be92 + FUN_0047be2f.**
  - `tick_step_with_now(now_ms, has_device, &callbacks, &out_sleep_ms)`
    is the pure-C dispatcher, taking the four big callees as function
    pointers so the scheduler can stand alone and tests can mock them
    under ASan. All arithmetic in 1/3 ms units (matching the engine's
    `*3` + `% threshold` residue pattern), so sub-ms frame budgets
    work without floating point.
  - `tick_step_win32(has_device, &callbacks)` is the Win32 wrapper that
    bundles QPC + Sleep on top.
  - `tick_now_ms()` mirrors FUN_0047be2f: `QPC.QuadPart * 1000ull /
    QPF.QuadPart` truncated to uint32, with `timeGetTime()` fallback
    when either QPC value reads zero.
  - Speed-threshold table `g_tick_speed_thresholds[5]` extracted via
    `tools/analyze/pe.py bytes 0x005cbc58 32` and verified
    byte-for-byte against the engine.
  - All scheduler globals (`now_ms`, `prev_ms`, `delta_thirds`,
    `leftover_thirds`, `speed`, `pending_speed`, `state`, `state_alt`,
    `state_seed`, `frame_count`, `flag_dddd0`, `flag_dddfa`) live in
    a `g_tick` struct with named members matching the engine's
    DAT_073de618.. / DAT_073dfca4.. / DAT_0438ccd8.. globals.
- **`src/main.c` — main loop now drives the scheduler.** Replaced the
  `tick_and_present()` placeholder call with
  `tick_step_win32(g_d3d && g_dev, &tick_cb)`. The old debug-magenta
  clear/draw/capture/present body now lives in `frame_render_stub`,
  which is passed as the `render` callback — same visible behaviour,
  but now exercised through the real dispatcher. The other three
  callbacks (`input_poll`/`sim_a`/`sim_b`) are NULL until their ports
  land — the scheduler tolerates NULL callbacks.

**Behavioral validation:**

- 316 unit tests pass under ASan/UBSan (was 298). 18 new tests for
  `tick.c`:
  - Speed-threshold table bytes vs `.rdata` dump.
  - First-frame huge-delta normalisation (prev=0 → one tick + leftover=0).
  - Sim-loop count vs latched speed (`speed=0` → 1 sim, `speed=4` → 5).
  - Adaptive-sleep band (delta=29..40 in 1/3 ms steps; sleep_ms = 5, 4,
    1, 1, 0=busy-spin at the boundary).
  - Steady-state 60 FPS residue accumulation (delta=51 each frame with
    threshold=50 carries 1, 2, 3, … in `leftover_thirds`).
  - Input poll firing at ≥1/60 s delta but NOT when delta is smaller.
  - State machine: state=1 skips sim/render (but commits leftover/prev),
    state=2 transitions to 1 after one tick, state_alt mirrors state_seed.
  - `has_device=0` early-return after sim, before render (engine order).
  - Per-frame flags clear on tick, persist on delayed pass.
  - Pending-speed latches at the top of the next frame, not mid-frame.
  - NULL callbacks are safe (shell-port scaffolding).
- Boot smoke (`./tools/smoke-test.py --target openrecet --scenario boot
  --duration 3 --capture`): `exit=0, 3 frames`. Captured frames are
  solid debug magenta (160,32,96) at 1024×768 — visually identical to
  the pre-scheduler boot, just driven by `tick_step_win32` now.

**Engine quirks documented:**

- **Speed-threshold lookup is OOB-unsafe.** `(&DAT_005cbc58)[DAT_0438ccd8]`
  has no bounds check; the engine relies on the unmapped F-key handler
  only ever writing values in `[0..4]`. Test for `speed = -1`
  intentionally skipped — would force ASan to read OOB into adjacent
  globals.
- **Dead clamp in adaptive sleep.** Inside `if (remaining < 0xb)` the
  engine has `if (0x1e < remaining) remaining = 0x1e;` — unreachable
  given the outer guard (remaining is already < 11). Preserved as a
  comment in `src/tick.c`; harmless leftover from an earlier formula.
- **`state_alt = state_seed` is a no-op at boot.** Both globals are
  BSS-zero, so the per-frame copy doesn't do anything in practice. We
  preserve the write for byte-identical behaviour once whichever code
  writes `state_seed` lands.

**Deliberate divergences:**

- The four big callees (FUN_0047b73c input poll, FUN_004536cb /
  FUN_0049966a sim halves, FUN_004547ab frame render) are NULL stubs
  in this commit. The render callback is filled in by
  `frame_render_stub` (the old magenta-clear path) to preserve the
  visual smoke-test signal until FUN_004547ab lands.
- Pure-C scheduler entry takes callbacks as function pointers, where
  the engine has direct calls. Necessary for ASan-clean testing and
  to keep `tick.c` decoupled from the four big functions; once they
  all land we could fold them into direct calls again, but there's no
  real upside.
- Engine writes to `DAT_0438ccd8` and `DAT_0438ccdc` from an unmapped
  F-key handler. Our `g_tick.pending_speed` stays 0 until that
  handler lands — meaning we always run at the 60 FPS target.

**Not in this commit (deferred):**

- `FUN_0047b73c` — input poll. 325 lines of keyboard + joystick state
  read with POV-hat angle decoding (centidegree values 4500/9000/
  13500/18000/22500/27000/31500 → direction bits). Next.
- `FUN_004536cb` / `FUN_0049966a` — the two sim halves. 322 / 267
  lines respectively. Will read decomp before scoping.
- `FUN_004547ab` — frame render. 303 lines. Replaces the magenta-clear
  stub with the engine's real Clear+BeginScene+...+Present sequence;
  likely drives the 24 render-layer objects already initialised in
  `src/layers.c`.

**Files:**

- new `src/tick.{c,h}`, `tests/test_tick.c`
- updated `src/main.c` (include tick.h, replace tick_and_present with
  tick_step_win32 + rename old body to frame_render_stub),
  `tests/Makefile`, `tests/test_main.c`,
  `docs/findings/winmain-and-bootstrap.md` (new §"Game tick scheduler"
  + main-loop annotation + open-subsystems table refresh)

## 2026-05-21 — Pre-window block closed: RNG + math3d + FUN_00451790

Closes the last three open steps in the WinMain pre-window chain (steps
2, 3, and 5 from `docs/findings/winmain-and-bootstrap.md`). After this
commit, every call between `timeBeginPeriod` and `create_main_window` is
either ported or documented as a deliberate no-op.

**Subsystems landed:**

- **`src/rng.{c,h}` — engine LCG + time-to-seed.** Reimplements
  FUN_005041f6 (`x = x * 0x343fd + 0x269ec3; return (x >> 16) & 0x7fff`),
  FUN_00471089 (`rand / 32768.0` unit float), FUN_0050bcff (time → seed
  scalar with tzset-style constants pulled from `DAT_006038d0`: TZ
  offset 28800s, DST bias -3600s, epoch literal 0x7c558180), and a Win32
  wrapper for FUN_005045eb that bundles `GetLocalTime` +
  `GetTimeZoneInformation` → DST flag → seed write. The engine's RNG
  constants are bit-identical to MSVC's `rand()` so the first values
  from seed=1 are the canonical 41 / 18467 / 6334 / 26500 / 19169
  sequence — covered by a unit test (one of those compiler-fingerprint
  facts that's nice to have pinned).
- **`src/math3d.{c,h}` — vec3/mat4 helpers.** Portable C
  implementations of `vec3_normalize` (FUN_004a1f67),
  `mat4_lookat_rh` (FUN_004a3b52), `mat4_perspective_fov_rh`
  (FUN_004a3ee8), and `mat4_mul` with internal-temp aliasing support
  (thunk_FUN_004a2a03 = D3DXMatrixMultiply). The engine reaches D3DX
  through `FUN_004cdd9f`'s indirect-dispatch table (x87 / MMX / SSE
  backends selected at boot); we use a single portable implementation
  since algebraic equivalence is what matters at this layer.
- **`src/prewindow.{c,h}` — FUN_00451790 (WinMain step 2).** Writes the
  six named globals: `flag_b1c4=0, flag_b8cc=0, camera=(10,61,-203),
  flag_b1c0=1, flag_bf84=0, flag_bf88=0`. Then runs FUN_00404e44
  (8544-entry object table — each 32-byte entry gets field0=0, y=1.0,
  field12=0 written; other 5 dwords stay BSS-zero) and FUN_00452569
  (100 randomized particles, 6 rand calls + alive=1 per particle =
  600 LCG steps total). Finally constructs the boot view+projection
  matrices: lookat with degenerate eye=target=(0,0,0) (`DAT_06a47110`
  is BSS-zero at this point in WinMain — see the engine quirk note
  below) and perspective with fov=π/4, aspect=4/3, near=10, far=2000.
- **`src/main.c` — wiring.** Pre-window block now reads:

  ```c
  timeGetDevCaps + timeBeginPeriod(min);
  prewindow_init();      // step 2 — particle table from seed=1
  rng_seed_from_now();   // step 3 — reseed for game-tick randomness
  timeBeginPeriod(10);
  // step 4: recet.ini path build (already done)
  // step 5: FUN_0047aa30 — empty stub (intentionally omitted)
  // step 6: log no-op
  recet_ini_load(...);   // step 7
  create_main_window();  // step 8
  ```

**Behavioral validation:**

- 298 unit tests pass under ASan/UBSan (was 271). New tests:
  - 9× rng — sequence vs MSVC, time-seed determinism, year-range
    rejects ([0x46, 0x8a] = 1970..2038), leap-year bumps (Feb→Mar +86400s).
  - 9× math3d — lookat translation correctness, perspective field map,
    matmul with output aliasing.
  - 9× prewindow — named globals, object table first/last + zeros at
    untouched fields, particle alive flags, value-range checks
    (pos.x/y ∈ (-5,5), pos.z ∈ (-17.5,-12.5), rot ∈ ±π/20), particle 0
    bit-exact against hand-computed seed-1 reference, post-init RNG
    state matches 600 manual LCG steps, proj-matrix field values, view
    contains NaN/inf (degenerate-input documentation).
- Boot smoke: exit=0, all 17 tables load, recet.ini loads, window
  1024×768. No visible regression on the magenta-clear+sprite tick.

**Engine quirks documented (and faithfully reproduced):**

- **Particle randomization runs before the time-based reseed.** WinMain
  step 2 (FUN_00451790) consumes the RNG with its `.data` initial value
  of 1 *before* step 3 (FUN_005045eb) replaces the seed. So the 100
  particles end up identical every boot — same sub-pixel jitter on
  whatever effect ends up consuming them. Almost certainly deliberate:
  developers wanted the deterministic boot scene without wiring a
  separate RNG.
- **Lookat eye position `&DAT_06a47110` is in the BSS-uninitialised
  region of .data.** Raw size in the unpacked binary (0xdbe00) doesn't
  cover that VA — so the vector reads as (0, 0, 0) when FUN_00451790
  runs. Combined with target=(0,0,0) that makes the lookat
  mathematically degenerate (zaxis tries to normalise (0,0,0) →
  divide-by-zero → NaN/inf). Engine produces a garbage view matrix at
  this point and never reads it — a later in-game camera setup
  overwrites it before any vertex transform consumes it. Port
  reproduces the call as-is; one prewindow test pins the
  NaN-or-inf-somewhere expectation so a future "let's clean up the
  garbage matrix" refactor would have to deliberately stomp on it.
- **FUN_0047aa30 is a 1-byte empty stub.** Vestigial leftover from a
  removed log call between init phases (FUN_0047aa31 is similarly
  empty — the one documented as the release-build logger). Port
  intentionally omits the call.

**Deliberate divergences:**

- The engine's `FUN_005045eb` caches the last-checked UTC year/month/
  day/hour/minute and skips `GetTimeZoneInformation` when unchanged
  — an optimisation that mattered when GTZI was slow. Port doesn't
  cache: it's called once per boot.
- The engine's matmul dispatcher (`FUN_004cdd9f`) picks between x87,
  MMX, and SSE backends at startup. Port uses a single portable C
  implementation; bit-exact match with the engine's per-CPU path
  isn't pursued (the engine itself drifts across CPUs).
- `mat4_mul` adds an internal temporary so `mat4_mul(view, view, proj)`
  works. D3DXMatrixMultiply in the official D3DX runtime does the same;
  the engine's per-CPU paths may or may not. Safer to do it
  unconditionally.

**Not in this commit (deferred):**

- Consumers of the camera globals (`DAT_0438cd64..6c`) and the
  particle table. We've found the initialiser but no reader of the
  particle pos/rot/alive arrays yet — they likely feed an as-yet-
  unported render path (title screen effect? loading-screen flair?).
  When that reader lands, it will reuse `g_prewindow.particle_*`
  directly and rename the field accessors at that point.
- Consumers of the 8544-entry object table at `DAT_00605214`. Same
  story — initialiser-only port; `struct prewindow_object` has named
  fields for the three writes but the other 5 dwords stay as `pad08`
  / `pad16_28` until we find a real consumer.

**Files:**

- new `src/rng.{c,h}`, `src/math3d.{c,h}`, `src/prewindow.{c,h}`
- new `tests/test_rng.c`, `tests/test_math3d.c`, `tests/test_prewindow.c`
- updated `src/main.c` (call order before create_main_window),
  `tests/Makefile`, `tests/test_main.c`
- updated `docs/findings/winmain-and-bootstrap.md` (steps 2/3/5 closed)

## 2026-05-21 — `recet.ini` reader ported (FUN_0047a474, pre-window init)

**Subsystems landed:**
- `src/recet_ini.{c,h}` — pure-C parser for FUN_0047a474
  (`docs/decompiled/by-address/47a474.c`). Handles 33 keys: a
  2×9 pad grid + 2×5 skill grid under `[option]` (formatted-key
  match on `padNM`/`skillNM`), 22 `[setup]` scalars (`winmode`,
  `screen`, `fps`, `windowpos`, etc.), 1 `[debug]` key (`camfree`),
  and 2 `[config]` keys (`se`/`mu`, clamped to `[0,9]`).
  Pre-baked defaults match the byte tables at `0x005c81d8` (pad)
  and `0x005c8204` (skill) in the unpacked binary, with the engine's
  `+1` adjustment baked in.
- Win32 entrypoints `recet_ini_default_path()` (mirrors the engine's
  `_splitpath(argv[0]) + wsprintfA "%s%s/recet.ini"` dance via
  `GetModuleFileNameA` + tail-strip) and `recet_ini_load()` (fopen+
  fread+parse). Parser stays pure-C so ASan tests run on Linux.
- `src/main.c` — `recet.ini` now loaded in `WinMain` **before**
  `create_main_window` (matching engine step 7 in `winmain-and-
  bootstrap.md`). `g_windowed` and the window's initial RECT now
  come from `g_ini.winmode` / `g_ini.width` / `g_ini.height`;
  same with the D3D `BackBufferWidth`/`Height`. Boot trace logs
  `recet.ini: winmode=1 screen=2 (1024x768) se=9 mu=9` against the
  vendor file.
- `tests/test_recet_ini.c` — 14 unit tests covering: empty-input
  defaults, default pad/skill tables byte-for-byte, all four
  `screen`→(w,h) branches incl. fallthrough, every `[setup]`
  scalar in engine order, `[option]` grid override, case-insensitive
  section+key match, `;`/`#` comments + blank lines, whitespace
  around `=`, **bgnodisp auto-derives from easydisp (quirk #37)**,
  se/mu clamp [0,9] (over + under), unknown keys/sections ignored,
  no-trailing-newline parse, vendor recet.ini round-trip.

**Behavioral validation:**
- 271 unit tests pass under ASan/UBSan (was 257).
- Boot smoke (`./tools/smoke-test.py --target openrecet --scenario boot
  --duration 3 --capture`): `exit=0, 3 frames`. Window now opens at
  1024×768 instead of the hardcoded 800×600 — matches what the
  original Recettear opens at on this user's machine.
- Path resolution: CWD-first (matches our dev convention of
  `cd vendor/original` before invoking the exe), falls back to
  next-to-exe via `GetModuleFileNameA` for the eventual deployment
  shape where `openrecet.exe` lives alongside the data files.

**Engine quirks documented (and faithfully reproduced):**
- **`bgnodisp` is dead text — overwritten from `easydisp` (#37).**
  Vendor `recet.ini` carries `bgnodisp=0` under `[setup]` but the
  loader doesn't read it; instead, after the main read loop,
  `DAT_0438b18c = DAT_0438b19c` unconditionally aliases the field to
  `easydisp`. Any explicit value in the ini is dropped.
- **`[debug] camfree` is read twice with the same section+key (#38).**
  Two adjacent `GetPrivateProfileIntA` calls write to the same
  global; second value sticks but both calls hit the same ini
  entry. Dead duplicate code from a refactor. Port reads once.
- **Three more keys never read anywhere in the binary (#39).**
  `pfnouse`/`fontmode1`/`fontmode2` ship in vendor `recet.ini` but
  no `GetPrivateProfile*` call touches them. Likely vestigial from
  earlier engine revisions. Port silently ignores (matches Win32
  semantics for missing keys).

**Deliberate divergences:**
- Path resolution adds a CWD-relative `recet.ini` lookup before the
  engine's next-to-exe path build. Required for our dev workflow
  (exe in `build/`, data in `vendor/original/`); behaviour identical
  for a deployment where the exe sits alongside its data.
- `recet_ini_parse` uses an in-process INI tokenizer instead of
  per-key `GetPrivateProfileIntA` calls — same semantics for every
  key in the engine's read set (case-insensitive lookups, `atoi`
  parsing, defaults on missing key). The only edge case we don't
  match is Win32's `0x` / `0` → hex/octal prefix handling; not used
  anywhere in vendor `recet.ini`.

**Not in this commit (deferred):**
- **`FUN_0047a804` shutdown save-back** (`[config] se`/`mu` always,
  `[setup] winx`/`winy` when `windowpos != 0`). Belongs to the
  shutdown chain — lands when that whole chain is ported.
- **Consumption of `pad[]`/`skill[]`** by the input subsystem. The
  values are loaded into `g_ini` but `src/input.c` currently only
  initialises DInput devices; wiring lands with the input-poll port.
- **`FUN_00451790`** (early camera/particle math init, step 2 of
  WinMain). Sized small in decomp (36L) but pulls in
  lookat/perspective/matmul/normalize/RNG helpers — deferred to a
  later milestone where those helpers earn their keep with real
  rendering.

**Files:**
- new `src/recet_ini.{c,h}`, `tests/test_recet_ini.c`
- updated `src/main.c` (load + wire into window/D3D init order),
  `src/Makefile` (picks up `*.c` automatically — no edit needed),
  `tests/Makefile`, `tests/test_main.c`
- new `docs/formats/recet-ini.md`
- new entries (#37, #38, #39) in `docs/findings/engine-quirks.md`
- updated `docs/findings/winmain-and-bootstrap.md` step 7

## 2026-05-21 — Phase B [+1]: `idx/stage.idx` parser

**Subsystems landed:**
- `src/tables_stage.{c,h}` — pure-C parser for FUN_00475270
  block #1 (`docs/decompiled/by-address/475270.c` L55..L329 +
  L3174..L3957 — the largest table parser in the loader by far,
  ~1000 lines of decomp). Defines the 21-record stage table
  (`stage:0-1`..`0-5` + `stage:1-1`..`1-16`) at base `&DAT_068dd2f8`,
  stride 0x1b3c = 6972 bytes. `_Static_assert` guards on 24 critical
  field offsets + total record size.
- `src/tables.c` — replaced the stage.idx stub with the real
  loader. No new resolver wiring (stage.idx is self-contained —
  no cross-table refs). Boot trace logs `(stages=S maps=M
  mapcameras=MC sunpos=S1 sunset=S2 moonpos=MP)`.
- `tests/test_tables_stage.c` — 28 unit tests covering: byte-offset
  layout, empty input, comments/blanks, lines-before-header
  dropped, all 21 stage-ID dispatch entries (both 3-byte and
  4-byte forms), unknown-ID fallback (quirk #34), every shape
  class (int / int→float / float / flag / string / slot string /
  int×3 / float×3 / float×2-colon / float×2-space), sunpos numeric,
  sunpos:off short-circuit, sunset numeric, **sunset:off broken
  (quirk #36)**, **moonpos shared coords (quirk #35)**, multi-record
  threading, no-trailing-newline EOF, map[] slot overflow safety,
  mapcamera[] threading, and a vendor-shape miniature integration
  smoke.

**Field key inventory:** 57 fully-dispatched keys covering map
geometry, camera, lighting (directional + ambient + maplight pairs),
water surfaces, weather flags, fog/colour ramp, and misc. ints. All
documented with their byte offset, type, default value, and source
key in `docs/formats/data-text.md`.

**Behavioral validation:**
- 257 unit tests pass under ASan/UBSan (was 229).
- Boot smoke: `idx/stage.idx — 22434 bytes (stages=20 maps=219
  mapcameras=0 sunpos=15 sunset=0 moonpos=0)`. Cross-checked:
  vendor file has exactly 20 `stage:` headers, 219 uncommented
  `map:` lines (`/map:...` comment lines correctly skipped), 15
  `sunpos:N:N:N` numeric lines, 5 `sunpos:off` short-circuits
  (mode=0, not counted in the sunpos= tally), 0 `sunset:` or
  `moonpos:` lines.

**Engine quirks documented (and faithfully reproduced):**
- **Unknown stage IDs alias to `1-16` (#34).** The chain-default
  `uVar5 = 0x14` collides with the last entry's index, so a
  typo'd `stage:foo` opens a record indistinguishable from a
  real `stage:1-16` on read-back. Dormant in vendor.
- **`moonpos:` shares X/Y/Z storage with `sunpos:`/`sunset:` but
  not the mode flag (#35).** Only `sunpos:`/`sunset:` touch
  `sunpos_mode`; `moonpos:` sets a separate `moonpos_set` flag and
  overwrites the sun coords. A record with both sunpos and moonpos
  ends up with sunpos's mode and moonpos's coords. Dormant in vendor.
- **`sunset:off` is broken (#36).** The "off" short-circuit
  compares against the literal string `"sunpos:off"` (the binary
  has two interned copies of `"sunpos:off"` at 0x005cab4c and
  0x005cab80 — but no `"sunset:off"` anywhere), so a real
  `sunset:off` line falls through to the numeric path. Dormant
  in vendor.

**Safety divergences (documented, not present in engine):**
- `map:` slot cap (engine bumps count unconditionally; port stops
  writing past slot 19 to avoid clobbering the minimap field).
- `mapcamera:` slot cap (engine bumps count unconditionally; port
  stops writing past slot 1 to avoid clobbering mapcamera_count).
- Post-loop unrelated globals (13 writes to `_DAT_0438cc6c..`) are
  player-inventory defaults, not stage state — deferred to the
  gameplay-state init port.

**Files:**
- new `src/tables_stage.{c,h}`, `tests/test_tables_stage.c`
- updated `src/tables.c`, `tests/Makefile`, `tests/test_main.c`
- new docs section in `docs/formats/data-text.md`
- new entries (#34, #35, #36) in `docs/findings/engine-quirks.md`

**Phase B fully complete.** All 15 of the originally-tracked Phase B
files (14 named + the tuto loop counted as 1) had parsers landed
in the 2026-05-20 sweep; this commit closes out the remaining
`stage.idx` stub — file 0 of the engine's load order, deferred at
the time because of its size. The full loader chain is now end-
to-end real: no stubs remain in `tables.c`.

## 2026-05-20 — Phase B [15/15]: `data/enemylist.txt` parser

**Subsystems landed:**
- `src/tables_enemylist.{c,h}` — pure-C parser for FUN_00475270
  block #14 (`docs/decompiled/by-address/475270.c` L2581..L2899).
  Two engine globals populated: a 10×60 grid of 752-byte
  `enemylist_section_t` at `&DAT_0053f8e8` (451200 bytes), and a
  10-dword wisp drop table at `&DAT_073d8630`. Each section carries
  `floor_lo`/`floor_hi` + 31 enemy slots (`{enemy_id, variant,
  count}`) + 31 drop slots (`int32_t item_id[3]`). `_Static_assert`
  guards on all four major byte offsets + the total 0x2f0 stride.
- `src/tables.c` — replaced the enemylist.txt stub with the real
  loader. Reuses the existing `resolve_via_item_state` adapter
  (already wired for enemy.txt and gousei.txt drop resolution).
  Boot trace logs `(sections=S enemies=E drops=D resolved=R
  wisps=W wisp_resolved=WR)`.
- `docs/formats/data-text.md` — appended full enemylist.txt
  section: 5-way line dispatch, sticky state semantics
  (dungeon-slot / section index / enemy slot), section byte-layout
  table, longest-prefix enemy-name lookup vs the pre-baked
  `g_enemy[]`, item-name → id resolution via `tables_item_resolve`,
  faithfully-reproduced quirks (#21 reused, plus new #31/32/33),
  vendor file shape.
- `docs/findings/engine-quirks.md` — added quirks #31 (10 dungeon
  slots reserved, only 6 keyed), #32 (`wisp10:` lands on the `:`
  byte and silent-drops), and #33 (slot-30 terminator hazard
  clobbers slot-0 drop ID — dormant in vendor).
- `tests/test_tables_enemylist.c` — 22 cases: byte-offset
  layout sanity, empty input, comments/blanks, wisp basic /
  empty / wisp10 silent-drop / unknown-item, dungeon-header
  resets section index, `f:N` single-floor, `f:` empty +
  loop-err-16 path, multiple `f:` lines thread sections, enemy
  basic (one drop), multi-drops (up to 3), variant `(N)` suffix,
  count `xN` suffix, longest-prefix wins, unknown enemy name
  skipped, per-line drop reset, NULL resolver yields -1, no-
  trailing-newline EOF, enemies thread across consecutive `f:`
  blocks, end-to-end vendor-shape integration smoke.

**Behavioral validation:**
- 229 unit tests pass under ASan/UBSan (was 207).
- Boot smoke: `data/enemylist.txt — 28281 bytes (sections=100
  enemies=696 drops=1118 resolved=1118 wisps=4 wisp_resolved=4)`.
  100 floor-sections matches the 100 `f:` lines in the vendor
  file. 4 populated wisps = vendor's `wisp3..wisp6` (the parser
  honours `wisp1:`/`wisp2:` ship-empty by leaving slots 0/1 at -1).
  All 1118 drop references resolved to real item ids via the
  shared `g_item` table.

**Engine quirks documented (and faithfully reproduced):**
- **10 dungeon slots, only 6 keyed (#31).** Init scrubs all
  10×60 = 600 sections to `floor_lo = -1`, but the SJIS key
  chain at L2690..L2702 only matches `ダンジョン１..６`. Slots
  6..9 are dead storage with no possible writer.
- **`wisp10:` silent-drops (#32).** Init reserves 10 wisp dwords,
  but the name-copy loop reads from `line[6]` — which is the
  trailing `:` for `wisp10`, terminating the copy immediately.
  Slot 9 storage exists but no `wispN:` line can populate it.
  Vendor only ships `wisp1..wisp6`.
- **Slot-30 terminator hazard (#33).** Engine writes `enemy_id
  = -1` to slot `local_18 + 1` after each enemy line. If a
  section hits 30 enemies, the terminator lands at slot 31's
  enemy_id field — which is the first drop dword of slot 0.
  Vendor never gets close (max ~12 per f-block). Port logs
  overflow + skips the line rather than clobbering drops[0].
- **Per-line drop reset.** drops[slot].item_id[0..2] reset to
  -1 at line start so a line with fewer drops than the previous
  one doesn't inherit stale ids.
- **State sticky across lines.** Dungeon slot, section index,
  enemy slot all persist until the next header. An enemy line
  emitted before any `ダンジョン`/`f:` lands in dungeon 0 /
  section 0 — vendor never does this.
- **`f:N` (no dash) → `floor_hi = floor_lo`.** Dash-scan stops
  on `\r`/`\n`; the second atoi never runs.
- **`f:` (empty) → "loop err 16" + line skipped.** Engine writes
  `atoi("") - 1 = -1` to floor_lo BEFORE bailing — leaving the
  section in a half-init state. Port preserves the write.
- **Effective-exact item-name lookup.** Engine's double-`FUN_00479f4d`
  pattern (memcmp twice, once with each side's strlen) behaves
  like exact match. Port routes through `tables_item_resolve`
  which is strncmp-up-to-32.

**Phase B complete.** All 14 file parsers (counting the 3-file
tutorial loop as one) plus the resolver-wiring follow-up are
landed. Remaining `tables` work for OpenRecet's surface mapping:
`stage.idx` (still a stub at `load_stage_idx`, 22434 bytes —
likely Phase C). Phase 3 next milestone candidates to confirm
with user at session start.

## 2026-05-20 — Phase B [14/15]: `data/news.txt` parser

**Subsystems landed:**
- `src/tables_news.{c,h}` — pure-C parser for FUN_00475270 block
  #11 (`docs/decompiled/by-address/475270.c` L1583..L2236). One
  global at `&DAT_056e0e00`, stride 0xbc (188 bytes), no engine
  cap on count (port reserves 100 slots). Each record carries a
  128-byte body, 16-byte name (parser CAN write 20 → overflows
  into rate, quirk #27), `rate` / `price_lo` / `price_hi`, the
  three lookup results (`attr_mask` / `category` / `item_id`)
  with their sentinel values, the sticky `target_group` from
  `対象者:`, optional `days_lo` / `days_hi`, and the sticky
  `period_start` / `period_end` from `時期:`. `_Static_assert`
  guards on all 13 field offsets.
- `src/tables.c` — replaced the news.txt stub with the real
  loader. Two new resolver adapters `news_resolve_category` and
  `news_resolve_item` prefix-match (engine `FUN_00479f4d`-style)
  against `g_item.categories[].singular` and
  `g_item.records[].singular` respectively, both wired through
  `tables_parse_news`. Boot trace logs
  `(news=N dash=D special=S attr=A category=C item=I)`.
- `docs/formats/data-text.md` — appended full news.txt section:
  file shape, sticky-header semantics, data-row layout with
  optional days range, name-resolution lookup chain (special →
  attr → category → item), record byte-offset table, all faithfully-
  reproduced quirks, vendor file shape.
- `docs/findings/engine-quirks.md` — added quirks #27 (name buffer
  overflow into rate), #28 (prefix-by-name-length lookup, not
  exact match), #29 ("-" rows leave target_group / item_id /
  days_lo / days_hi at BSS-zero), and #30 (body retains trailing
  `\r` on CRLF lines).
- `tests/test_tables_news.c` — 20 cases: empty input, byte-offset
  layout sanity, comments/blanks, `特殊` sentinel, SJIS attr-mask
  hit (`武器` / `防具`), category resolver hit (`Daggers`), item
  resolver hit (`Candy`), lookup-chain precedence (attr wins over
  cat over item), days-range optional, `-` row with all the BSS-
  zero defaults, sticky `target_group` across rows, sticky
  `period_start` / `period_end` across rows, period defaults
  (0, 100) before any header, malformed `時期,A` (no `-`) leaves
  `period_end` unchanged, no-trailing-newline EOF, body retains
  `\r` on CRLF, body strips on LF-only, resolver miss is silent
  (logs but counts), max-records cap, end-to-end vendor-shape
  integration smoke.

**Behavioral validation:**
- 207 unit tests pass under ASan/UBSan (was 187).
- Boot smoke: `data/news.txt — 6342 bytes (news=80 dash=43
  special=2 attr=22 category=12 item=1)`. Each bucket
  cross-checked against an independent Python re-count of the
  vendor file, with the only discrepancy being `アクセサリー` —
  it matches the SJIS attr tag `アク` (0x83 0x41 0x83 0x4e, bit
  0x0010), which the Python re-count's curated attr-tag list
  initially missed. Port matches the engine.

**Engine quirks documented (and faithfully reproduced):**
- **Name buffer can overflow into rate.** Parser caps the
  name-write loop at 20 bytes, but the structural field is 16
  bytes (rate follows at +0x90). For names ≥ 16 bytes the NUL
  terminator lands in rate / price_lo / category. Dormant in
  vendor (longest name = `アクセサリー` at 12 bytes).
- **Lookup chain is prefix-by-name-length.** All three name
  lookups (special / attr / category / item) use
  `memcmp(name, candidate, name_len)`. A short news.txt name
  matches any candidate it's a prefix of. Vendor names always
  fully equal their candidate.
- **`-` rows leave BSS-zero fields.** The `-` branch skips the
  `target_group` / `item_id` / `days_lo` / `days_hi` writes,
  leaving them at memset-zero. Consumers that expect -1 for "no
  match" see 0 for "-" rows.
- **CRLF body keeps trailing `\r`.** Line-collect stores the
  terminating `\r` in the line buffer; body-copy stops at `\0` /
  `\n` but not `\r`. Vendor file is CRLF so every body has a
  trailing `\r` byte.
- **`時期,A` (no `-`) leaves `period_end` unchanged.** Engine
  "loop err 6"; port skips the second atoi via `strchr` miss.

**Note for the next milestone:**
- Phase B 15: `enemylist.txt` (28281 bytes — substantially
  larger than news.txt). Confirm with user at session start —
  enemylist.txt's parser block is much further down in the
  binary and may need its own discovery doc.

## 2026-05-20 — Phase B [13/15]: `data/event.txt` parser

**Subsystems landed:**
- `src/tables_event.{c,h}` — pure-C parser for FUN_00475270 block #10
  (`docs/decompiled/by-address/475270.c` L1521..L2235). 4 in-town
  location categories (広場/市場/教会/酒場), each with up to 100
  records (50-dword stride = 200 bytes); each record carries an
  event id, a "flag to set on trigger", 4 hex-encoded prereq slots
  (lowercase `0..9/a..f`, with sticky `-` → -1), first/max weekday-
  of-day index (NOT a bitmask like kyaku.txt — single 0..3 indices
  for 朝/昼/夕/夜), 20 day-range pairs, a `loop_min` gate, and a
  `decay_or_max` field that pre-bakes to 100000 for the seed record
  and 0 for all parsed records. `_Static_assert` guards on every
  field offset so the layout stays byte-identical to the consumer
  `FUN_0045de68`'s negative-offset reads.
- `src/tables.c` — replaced the event.txt stub with the real loader.
  Boot trace logs
  `(hiroba=H ichiba=I kyokai=K sakaba=S with_prereqs=N)`. No
  resolver wiring needed — `event.txt` has no cross-table lookups.
- `docs/formats/data-text.md` — appended full event.txt section:
  category-header table, record layout with field offsets, data-line
  shape annotated, prereq encoding (hex + sticky -), weekday-of-day
  tag table with the 1-byte-mismatch quirk, day-pair format,
  pre-baked default record, all faithfully-reproduced quirks,
  vendor file shape.
- `docs/findings/engine-quirks.md` — added quirk #26 ("`event.txt`'s
  weekday-tag mismatch advances 1 byte, not 2") with the engine
  decomp snippet and dormant-but-real explanation.
- `tests/test_tables_event.c` — 15 cases: empty-seeds-default,
  byte-offset layout sanity, comments/blanks, basic 広場 record,
  prereq hex+minus, time_first/max tracking, time_max clamps to
  no-higher, unknown-only tokens leave 0/0, loop_min atoi, 20-pair
  cap, all 4 categories dispatched, pre-header data-line goes to
  広場, decay_or_max=100000 only for seed, no-trailing-newline,
  vendor-shape integration smoke.

**Behavioral validation:**
- 187 unit tests pass under ASan/UBSan (was 172).
- Boot smoke: `data/event.txt — 8901 bytes (hiroba=39 ichiba=9
  kyokai=9 sakaba=19 with_prereqs=76)`. The four counts match an
  independent Python re-count of the vendor file's headered
  sections + data lines, including the pre-baked seed contributing
  1 to 広場. Every record has `prereq[0] >= 0` (vendor convention:
  the "must NOT be set" flag is always populated).

**Engine quirks documented (and faithfully reproduced):**
- **Pre-baked record 0 of category 0.** Before parsing, the engine
  hand-writes a "default 広場 event" with id=0x0b, prereq[0]=0xa3,
  time 0..1, day range (0,40), loop_min=0, decay_or_max=100000.
  Sets `counts[0] = 1`, so the first parsed 広場 line lands at
  slot 1.
- **Lines before any header dispatch to 広場.** Init leaves
  `local_18 = 0` (= 広場). Vendor data has 広場 as the first
  header so this is dormant.
- **Hex-only prereq with sticky `-`.** `:`-delimited fields accept
  lowercase `0..9/a..f` only; any byte that's not hex/`-`/`:` is
  silently skipped (e.g. leading spaces). A `-` anywhere in a
  field's tail nukes that field to -1 regardless of any hex value
  accumulated before it. So `100` = 0x100 = 256, `-1`/`-2`/`f-f`
  all = -1.
- **Weekday-tag mismatch advances 1 byte, not 2.** New quirk #26.
  Unknown 2-byte SJIS chars get scanned twice (once at byte 0,
  once at byte 1). Dormant in vendor data thanks to full-width-
  space padding `81 40`.
- **`time_first == 0` is overloaded.** "No matched token" and
  "first matched token was 朝" both result in `time_first = 0`.
  Consumer interprets as "morning-only" in both cases.
- **End-of-list sentinel.** Loader writes `id = -1` to the slot
  one past `counts[cat]` for each category — the consumer's loop
  terminator.

**Note for the next milestone:**
- Phase B 14: `news.txt` (6342 bytes, ~330 C lines in 475270.c
  block #11) — pre-categorised by `対象者:`/`時期:` headers, then
  `品名,カテゴリ,価格-高値,日数-日数` data lines (5 fields with
  comma + dash separators). Larger than event.txt, but still
  smaller than the average tables file.

## 2026-05-20 — Phase B [12/15]: `data/kyaku.txt` parser

**Subsystems landed:**
- `src/tables_kyaku.{c,h}` — pure-C parser for FUN_00475270 block #4
  (`docs/decompiled/by-address/475270.c` L469..L832). 18 active
  records out of 50 in vendor data; each record carries a
  singular/plural name, name-table index, 2-axis attribute pair,
  up-to-20 preferred item categories, preferred-attribute bitmask
  (uses the same 16-tag SJIS table as oder/item), budget range,
  activity-time mask (朝/昼/夕/夜 → bits 1/2/4/8), 6 haggle-tuning
  ints, and a per-customer dialog-file path.
- `src/tables.c` — replaced the kyaku.txt stub with the real loader.
  New `resolve_via_item_category` adapter resolves `好き種類:` lines
  against `g_item.categories[].singular` (populated earlier by
  item.txt's category headers). Boot trace logs
  `(customers=N like_kinds=K with_budget=B)`.
- `docs/formats/data-text.md` — appended full kyaku.txt section: per-
  line dispatcher table, header singular/joint quirk details, the
  activity-time and preferred-attribute token tables, all 8
  faithfully-reproduced quirks, vendor file shape.
- `tests/test_tables_kyaku.c` — 23 cases covering: empty input,
  comments / blanks, header singular-only + with-plural, attr X/Y
  (full + empty), budget range (full + empty), like-kind resolver
  hit / null-resolver-skip / 20-cap, like-attr SJIS mask, the
  `嫌い:` orphan-noop, file_path copy, activity-time (all 4 tokens,
  partial, unknown token), atoi scalars, lines-before-header
  dropped, no-trailing-newline, multi-customer threading,
  resolves-via-item-category end-to-end with a hand-populated
  `item_state_t`, and a vendor-shape integration smoke.

**Behavioral validation:**
- 172 unit tests pass under ASan/UBSan (was 149).
- Boot smoke: `data/kyaku.txt — 7603 bytes (customers=18
  like_kinds=111 with_budget=15)` matches the vendor file (manual
  count of `好き種類:` lines totals 111; only Recette / Tear /
  Euria have empty `予算:` → 18 - 3 = 15 with-budget).

**Engine quirks documented (and faithfully reproduced):**
- **`嫌い:` is an orphan match.** The 5-byte `嫌い:` key match has
  an empty body — match-but-discard. Almost certainly a dialled-back
  feature; vendor data still ships dozens of `嫌い:` lines but nothing
  consumes them.
- **Header singular/joint write-position reset.** On `NNN:S#P` the
  joint cursor resets to offset 0 at the `#`, so the plural
  *overwrites* joint[0..] starting from the beginning. If plural is
  shorter than singular, the tail of singular leaks into joint —
  but vendor data never triggers (all plurals ≥ singular length).
- **Singular NUL at off-by-five.** Engine's `puVar14[iVar17 + 5] = 0`
  writes NUL at `singular[iVar17 + 1]`, NOT `singular[iVar6 + 1]`.
  For `#`-containing headers it lands several bytes past singular's
  end. Harmless thanks to BSS zero-init.
- **Header gated by leading `0`.** Dispatcher only tries the 50-iter
  `%03d:` match if `line[0] == '0'`. Records 0..49 always start
  with `0` so this is a perf optimisation in practice; record IDs
  ≥ 100 would be silently ignored.
- **`属性:` / `予算:` unbounded delimiter scans.** Once the first
  numeric is parsed, the engine walks forward looking for `,` /
  `-` with NO upper bound. Vendor data always has the delimiter;
  the port also stops at NUL.
- **`好き種類:` cap of 20** + MessageBoxA on overflow / unknown
  category. Port logs to stderr.
- **Lines before any header are silently dropped** (engine's
  `local_14 < 0` sprintf-to-discarded-local branch).

**Resolver wiring:**
- `好き種類:` resolves through `resolve_via_item_category` (new in
  `src/tables.c`) — different from the existing `resolve_via_item_state`
  (which probes `g_item.records[].singular` for full item names).
  Kyaku resolves against the **category-name** table at
  `g_item.categories[].singular`, populated by item.txt's
  `:Category#(tag)` headers. The 111 vendor `好き種類:` lines all
  resolve successfully against the populated category table.

**Note for the next milestone:**
- Phase B 13: `event.txt` (8901 bytes, ~62 C lines in 475270.c
  block #10) — likely the next-easiest remaining file. Or `news.txt`
  (6342 bytes, 655 C lines — larger but more boxed-in to a single
  format). Confirm priority with the user.

## 2026-05-20 — Phase B [11/15]: resolver-wiring follow-up

**Subsystems touched:**
- `src/tables_enemy.{c,h}` — `tables_parse_enemy` gains an
  `enemy_resolve_fn (resolve, user)` pair, replacing the dead-stub
  `lookup_item_id` that always returned -1. NULL resolver collapses
  to the previous behaviour (tests use this).
- `src/tables_gousei.*` — already accepted the resolver; no change.
- `src/tables.c` — new `resolve_via_item_state` adapter wires
  `tables_item_resolve(&g_item, name)` into both `load_enemy_txt`
  and `load_gousei_txt`. Boot trace now reports resolution counters
  (`drops_resolved`, `outputs_resolved`, `ingredients_resolved`).
- `tests/test_main.c` — registry is X-macro-driven now (separate
  cleanup commit). 149 tests pass (was 147): two new tests cover
  the resolver wiring end-to-end (`tables_enemy_drop_resolves_via_callback`
  via stub; `tables_gousei_resolves_via_item_state` via a real
  hand-populated `item_state_t`).

**Observed boot deltas** (vendor data):
- `enemy.txt — drops_resolved=70` (was 0 — 54 enemies × ≤2 drops).
- `gousei.txt — outputs_resolved=101 ingredients_resolved=268`
  (was 0 — every recipe output name has a matching item.txt singular,
  so 100% of outputs resolve).

**Out of scope (still deferred):**
- `oder.txt` attribute-table fallback — its name table at
  `&DAT_0963e5f8` is populated by item.txt's category-header path,
  so the lookup already works; no rewire needed.
- Drop-name → item-id misses for the ~38 enemy slots that still
  resolve to -1. These are vendor-data spelling mismatches and need
  per-name investigation; out of scope for the wiring pass.

## 2026-05-20 — Phase B [10/15]: `data/item.txt` parser

**Subsystems landed:**
- `src/tables_item.{c,h}` — pure-C parser for FUN_00475270 block #3
  (`docs/decompiled/by-address/475270.c` L428..L468 main dispatch +
  L815..L829 cross-block record fallback reached via
  `goto LAB_00476d04`). The two sub-parsers are FUN_00491044
  (category header, 81 bytes) and FUN_004912de (item record, 820
  bytes). 716-byte record layout (stride 0x2cc) populated end-to-end:
  rank, price, atk, def, mt, mf, attr_mask (incl. category-class OR
  via FUN_0049eb2a), equip_class (FUN_0049ed75), stock_info[9]
  (FUN_00491095 — 7 SJIS tags incl. `ダ`'s ×10-if-<10 quirk),
  aud_mask (FUN_0049e849 — 11 SJIS audience tags including `男`/`女`
  composites), singular[64], plural[64], desc_line1[256],
  desc_line2[256]. Static asserts validate every offset.
- `src/tables.c` — replaced the item.txt stub with the real loader.
  Boot trace logs `(items=N max_id=M equippable=K cats=C)`.
- `docs/findings/item-table.md` — captures the chained-dispatcher
  discovery (the cross-block `goto LAB_00476d04` is real, not a
  decompiler artifact), the scratch-buffer flow (FUN_00491044 writes
  scratch consumed by FUN_004912de's sprintf copies), the per-record
  byte layout, and the resolver implications for the three already-
  ported parsers that defer item-name lookup (oder, enemy, gousei).
- `docs/formats/data-text.md` — appended a full item.txt section:
  per-line dispatcher table, 12-field record format, attribute /
  stock / audience tag tables, the `##`-makes-desc1-the-real-content
  semantics, vendor file shape.
- `tests/test_tables_item.c` — 23 cases covering: empty input,
  comment/blank/indent-space skipping, basic record (with and without
  `+` plural), full stat fields, category header routing,
  multi-category index threading, attribute-mask + category-class
  OR, audience tags (全 → 0xff, 男 → 0x55, リ → 0x01,
  empty-field → 0xff), stock tags (在庫(N) basic + ダ(N) ×10 quirk),
  out-of-range item_id dropped, no-trailing-newline, description-
  line1+line2 split on embedded `#`, phase-2 `/` truncation,
  unknown-line stderr fallback, resolver lookup, slot cap, and a
  vendor-shape integration test against
  `/tmp/openrecet-extract/data/item.txt`.
- `tools/analyze/pe.py` — added the `bytes` subcommand earlier;
  reused here to identify the dispatcher sentinels `':'` at
  `0x5cacf0` and `' '` at `0x5cacf4`, plus the stock-info /
  audience SJIS tag tables.

**Behavioral validation:**
- 147 unit tests pass under ASan/UBSan (was 124).
- Boot smoke: `data/item.txt — 121998 bytes (items=571 max_id=5408
  equippable=331 cats=33)` matches the vendor file's actual counts
  (Python analysis: 571 records, 33 categories, IDs 0..5408).
- ASan caught one early-iteration bug: `item_class_bits` had a
  hand-written length table (`{ "Arm Parts", 12, ... }`) that
  memcmp'd past the C string literal's bounds. Fixed by switching to
  `strlen(.name)` + exact-NUL terminator check. Test for this is
  implicit in the vendor-shape run, which would crash under ASan if
  the OOB read reappeared.

**Engine quirks documented:**
- **Cross-block dispatcher goto.** The non-`:` line path inside
  item.txt's loop is reached via `goto LAB_00476d04` that physically
  lands inside the next block's (kyaku.txt) function body. Real code
  layout, not a decompiler artifact — the port linearises it.
- **Most-recent-header semantics.** Category headers don't index
  into the per-category table directly; the next item record copies
  the scratch buffer into `categories[item_id/100]`. Vendor files
  respect the convention; an adversarial reorder would scramble the
  category-name lookup.
- **Phase-1-immediate-`#` empties desc_line1.** Vendor `##` between
  AUD and DESC means AUD is empty (engine ORs `aud_mask |= 0xff`)
  and DESC1 starts AT the byte after the second `#`. desc_line1
  ends up with the first half of the description; desc_line2 gets
  the second half after the `#` between them.
- **Description phase 2 ends on `/`.** A literal `/` in the second
  description line truncates the field. Phase 1 has no such check.

**Note for the next milestone:**
- Resolver wiring (Phase B 11 follow-up) is now unblocked. A single
  pass through `src/tables.c` can wire `tables_item_resolve` into
  the deferred hooks of `tables_parse_enemy` (drop refs, currently -1)
  and `tables_parse_gousei` (ingredient/output IDs, currently -1).
  `oder.txt`'s attribute-table lookup doesn't actually need
  resolution — its `attr_index = -1` placeholder was a misread; the
  oder parser already references the singular-name table via
  `oder_attr_hash`, and the table will be populated automatically
  now that item.txt has been parsed. Cleanup is mostly removing the
  TODO comments from `src/tables.c`.

---

## 2026-05-20 — Phase B [9/15]: `data/gousei.txt` parser

**Subsystems landed:**
- `src/tables_gousei.{c,h}` — pure-C parser for FUN_00475270 block
  #13 (LAB_004790cd / `docs/decompiled/by-address/475270.c`
  L2402..L2579). 12-dword (0x30-byte) record layout: output_id, rank,
  ingredient_id[5], ingredient_count[5]. Header-vs-recipe dispatch
  on the 7-byte SJIS `ランク:` prefix. Recipe lines skip the 5-byte
  `NNNN:` prefix wholesale (engine: `pcVar16 = local_27c + 0x25`),
  then walk colon-separated fields with `#N` count modifiers.
- `src/tables.c` — replaced the gousei stub with a real loader.
  Threads a NULL item-name resolver for now (item.txt parser hasn't
  landed); when it does, tables.c will pass a real callback into
  `tables_parse_gousei` without touching the parser. Boot trace
  logs `(recipes=N max_rank=M)`.
- `docs/formats/data-text.md` — appended a full gousei section:
  per-record layout, header dispatch, the discarded 4-digit prefix,
  the ing1-write quirk, the exact-name lookup, the index-0
  MessageBox quirk, the 200-record cap, vendor file shape.
- `docs/findings/engine-quirks.md` — added quirk #23: the
  `Master's Plate` recipe line ships without a trailing `:`, which
  trips the engine's unbounded `:` hunt past the line terminator and
  into surrounding memory. Record still commits; port detects EOL
  in the hunt and finalises the column cleanly.
- `tests/test_tables_gousei.c` — 15 cases covering empty input,
  comment/blank skipping, basic recipes, rank header dispatch,
  rank-0 recipes preceding any header, prefix-discarded behaviour,
  3- and 5-ingredient widths, NULL-resolver fall-through, unknown-
  name → -1, the EOL-without-trailing-':' recovery, no-trailing-
  newline, the 200-record cap, embedded-NUL early-exit, and a
  vendor-shape integration test.

**Behavioral validation:**
- 124 unit tests pass under ASan/UBSan (was 109).
- Boot smoke: `data/gousei.txt — 6252 bytes (recipes=101 max_rank=5)`
  matches the vendor file's actual recipe count (22+22+17+19+21 = 101
  across ranks 1..5). Pre-fix, my parser was reporting 100 — the
  missing recipe was the Master's-Plate-without-trailing-':' line,
  which my -1-return path was silently dropping; chased it down via
  per-line debug instrumentation, then replaced the bail with an
  EOL-aware fall-through.

**Note for the next milestone:**
- Item resolver hook is now the gating dependency for *several* of
  the already-ported parsers (oder.txt attribute lookup, enemy.txt
  drop refs, gousei.txt output/ingredient IDs). Once item.txt's
  parser is in, a single resolver callback wired into each loader
  will populate the long-deferred ID fields without re-touching the
  parsers.

---

## 2026-05-20 — Phase B [8/15]: `data/tuto[123].txt` parser

**Subsystems landed:**
- `src/tables_tuto.{c,h}` — pure-C parser for FUN_00475270 block #15
  (L2898..L3123). The three tutorial scripts (`tuto1.txt` /
  `tuto2.txt` / `tuto3.txt`) share a single 296-byte-per-record
  array (`g_tuto[600]`). Per-line CSV with a 16-token opcode
  dispatch (ASCII tokens `CHR0`/`CHR1`/`TAGD`/`PRID`/`PRIA`/`BUN0`/
  `GOTO`/`TAGN`/`TOUT` and SJIS keywords `値段`/`高く`/`値引`/`値上`/
  `アイテム`/`剣選択`/`初期金額決定`). Two payload families: 1 int +
  text for CHR0/CHR1, 7 ints for the price/branch opcodes, none for
  the rest. Handles the `id == -1` sentinel and `id <= -2`
  text-only branches faithfully.
- `src/tables.c` — replaced the tuto stub-loop with a real loader.
  Mirrors the engine's hard-coded 3-file iteration (no early-exit
  on miss) and logs `(records=N)` with a ⚠ when N exceeds the
  50-slot parser cap (which it does on all three vendor files).
- `docs/formats/data-text.md` — appended a full tuto section:
  opcode table, record layout, the parser-vs-consumer stride
  mismatch, vendor-data overflow numbers, and the final
  cross-overwritten array state.
- `docs/findings/engine-quirks.md` — added quirk #22 (parser stride
  50 vs consumer stride 200, both pointing at `&DAT_005d1fc8`;
  three of four `FUN_00461bf6` callers push `2` so the consumer
  reads a never-written region).
- `tests/test_tables_tuto.c` — 18 cases covering empty input,
  blank/comment skipping, every ASCII opcode, every SJIS opcode,
  the `id < 0` branches, the 7-int reader with short-arg fallback,
  the file_index×50 stride, the 50-slot overflow, and a vendor-
  shape integration test.

**New persistent tooling:** `tools/analyze/pe.py` — PE32 helper
module + CLI for the unpacked vendor exe, used for VA → file offset
mapping, NUL-terminated cp932 string dumps, raw byte / blob
extraction, and call-site discovery with PUSH-imm decoding.
Replaces the ad-hoc inline Python scripts that kept getting
reinvented for each RE session. `docs/AGENT-WORKFLOW.md` got a new
"Persistent analysis tooling" section pointing at it.

**Engine fidelity divergences (documented):** the 7-int reader on
short lines reads stack garbage in the engine; our port zeros the
line buffer between records so missing args read as 0 (benign —
gameplay code only uses `args[0]` for `GOTO`). The parser-vs-
consumer stride mismatch (quirk #22) is preserved on the parser
side; the consumer port will inherit whatever the engine actually
does at runtime.

**Boot trace** (smoke test, vendor data):
```
tables: data/tuto1.txt — 8978 bytes (records=135 ⚠ overflows 50-slot cap)
tables: data/tuto2.txt — 5828 bytes (records=90 ⚠ overflows 50-slot cap)
tables: data/tuto3.txt — 4064 bytes (records=60 ⚠ overflows 50-slot cap)
tables: tuto overflow — 3/3 files exceed the 50-slot parser cap (engine quirk: stride mismatch vs consumer)
```

**Tests:** 109 pass (was 91), 0 fail, 0 skip.

**Remaining Phase B order:** `gousei.txt → kyaku.txt → event.txt → news.txt → stage.idx → enemylist.txt → item.txt`.

## 2026-05-20 — Phase B [7/15]: `data/enemy.txt` parser

**Subsystems landed:**
- `src/tables_enemy.{c,h}` — pure-C parser for FUN_00475270 block #5
  (L834..L1026). 64 fixed enemy records at `&DAT_005c23f0` (stride
  0x68 = 104 bytes). Per-line **longest-common-prefix** match
  against the pre-baked record names, then 6 ints (HP/EXP/AT/DF/MA/MD)
  + 2 drop-item name lookups; both drops reset to -1 at line start.
  Pre-baked NAMES + boss flags live in `.data` (extracted from
  `vendor/unpacked/recettear.unpacked.exe` at file offset
  `0x1c0bf0`) and are populated via `tables_enemy_init` before the
  parser runs.
- `src/tables.c` — replaced the enemy.txt stub with a real loader.
  Init-then-parse pattern: call `tables_enemy_init(g_enemy)` to
  copy the 64 names + flags from `.data`, then `tables_parse_enemy`
  to overlay the stats. Boot trace logs `(enemies=N bosses=M)` with
  a counter that handles outlier vendor rows.
- `docs/formats/data-text.md` — appended a full enemy.txt section:
  line shape, 0x68-byte record layout, longest-prefix lookup with
  worked examples, pre-baked-record metadata, engine quirks, and
  lnkdatas-vs-overlay vendor shape (2801 vs 3589 bytes).
- `docs/findings/engine-quirks.md` — added quirk #21 (`enemy.txt`
  unmatched lines fire MessageBoxA on every boot of the original
  exe; `アルマ*` lines collapse onto a single record via the
  alias-prefix path).
- `tests/test_tables_enemy.c` — 10 cases: pre-baked init, basic
  record, longest-prefix wins (アーリマン緑 over アーリマン), shorter
  prefix when no longer match available, comments + blank lines
  skipped, per-line drop reset, unknown-name silently skipped,
  placeholder records (`name = " "`) skip match, no-trailing-newline,
  vendor-shape end-to-end with mixed-prefix routing.

**Engine fidelity divergences (documented):** the port silently
skips unmatched lines (engine pops a blocking MessageBoxA on every
one — vendor data triggers this 9 times per boot via the overlay
file's late-content lines). Drop-name → item-id resolution is
deferred until `item.txt` lands (slot #3, still a stub) — drops
resolve to -1 unconditionally. The seven runtime floats at
+0x44..+0x5f (collision/sprite-scale data, populated by
not-yet-ported runtime code) are left at zero in the port; the
engine ships them with a baked snapshot in `.data` that the parser
overwrites for stats but not these.

**Boot verification:** stderr now shows
`tables: data/enemy.txt — 3589 bytes (enemies=54 bosses=6)` against
the bmpdata overlay (which `storage_read` picks first). The 54
records match the count of unique pre-baked record names that the
overlay's 67 data lines route into via longest-prefix match. The
6 bosses come straight from the pre-baked flags table.

**Test status:** 91 tests pass (up from 81), no fails, no skips.

## 2026-05-20 — Phase B [6/15]: `data/snews.txt` parser

**Subsystems landed:**
- `src/tables_snews.{c,h}` — pure-C parser for FUN_00475270 block #12
  (L2238..L2401). Two unrelated globals populated from one file: a
  flat 64-slot name table keyed by 3-digit ID (`NNN:<text>` lines)
  and a 10×30 grid of floor-range sections keyed by SJIS dungeon
  names (`ダンジョン1`..`ダンジョン6`) with per-section weighted
  entry lists (`NNN,W` and `NON,W`). Only 6 of the 10 outer dungeon
  slots are reachable; the other 4 stay empty.
- `src/tables.c` — replaced the snews.txt stub with a real loader.
  Boot trace logs `(names=N sections=M)`, where `sections` counts
  records with non-sentinel `floor_start`.
- `docs/formats/data-text.md` — appended a full snews.txt section
  with line-shape table, the SJIS dungeon-key bytes, record layout
  for both globals, engine quirks (including the dungeon-transition
  off-by-one), and vendor-file shape with per-dungeon f: counts and
  weights.
- `docs/findings/engine-quirks.md` — added quirk #20 (snews.txt
  dungeon-transition floor-range corruption) with the full
  pointer-juggling story.
- `tests/test_tables_snews.c` — 10 cases: empty (sentinel init),
  name table (basic, empty value, overlong→truncated), comments +
  blanks skipped, single dungeon + section (with engine off-by-one
  verified), multiple sections within one dungeon, dungeon
  transition floor-end corruption (the quirk pinned in a dedicated
  test), entry-slot overflow dropped at port cap, and a full
  vendor-shape end-to-end with spot checks on every f:-line's
  landing position.

**Engine fidelity divergences (documented):** the dungeon-transition
floor-range corruption (quirk #20) is reproduced faithfully — the
first `f:N-M` line of every new dungeon writes its floor info to the
*previous* dungeon's last section before advancing. Vendor data is
structured so this is benign; consumers querying floor ranges still
see plausible matches. Port adds safety caps for overlong names
(>= 64 chars), name-table OOB IDs, and per-section entry-slot
overflow (>20 entries).

**Boot verification:** stderr now shows
`tables: data/snews.txt — 2230 bytes (names=25 sections=10)`. The 10
sections matches the trace: 11 `f:` lines across 6 dungeons, with
the off-by-one shifting the last-section-of-each-dungeon writes onto
the next-dungeon's first section, leaving dungeon 6's section [5][0]
with floor_start = -1 (no successor to write over it).

**Test status:** 81 tests pass (up from 71), no fails, no skips.

## 2026-05-20 — Phase B [5/15]: `data/chara.txt` parser

**Subsystems landed:**
- `src/tables_chara.{c,h}` — pure-C parser for FUN_00475270 block #6
  (L1030..L1146 outer + L76547..L76593 LAB_00477931 continuation).
  Two interleaved CSV sub-blocks share the same 8 records:
  `000:`..`007:` populates base stats (10 fields, 7 ints + 3 floats);
  `100:`..`107:` populates the level-100 endpoints (6 ints, permuted
  AT/DF/MT/MF/HP/SP → hp_lv100/sp_lv100/at_lv100/.../mf_lv100).
  Engine init seeds nine of the ten base fields per record (LV=1,
  HP=50, SP=30, AT=10, DF=13, MT=5, MF=10, move=0.15f, dash=0.20f);
  the port memsets to zero first so crit_rate and all lv100 stats
  start at 0 — a harmless superset.
- `src/tables.c` — replaced the chara.txt stub with a real loader.
  Heuristic for the boot trace: `level_threshold != 1` flags a
  parsed record (default is 1; vendor unlock-levels 1/8/10/15/20/30
  store as 0/7/9/14/19/29, none equal to 1). Boot trace now logs
  `(adventurers=N lv100=M)`.
- `docs/formats/data-text.md` — appended a chara.txt section with
  line-shape table, record layout, field-order permutation
  (file order vs in-memory layout for both sub-blocks), defaults
  table with bit-exact float values, engine quirks, the 10×8
  parse-loop overrun bug, and full vendor-shape table for the 8
  adventurers (Louie through Arma).
- `tests/test_tables_chara.c` — 9 cases: empty (defaults only),
  defaults bit-exact (0x3e19999a / 0x3e4ccccd match `0.15f` / `0.20f`
  byte-for-byte), basic record, lv100 alone, both blocks combined,
  comments skipped, OOR-index 008/009/108/109 guarded (no OOB
  write), lv100 field permutation with distinct sentinels,
  vendor-shape end-to-end with spot checks on Louie/Griff/Arma.

**Engine fidelity divergence (documented):** the engine's parse
loop iterates 10 times per sub-block even though only 8 records are
initialized — a 2-record overrun bug that would write into the
adjacent `g_models[0..1]` globals at `&DAT_073ae258` if chara.txt
contained any `008:` / `009:` / `108:` / `109:` lines. Vendor data
ships only `000:`..`007:` and `100:`..`107:`, so the bug is
dormant. The port caps the inner match loop at `CHARA_COUNT` and
silently drops out-of-range indices.

**Boot verification:** stderr now shows
`tables: data/chara.txt — 1868 bytes (adventurers=8 lv100=8)`,
matching the vendor file's 8 adventurer rows + 8 lv100 endpoint
rows. All other stubs continue to log as before; tutorial loop
still stops correctly at `tuto4.txt`.

**Test status:** 71 tests pass (up from 62), no fails, no skips.

## 2026-05-20 — Phase B [4/15]: `data/model.txt` parser

**Subsystems landed:**
- `src/tables_model.{c,h}` — pure-C parser for FUN_00475270 block #9
  (L1422..L1520). Fixed array of 20 records at `&DAT_073ae258` (stride
  0x2b8 bytes). Per-line dispatch: `no:N` sets current model index
  (atoi), `fname:` copies the `.x` filename, `NN:` (00..19) copies a
  bone/attachment-point name and increments `count`. Engine quirks
  faithfully reproduced: `local_c` defaults to 0 (writes before `no:`
  go to record 0); `used[slot] = 1` and `count++` fire unconditionally
  on every matching `NN:` line (no gate on `!used[slot]`); all 20 slot
  prefixes checked on every line. Safety divergences: fname + point
  names truncated at 31 chars + NUL to prevent field-overflow into
  adjacent record fields; out-of-range `no:N` (N < 0 or N ≥ 20) skips
  subsequent writes rather than computing an out-of-bounds pointer.
- `src/tables.c` — replaced the model.txt stub with a real loader.
  Counts `defined` (records with `count > 0`) and `max_points` (max
  `count` value across all records). Boot trace now logs
  `(models=N max_points=M)`.
- `docs/formats/data-text.md` — appended a model.txt section with
  line-shape table, record layout, engine quirks and safety
  divergences, and vendor-file shape including the out-of-order
  indices (17/18 appear swapped in the file).
- `tests/test_tables_model.c` — 9 cases: empty, basic one record,
  index threading (records 0 and 5), comments/blanks skipped, fname
  before any no:, repeated-slot count increment, overlong fname
  truncation (count field not corrupted), out-of-range no: skipped
  (no OOB write), vendor-shape end-to-end fixture with all 17 models
  and spot-checks on fname, point names, and gap indices 9/16/19.

**Engine fidelity divergence (documented):** the engine's write cap
for both fname and point names is 0x100, but the fname field is only
0x20 bytes before the `count` field — an overlong fname would
silently corrupt adjacent fields. Our port truncates at
`MODEL_DEF_NAME_MAX - 1 = 31` chars. Out-of-range `no:N` indices
are also guarded (engine would compute an out-of-bounds pointer on
`no:25` etc.). Vendor data has fnames ≤ 12 chars and indices 0..18,
so both guards are dormant against real input.

**Boot verification:** stderr now shows
`tables: data/model.txt — 1758 bytes (models=17 max_points=8)`,
matching the vendor file's 17 defined records and 8-point maximum
(kani models at indices 10 and 11). All other stubs continue to log
as before; tutorial loop still stops correctly at `tuto4.txt`.

**Test status:** 62 tests pass (up from 53), no fails, no skips.

## 2026-05-20 — Phase B [3/15]: `data/oder.txt` parser

**Subsystems landed:**
- `src/tables_oder.{c,h}` — pure-C parser for FUN_00475270 block #8
  (dispatch L1378..L1421 + inner CSV loop reached via
  `goto LAB_00477ffe` at L1813..L1931). Two parse phases plus a
  `LV:`-header dispatch: each data row is `<singular>,<plural>,
  <attribute>`, where field 1 writes at column position into the
  record (engine quirk faithfully reproduced with a safe truncation
  guard), field 2 writes sequentially after the first comma, and
  field 3 is hashed against a 16-tag SJIS attribute table at
  `&DAT_005fd7fc`. Record stride 0x4c (76 bytes) matching the engine.
- `src/tables.c` — replaced the oder.txt stub with a real loader.
  Boot trace now logs `(orders=N max_lv=M)`.
- `docs/formats/data-text.md` — appended an oder.txt section with
  line-shape table, record layout, the full 16-tag attribute table
  (SJIS bytes + kanji + romaji + meaning), inner-loop quirks
  (100-char cap, tab skipping, column-position writes), and the
  fallback name-table lookup that we intentionally suppressed
  until `item.txt` lands.
- `tests/test_tables_oder.c` — 9 cases: empty, single record, LV
  threading across data lines, all 16 SJIS tags → expected bits,
  English fallback (mask=0, attr_index=-1), tab skipping inside
  fields, 100-char inner-loop cap, no-trailing-newline EOF,
  vendor-shape end-to-end fixture with mixed SJIS/English rows
  across LV groups 1, 2, and 5.

**Engine fidelity divergence (documented):** the engine's fallback
linear search through `&DAT_0963e5f8` (item-name table, populated
by item.txt) is deferred — populated as `attr_index = -1`. When
item.txt parses we'll add a name-lookup callback hook. The
engine's MessageBoxA on unknown attributes is intentionally
suppressed so the port doesn't pop up "属性不明な登録" on boot.

**Boot verification:** stderr now shows
`tables: data/oder.txt — 1686 bytes (orders=24 max_lv=5)`,
matching the vendor file's 24 records across LV groups 1-5. All
other 16 stubs continue to log as before; tutorial loop still
stops correctly at `tuto4.txt`.

**Test status:** 53 tests pass (up from 44), no fails, no skips.

## 2026-05-20 — Phase B [2/15]: `data/config.idx` parser

**Subsystems landed:**
- `src/tables_config.{c,h}` — pure-C parser for FUN_00475270 block #2.
  Five live keys (`kanjioff`, `edgewi`, `effectmode`, `edgedel`, `font`)
  + one dead key (`makefont` — the engine matches 8 bytes against the
  bare word but assigns to nothing; we mirror the dead check). The
  `font:` value is copied as raw bytes into a 256-byte fixed buffer
  with safe truncation on overlong input.
- `src/tables.c` — replaced the config.idx stub with a real loader.
  Path-mismatch quirk still sidestepped via the read-side spelling
  (`"data/config.idx"`) for both `storage_get_size` and `storage_read`.
- `docs/formats/data-text.md` — appended a config.idx section with
  full key table, dead-makefont quirk, and the line-terminator
  handling difference from buysell.txt.
- `tests/test_tables_config.c` — 7 cases: empty input, all five
  live keys parsed together, `makefont:` no-op, SJIS font name
  (`ＭＳ Ｐゴシック`), font over-length truncation at the 256-byte
  cap, comment-only file (everything `/`-prefixed → all defaults),
  vendor-shape end-to-end (only `edgewi=2 edgedel=6` active).

**Boot verification:** stderr now shows
`tables: data/config.idx — 950 bytes (kanjioff=0 edgewi=2 edgedel=6 effectmode=0 font=(default))`,
matching the shipping vendor file's active key set exactly.

**Test status:** 44 tests pass (up from 37), no fails, no skips.

## 2026-05-20 — Phase B [1/15]: `data/buysell.txt` parser

**Subsystems landed:**
- `src/tables_buysell.{c,h}` — pure-C parser for FUN_00475270 block #7.
  Mirrors the engine's "match every prefix on every non-comment line"
  structure with five key forms: `ok:` (debug flag), `客番号:` / `種類:`
  (SJIS scalars), and `msg%02d:` / `rmsg%02d:` (two 20-int arrays).
  Engine-global instance `g_buysell`; tests use the out-parameter form.
- `src/tables.c` — replaced the buysell stub with a real loader that
  storage_reads the file, calls `tables_parse_buysell`, and logs the
  three scalars to the boot trace.
- `docs/formats/data-text.md` — new format-spec doc for the
  `data/*.txt` + `idx/*.idx` group. Documents shared conventions
  (Shift-JIS, CRLF, leading-`/` comments, two format families) plus a
  full section for buysell.txt (key table with byte-level SJIS
  identification, engine-side global addresses, the
  rmsg-before-msg in-memory layout quirk, vendor file sample).
- `tests/test_tables_buysell.c` — 8 cases covering empty input,
  comment-only files, the `ok:` toggle, the two SJIS scalar keys
  (using the exact byte sequences from the engine's `.data`),
  msg/rmsg arrays at boundary indices 0 and 19, EOF-without-newline,
  embedded-`\0` early-termination, and a vendor-shape end-to-end
  fixture that reproduces the actual file's CRLF + SJIS + comment
  layout with non-zero values.

**Boot verification:** stderr now shows
`tables: data/buysell.txt — 504 bytes (debug=0 kyaku=14 kind=2)`,
matching the vendor file's expected values (debug commented, customer
14, kind "about"=2). All other 16 stubs continue to log size lines as
before; tutorial loop still stops correctly at `tuto4.txt`.

**Test status:** 37 tests pass (up from 29), no fails, no skips.

## 2026-05-20 — FUN_00475270 ("init indexfile ok") skeleton + Phase A discovery

**Subsystems landed:**
- `docs/findings/tables-loader.md` — discovery doc for the gameplay
  tables loader: caller context (it's the boot trace step right after
  `init render ok`), full file list with sizes and per-block C-line
  ranges, helper identities (storage_get_size / storage_read / atoi /
  atof / free), the two format families observed (`/key:value` for
  `config.idx`; CSV-with-comments for the `data/*.txt` files), and
  the proposed one-commit-per-file Phase B plan.
- `src/tables.{c,h}` — skeleton dispatcher `tables_load_all()` calling
  fourteen stub loaders (one per file) plus a tutorial-loop stub. Each
  stub exercises `storage_get_size` + `storage_read` end-to-end and
  logs the byte count to stderr; the real parsers will replace the
  printf in Phase B without touching the dispatcher.
- `src/main.c` — wired `tables_load_all()` into the boot chain at the
  TODO marker that was already pinned for `FUN_00475270`. Position
  matches the engine's `init render ok → [HERE] → init fontsys ok`
  ordering.

**Engine quirks documented this turn:**
1. `FUN_00475270` calls `storage_get_size` and `storage_read` with
   different `.data` addresses in every block — usually two interned
   copies of the same path string. For `config.idx` the developer
   accidentally typed two **different** spellings (get_size with
   `"config.idx"`, read with `"data/config.idx"`), so the original
   silently `malloc(0+10) = 10` and overruns by 940 bytes on every
   boot. Our stub uses the read-side spelling to avoid the bug.
2. Tutorial format string is `"data/tuto%d.txt"` (no underscore).

**Boot verification:** stderr trace from `openrecet.exe
--max-duration-ms 2000` shows all 17 storage reads succeed (14 fixed
+ 3 tutorials), and the loop correctly stops at `tuto4.txt`. Several
files come back larger than the lnkdatas size because they have a
bmpdata-overlay patched version (e.g. `enemy.txt`: 2801 → 3589).

**Test status:** 29 tests pass (no new tests yet — Phase B will add
per-file fixture tests as each parser lands). Boot smoke clean.

## 2026-05-20 — FUN_004341d4 bookkeeping (file-size helper)

Pinned candidate #2 closed as already-done. `FUN_004341d4` is the
trivial `fseek(0,SEEK_END); ftell; fseek(0,SEEK_SET)` file-size
helper, and it was already faithfully translated as
`storage_file_size` in `src/storage.c:139` during the
`storage_init`/`FUN_004341fe` port (with an in-file comment naming
the original). All four in-engine call sites we've ported (the ones
inside `storage_init` itself) route through it.

The other three inlined `fseek/ftell/rewind` idioms in `src/tga.c`,
`src/sprite.c`, and `src/lnkdatas_hash.c` were written by us, not
ports — they intentionally check the fseek/ftell return values
(the original doesn't). Left untouched so the defensive coverage
stays in place; promoting a 5-line static into a shared util module
would have been premature abstraction. Dropped from the
session-starter pin list.

## 2026-05-20 — lnkdatas content read + LZSS

**Subsystems landed:**
- `src/lnk_lzss.{c,h}` — port of FUN_004349e5 (the lnkdatas LZSS decoder).
  Pure C, no Win32 surface.  ~50 LOC. Stream is self-delimiting via the
  back==0 sentinel, so no input size is required.
- `src/storage.c` — extended `storage_get_size` and `storage_read` to fall
  back to the lnkdatas index when the asset isn't in the bmpdata overlay.
  Adds a 1-deep `bin/data%03d.bin` FILE* cache and a 10 MiB chunk-spanning
  reader (handles entries that straddle a `bin/data*.bin` boundary).
  Skips the original engine's 3× `Sleep(500ms)` retry loop around the
  fopen — that was robustness against transient I/O on 2007 spinning
  drives, not load-bearing for a modern Steam install.
- `tests/test_lnk_lzss.c` — 7 synthetic unit tests covering single
  literals, short / extended back-references, self-overlap RLE, the
  end-of-stream sentinel mid-control-byte, high-bit back-distances, and
  mixed flags within one control byte. Plus a vendor round-trip that
  iterates every entry in `vendor/original/lnkdatas.bin`, reads its
  slice (across chunk boundaries as needed), decompresses, and verifies
  the result length matches the declared `dsize` + that a one-byte
  output canary is intact.

**Two case-sensitivity quirks worth knowing:** the bmpdata branch of
`FUN_00434585` / `FUN_004346bf` does case-insensitive name matching
(A..Z folded to a..z) over 88 bytes; the **lnkdatas branch does a
straight byte compare** over 128 bytes — no fold. Our port mirrors
both. Callers relying on case-insensitive lookup must hit through the
bmpdata path.

**Pixel-exact validation:** rebuilt the standalone harness
`/tmp/storage_extract.exe` (built from `src/storage.c` with
`-DSTORAGE_TEST_EXTRACT`) and confirmed byte-identical output vs the
Python reference (`tools/extract/data-bin.py`) on 5 entries including
4 chunk-straddling ones (`xfile/koku_last/mahoujin.tga`,
`xfile/wall/kabe_check.bmp`, `bmp/chr/chr31.bmp`,
`bmp/worldmap_yugata.bmp`). Hashes match (SHA-256).

**Test status:** 29 tests pass (up from 21), no fails, no skips.
Sanitizer-clean. ASan caught two bugs while writing tests — both in
the *test fixtures*, not in the decoder: a mis-computed control byte
in `test_lnk_lzss_self_overlap` (0x28 should have been 0x30) and a
use-after-free on the canary value in the vendor round-trip. Good
ASan-pays-for-itself moment.

**Engine smoke:** boot scenario `tools/smoke-test.py` still exits 0
in ~4s on the rebuilt exe, debug-magenta clear color unchanged.

**Next pin (per session-start):** `FUN_00475270` is the big one —
3965 decompiled lines of `data/*.txt` parsing (item / kyaku / chara
/ enemy gameplay tables) plus `idx/stage.idx` and `idx/config.idx`.
Will likely need splitting across multiple commits.

## 2026-05-20 — Sanitizer-instrumented unit tests

**Subsystems landed:**
- `tests/Makefile` — Linux-native test harness. Host gcc +
  `-fsanitize=address,undefined -fno-sanitize-recover=all`. Run with
  `make -C tests run`.
- `tests/t.h` — dependency-free assertion macros (T_ASSERT, T_FAIL,
  T_SKIP) + UBSan-safe byte writers.
- `tests/test_main.c` — runs registered tests, supports name-substring
  filter via `argv[1]`, exits non-zero on any failure (skips don't
  count).
- `tests/test_{bmp,tga,bmp_lzw,lnkdatas_hash}.c` — 21 tests covering
  every audited code path in the four portable decoders.

**Why now:** the Win32 sprite loader just landed, and every decoder
ingests user-controlled bytes (BMP/TGA from `bmpdata.bin`, LZW slices,
CRC over the whole lnkdatas blob). Memory bugs in these can pass
pixel-equality checks while still being broken. Valgrind/ASan can't
run Win32 PE binaries, so the natural split is "Linux test target for
the portable .c files". The Win32 layer stays exercised by smoke
tests.

**Doc fix discovered during test writing:** `src/lnkdatas_hash.{c,h}`
called the engine's hash "CRC-16/CCITT-FALSE". It's *shaped* like
CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, final invert) but
the feedback step uses **subtraction** instead of XOR. The standard
CCITT-FALSE check value for "123456789" is 0x29B1; ours is 0xF5B7
because of borrow propagation. Cross-checked against
`/opt/src/recettear-repacker/crc.py`.

**Sanity check the harness catches real bugs:** temporarily injected
a 1-byte OOB read past the BMP pixel buffer and reran. ASan
pinpointed the exact `bmp.c:77` line with the heap region details.
Reverted; all 21 tests pass green.

## 2026-05-20 — `FUN_0047193c` ported — engine-style sprite loader

**Subsystems landed:**
- `src/bmp.{c,h}` — 24-/32-bit BI_RGB DIB decoder with color-key
  application (engine passes `0xFF00FF00` to D3DX → we test exact-match
  pure-green and zero the alpha). Top-down + bottom-up.
- `src/tga.c` extended — now also handles Type 10 RLE, plus an
  `tga_load_mem(buf,size,*img)` variant so the loader can decode
  storage-fetched bytes without round-tripping through disk.
- `src/sprite.c` — new `sprite_load(dev, name, w, h, *out)` entry
  point. Mirrors `FUN_0047193c`: tries `fopen(name,"rb")` first, falls
  back to `storage_read(name)`, sniffs `'BM'` → BMP-with-key vs TGA,
  decodes, uploads via the existing `sprite_create`.
- `src/main.c` — replaced `--show-tga <path>` (direct-file shim) with
  `--show-sprite <name>` that routes through the new `sprite_load`.

**Identification fix carried into `docs/findings/texture-loader.md`:**
the earlier write-up had the disk and storage calls swapped (claimed
the engine tried storage first with disk as fallback). Re-reading
`FUN_005038b0` as a thin `fopen` wrapper (forwards to `FUN_00503890`
with a `0x40` buffer-size hint) flipped it — **disk is tried first,
storage second**. The user-facing implication: external/mod overrides
on disk take precedence over the packed asset, consistent with how
`recettear-repacker` works.

**Validation (pixel-perfect, max diff 0/255 in all four cases):**
- Storage path: `--show-sprite bmp/ivent/ed_kasi11.tga` (resolves via
  `storage_read` from `bmpdata.bin`) renders byte-identical to a
  reference Python decode composited over the debug-magenta clear
  color, across all 512×32 pixels.
- Synthetic disk fixtures (built in-place, then cleaned up): a 64×64
  Type-2 TGA, a 64×64 Type-10 RLE TGA, and a 64×64 24-bit BMP with one
  half pure-green keyed and the other half opaque blue. All three
  round-trip 0 mismatches against the expected composite.

**Not yet engine-accurate:** D3DX-style resampling to `(expected_w,
expected_h)` is still skipped. Every audited asset ships at native
resolution, so this matters mainly for forward-compat / mod paths
that intentionally scale.

**Lifecycle fix (orphan-window cleanup, follow-up commit):** ad-hoc
`timeout 3 openrecet.exe …` runs kept leaving the host's Windows
side with orphan windows because `g_paused` blocks the main loop in
`WaitMessage` when the window loses focus — any deadline check
inside `tick_and_present` is never reached. Added `--max-duration-ms
<ms>` (also taken up by `tools/smoke-test.py`) that registers
`SetTimer` → `WM_TIMER` → `DestroyWindow`, which fires regardless of
pause state. Smoke harness now reaches `exit=0` gracefully instead
of falling through to SIGTERM/taskkill.

Next-milestone candidates: lnkdatas content-read path (so `storage_read`
also services `bin/data_NNN.bin` + the LZSS decompressor at
`FUN_004349e5`); `FUN_004341d4` standalone port (mostly mechanical);
diving into `FUN_00475270` (gameplay-text-table parser, 3965 lines —
needs splitting across commits).

## 2026-05-20 — `bmpdata.bin` LZW decoder + storage_read overlay path

**Subsystems landed:**
- `src/bmp_lzw.{c,h}` — 12-bit MSB-first LZW decompressor. Translation
  of `FUN_00434b32` (main loop), `FUN_00434c2c` (bit reader), and
  `FUN_00434ca9` (dict-chain walker). Dictionary frozen at 3839 entries,
  matches `recettear-repacker/bmp_unpack.py` exactly.
- `src/storage.{c,h}` extended — now also opens `bmpdata.bin`, slurps
  it into memory, validates the hash sentinel `0x21dc`, and exposes
  `storage_get_size(name)` + `storage_read(name, dst)`. Mirrors
  `FUN_00434585` (size lookup) and `FUN_004346bf` (read into buffer) for
  the bmpdata branch.

**Identification fix:** `FUN_00475270` (originally pinned as "likely
bmpdata.bin LZW loader" in PROGRESS) turned out to be the global
gameplay-text-table loader — a 3965-line parser for `data/item.txt`,
`data/chara.txt`, the `idx/stage.idx` chain, etc. The real LZW lives in
the much smaller `FUN_00434b32` + helpers, called lazily from the
storage read path. Plan annotation corrected.

**Engine deviation, by design:** the engine's `FUN_00434b32` doesn't
handle code 256 (LZW reset/EOS) — it walks past the dict base on the
sentinel and emits a few garbage bytes past the caller's `dsize`
buffer. Benign in the shipping game (callers tolerate the overrun) but
we'd rather not write past the asked-for size, so our decoder honors
256 explicitly. End result: byte-for-byte identical with
`bmp_unpack.py` output, not byte-for-byte identical with the engine's
overrun-prone output.

**Validation:** all 22 entries in the shipping Steam `bmpdata.bin`
round-trip through `storage_init → storage_read → stdout` to
byte-equal output vs the Python reference (see `/tmp/storage_diff.py`).
Boot smoke (`tools/smoke-test.py`) still green — exit signal, 3 frames
captured, no early-exit error from the now-stricter `storage_init`
(which is required to find `bmpdata.bin`).

**New format spec:** `docs/formats/bmpdata.md` — 84-byte names, 3 ×
int32 (dsize/offset/csize) per entry, 96-byte stride, 12-bit LZW
payload, hash sentinel `0x21dc`.

Next-milestone candidates (unchanged): `FUN_0047193c` (proper sprite
loader using `storage_*` with BMP + green-key + RLE-TGA — now possible
because `bmpdata` lookups work), `FUN_004341d4` standalone port, or
diving into the `FUN_00475270` gameplay-data parser.

## 2026-05-19 — Render-layer init ported (`FUN_00454e69` + `FUN_004038e4`)

**Subsystem landed:** `src/layers.{c,h}`. The "init render ok" hand-off
isn't device creation (that's step 11) — it's the engine fanning
`GetDeviceCaps` + back-buffer-desc + the live device pointer out into
its 24 per-layer state objects (each 0x2f0 bytes). Two arrays:
`g_layers_b[20]` (loop, `DAT_073da2f0` stride 0x2f0) and `g_layers_a[4]`
(unrolled in asm at `DAT_073cba20`/`+0x2f0`×3). See
`docs/findings/winmain-and-bootstrap.md` §"Render-layer init" for the
RE writeup + offset table.

**Layout corrections from the earlier guess:**
- The previous notes claimed the loop "zeros" the structs via
  `FUN_004038e4`. It doesn't — it actively writes `device` (`+0x108`),
  the back-buffer `D3DSURFACE_DESC` (`+0x10c`, 32 bytes), and a copy of
  `D3DCAPS8` (`+0x12c`, 212 bytes), then nulls `+0x200`.
- The 20-element loop is only *one* of two arrays; the 4 unrolled
  trailing calls operate on a *separate* 4-element array — easy to miss
  from the decompiler output because Ghidra strips the ECX setup before
  each thiscall.

**Skeleton wiring (`main.c`):**
- Removed the placeholder `IDirect3D8_GetDeviceCaps` from `init_render`
  — the real owner is now `layers_init`.
- `layers_init(g_d3d, g_dev)` slotted in after `input_init`, matching
  the original's `…dinput ok → init render ok` ordering (the previous
  comment had this misplaced).
- Bootstrap-order comments now mirror the actual call sequence.

**Why the struct is field-by-field (not a byte blob):** mingw's `d3d8.h`
ships `D3DCAPS8 = 212`/`D3DSURFACE_DESC = 32` — exact match to the
original's `rep movsl 0x35` and `0x12c−0x10c = 0x20`. Five
`_Static_assert`s on the known offsets + total size catch any future
header drift at build time.

**Verified:** `tools/smoke-test.py --target openrecet --scenario boot
--duration 4 --capture` — debug magenta `(160, 32, 96)` reads flat
across all 4 captured frames; no crash on init or shutdown.

Next-milestone candidates (unchanged): `FUN_00475270` ("init indexfile
ok" — likely `bmpdata.bin` LZW loader, cross-ref
`/opt/src/recettear-repacker/bmp_unpack.py`), `FUN_004341d4` (file-size
helper, quick mechanical port), or porting `FUN_0047193c` properly to
read assets via `storage_*` with BMP+green-key + RLE-TGA support.

## 2026-05-19 — Project bootstrap

**What landed**
- Decisions (see [`PLAN.md`](PLAN.md) §3): C + mingw-w64 32-bit, DirectX
  direct, MIT, Win32-first drop-in.
- `flake.nix` with full RE toolchain (ghidra 12, radare2, rizin, cutter,
  retdec, imhex, wine staging, frida-tools, mingw32 i686 cross compiler,
  python env with construct/scikit-image/pillow/opencv, xvfb-run, scrot,
  ffmpeg, imagemagick, pandoc).
- Directory structure: `src/`, `tests/`, `tools/`, `docs/`, `vendor/` (gi),
  `ghidra/` (gi).
- Plan, README, MIT license, `.gitignore` that aggressively protects any
  derived game data, `.editorconfig`.

**What we know about the original**
- `recettear.exe`: 32-bit PE, **SteamStub-packed** (VLV signature @0x80),
  5.6 MB on disk.
- `custom.exe`: ~462 KB config tool reading `recet.ini`.
- Assets: `bin/data###.bin` (custom archives, format TBD), `xfile/*.x` and
  `xfile2/*.x` (DirectX retained-mode `.x` text models — open spec),
  `bgm/*.wav`, `ef/effect*.dat`, `bmpdata.bin`, `lnkdatas.bin`,
  `recet_op.wmv`.
- `recet.ini` exposes: `winmode`, `fps`, `dispfps`, `usefog`, `usemipmap`,
  `usetree`, `windowpos`, `uselighttex`, `nolight`, `easydisp`, `bgnodisp`,
  `texlevel`, `toorioff`, `s_easydisp`, `sfnouse`, `pfnouse`,
  `fontmode1`/`fontmode2`, `screen`, `texmode`, `mapmode`, `demomode`, plus
  `pad##` / `skill##` key bindings and `[config] se`/`mu` audio levels.
  These are the engine's main feature toggles — each one is a hint about
  a code path we'll meet.

**Note on wine vs WSLInterop** — WSL has `WSLInterop` registered as a
binfmt handler, so `.exe` invocations run natively on Windows by default.
We use that for Steamless and for casual play. The automated test harness
still uses wine + Xvfb so that (a) the runtime is pinnable via the flake
and (b) original-vs-ours diffs share the same backend and don't surface
wine-vs-Windows differences as phantom bugs. See `PLAN.md` §6 for details.

**Tooling landed**
- `tools/setup.sh` — symlinks game, runs Steamless via WSLInterop, prints sha256s.
- `tools/ghidra-headless.sh` + `tools/ghidra-scripts/ExportDecompiledC.py` —
  batch decompile every function to `docs/decompiled/` (gitignored).
- `tools/smoke-test.py` — Xvfb+wine runner with frame capture and SSIM diff
  against a golden run.
- `tools/contact-sheet.py` — single-set or side-by-side downscaled grids, with
  optional `--zoom` full-res crop strip.
- `tools/extract/xfile.py` — DirectX `.x` text-format summarizer + tree scanner.

**First extractor result (validates pipeline)** — running
`xfile.py xfile/city/dun_city00.x` on the user's Steam install reports:

| template               | count |
|------------------------|------:|
| Material               |    58 |
| TextureFilename        |    25 |
| Frame                  |    13 |
| FrameTransformMatrix   |    13 |
| Mesh                   |    12 |
| MeshMaterialList       |    12 |
| MeshNormals            |    12 |
| MeshTextureCoords      |    12 |
| MeshVertexColors       |     1 |

All **standard DirectX 9 retained-mode** templates. No custom extensions →
`xfile/` and `xfile2/` can be parsed with stock `D3DXLoadMeshFromX*` or any
open-spec parser. (Phase 2 will scan the full tree for a definitive answer.)

**Flake quirk** — `retdec` currently fails to build in nixpkgs (capstone
sub-build error). Disabled it. Ghidra + radare2/rizin cover decompilation
cross-checks. Re-enable if upstream fixes.

**Next** (Phase 1 entry)
- User runs `./tools/setup.sh` to unpack the exe.
- Then `./tools/ghidra-headless.sh` to produce `docs/decompiled/`.
- First subsystem to map: WinMain + main loop. Find `D3D*Create*` calls in
  the decompiled output → confirms DirectX version (likely 8 or 9).

---

## 2026-05-19 — Setup ran, first findings from unpacked binary

- ✅ `tools/setup.sh` ran successfully on the user's machine — Steamless via
  WSLInterop produced `vendor/unpacked/recettear.unpacked.exe` (5.0 MB,
  down from 5.6 MB packed; 7 PE sections → 6 sections; VLV signature gone).
- Fixed `tools/ghidra-headless.sh`: nixpkgs ghidra names its binaries
  `ghidra-<tool>` (e.g., `ghidra-analyzeHeadless`), not bare `analyzeHeadless`.

**New findings — recorded in [`findings/imports-and-layout.md`](findings/imports-and-layout.md):**

- **DirectX version is 8** (d3d8.dll / d3d8d.dll / D3DERR_*). Fixed-function
  pipeline only — no shaders, no HLSL compiler required.
- **DirectX is loaded dynamically** via `LoadLibraryA` + `GetProcAddress` —
  static imports are only `KERNEL32`, `USER32`, `SHELL32`, `WINMM`,
  `ole32`, `ADVAPI32`. Six DLLs total. Very tight.
- **`DirectXFileCreate` is used** for `.x` model parsing (open DX File API).
- **No `dsound.dll` / `dinput.dll` static imports.** Audio likely via
  `WINMM` (`mciSendString` / `waveOut*`) or dynamically-loaded DSOUND;
  input likely raw `USER32` `WM_KEYDOWN` / `GetKeyState`. To be confirmed.
- **Asset layout discovered from strings:** the binary references
  `bmp/item/item%02d.bmp`, `bmp/item_win.tga`, `data/item.txt`,
  `bin/se/.../*.bin`, etc. None of these paths exist on disk — confirming
  that `bin/data###.bin` archives contain the `bmp/` (TGA/BMP textures)
  and `data/` (plain text gameplay tables) trees. Cracking this format
  unlocks all 2D art and all gameplay data.
- **All UI/dialogue strings are inline in `.rdata`** — no string table,
  no `.po` files. i18n story is "rebuild the binary".

**Next**
- ~~Run `./tools/ghidra-headless.sh`~~ Done — 2620 functions decompiled.
- ~~Locate the function that opens `bin/data000.bin` → archive format~~
  Pre-empted: spec was already cracked by UnrealPowerz/recettear-repacker.
- Locate `WinMain` and the `LoadLibraryA("d3d8.dll")` site → document
  the window+device init sequence for the skeleton in phase 3.

---

## 2026-05-19 — Ghidra working, cross-references absorbed

**Ghidra:**
- Fixed the post-script: nixpkgs Ghidra 12 isn't built with PyGhidra, so
  `.py` scripts fail. Rewrote `ExportDecompiledC` in **Java** — works
  in plain headless mode without flags.
- Also fixed a latent bug: the script had `-deleteProject` set on the
  first import, which would have wiped analysis state. Removed.
- Result: **2620 functions** decompiled into `docs/decompiled/all.c`
  (6.3 MB), `by-address/*.c`, `by-name/*.c`, `functions.csv`.

**Cross-reference projects** (cloned to `/opt/src/`):
- **UnrealPowerz/recettear-repacker** — full spec for `bin/data*.bin`
  archives. Format: 10 MiB chunks of LZSS-compressed blobs indexed by
  big-endian `lnkdatas.bin`. Custom LZSS variant with 12-bit back-distance
  + MSB-first ctrl byte. `bmpdata.bin` is a separate LZW-compressed
  update overlay.
- **ribeena/RecettearXTools** — `.x` ↔ USD Blender 4.1 converter; useful
  for double-checking our `.x` parser.
- **just-harry/FancyScreenPatchForRecettear** — runtime widescreen
  patcher; useful as a map of engine offsets we'll want to understand.

**New format spec:** [`formats/data-bin.md`](formats/data-bin.md).

**Our own extractor:** `tools/extract/data-bin.py` — clean Python
reimplementation matching the spec. Validated against upstream:
**byte-identical** output on the current Steam build (1188 files extracted).
Run `./tools/extract/data-bin.py vendor/original --validate-against
/opt/src/recettear-repacker` to re-verify.

**Newly confirmed about the engine:**
- **DirectInput 8** (`dinput8.dll`) is also dynamically loaded — found
  `DirectInput8Create` symbol at `0x4a1cc0`.
- **C++ compiled with MSVC** — `vector_constructor_iterator` /
  `vector_deleting_destructor` indicate array new/delete scaffolding.
- **MFC is statically linked** — `RFX_Text_Bulk` (MFC ODBC field exchange)
  present. Probably leakage from `custom.exe` sharing libs; possibly
  engine uses some MFC for save serialization (TBD).
- PE entry is at `0x5046c7` — MSVC `__tmainCRTStartup`. `WinMain`
  symbolic name not yet auto-resolved.

**Next investigation targets**
1. Trace `__tmainCRTStartup` → `WinMain`. Rename in the Ghidra project.
2. From `WinMain`, find the `LoadLibraryA("d3d8.dll")` call → document the
   DX8 device-creation sequence. (Skeleton for phase 3.)
3. Read `recettear-repacker/crc.py` — the engine probably uses the same
   CRC as a path hash for `bmpdata.bin` lookups. Worth porting.
4. Optional: read `FancyScreenPatchForRecettear` patch sites to find
   resolution-clamping code (a likely candidate for an early test
   subsystem since the patches are small and well-isolated).

---

## 2026-05-19 — Engine bootstrap mapped end-to-end

Full writeup in [`findings/winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md).
Highlights:

- **WinMain at `0x47bfb3`**. Identified by the standard MSVC
  `__tmainCRTStartup(hInst=GetModuleHandleA(NULL), 0, lpCmdLine, nCmdShow)`
  call signature in the PE entry.
- **Engine internal name is "Azumanga"** — that's EGS's name for their
  custom engine (also powers Chantelise). Window class is literally
  `"Azumanga Main Window"`.
- **Window title is `"RECETTEAR Ver 1.108"`** — exact version string for
  drop-in compatibility.
- **Debug logger `FUN_0047aa31` is a 1-byte `return;` stub** — all
  logging compiled out in the release build. The `s_init_*` string
  constants remain in `.rdata` as breadcrumbs, which **gave us the full
  subsystem init order for free**:
  `start → strage → print → dinput → render → indexfile → fontsys →
  daoudio → fontsystem → systemtex → savefile → titletex → main loop`.
  (Note Japanese-English typos preserved: `strage`, `daoudio`.)
- **Main loop function: `FUN_0047be92`** — the game tick.
- **`Direct3DCreate8(0xDC)` at line 77975 of `all.c`** — `0xDC = 220 =
  D3D_SDK_VERSION`. Global `IDirect3D8 *` is `DAT_073dfcb8`.
- **DirectInput 8 init: `FUN_0047af52`** — keyboard + EnumDevices for
  joysticks, with axis range ±5000 and 100-unit deadzone.
- **Storage init: `FUN_004341fe`** — tries `lnkdata.bin` (JP name) first,
  falls back to `lnkdatas.bin` (EN name). Validates the index via
  `FUN_00474f14` which must return `-0x7456` (`0xFFFF8BAA`); this is the
  engine's integrity hash, almost certainly matches
  `recettear-repacker/crc.py`.
- **WndProc `FUN_0047b2e7`** handles `WM_CREATE`, `WM_DESTROY`,
  `WM_ACTIVATE` (pause + DI un/acquire), `WM_CLOSE` (confirm dialog in
  windowed), `WM_KEYDOWN` (ESC only — rest of input via DInput).

**Process docs added:** [`AGENT-WORKFLOW.md`](AGENT-WORKFLOW.md) — codifies
the Opus-orchestrator / Sonnet-subagent split + briefing template + stop
conditions. Read at the start of every new session.

**Stop point:** engine bootstrap is mapped. Logical next milestone:
either (a) write the phase-3 skeleton drop-in `src/main.c` matching the
init order, or (b) start translating the high-value individual functions
(`FUN_0047be92` game tick, `FUN_00474f14` integrity hash, the d3d8 wrapper
that calls `Direct3DCreate8`). User to choose.

---

## 2026-05-19 — Phase 3 skeleton drop-in runs

**`src/main.c` + `src/Makefile` written and building.** The skeleton
mirrors the bootstrap chain from
[`findings/winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md):
high-resolution timer setup → window class register (`"Azumanga Main
Window"`) → CreateWindowExA (`"RECETTEAR Ver 1.108"`) → LoadLibraryA
(`d3d8.dll` → `d3d8d.dll` fallback) → `Direct3DCreate8(D3D_SDK_VERSION)`
→ `IDirect3D8::CreateDevice` → message pump with `PeekMessage`/
`WaitMessage`/`tick_and_present`. Each subsystem in the original's init
order is a `TODO` comment naming the `FUN_XXX` we still need to
translate.

**Builds at 77 KB** via `i686-w64-mingw32-gcc` from inside `nix develop`.
Static libs: `-ld3d8 -ldinput8 -ldsound -lwinmm -lgdi32 -luser32
-lkernel32 -lole32 -ladvapi32 -lshell32`.

Tick path currently does `Clear → BeginScene → EndScene → Present` with
a distinctive **debug magenta** clear color (`160, 32, 96`) so a working
boot is visually obvious vs a black-screen failure.

**Test harness pivoted to WSLInterop** (see updated `PLAN.md` §6):

- Modern nixpkgs `wineWow64Packages.stagingFull` skips the 32-bit
  `syswow64/` layer → 32-bit binaries fail to load `kernel32.dll`.
- `wineWowPackages.stagingFull` (classic dual-arch) builds from source on
  every machine, slow.
- WSL2 + WSLInterop is rock solid and runs the exe natively on Windows.
  Trade-off: tests pop a window on the desktop. We'll work around this
  with self-emitting back-buffer captures inside the exe
  (`--capture-to <dir>`, not yet wired).
- Wine dropped from the flake entirely.

`tools/smoke-test.py` rewritten — no Xvfb, no wine, no scrot. Launches the
exe via WSLInterop, captures exit code + duration + stdout/stderr + sha256.
First run: `openrecet.exe` ran cleanly for 3 seconds, was killed by
timeout (exit code -15, SIGTERM), `taskkill /F /IM openrecet.exe` confirmed
clean shutdown.

**Next**
1. Wire `--capture-to <dir>` into `src/main.c` — save back-buffer as 32-bit
   BMP every N frames, into the harness's `runs/<scenario>/<id>/frames/`.
2. Translate `FUN_00474f14` (the lnkdatas integrity hash) to validate our
   `tools/extract/data-bin.py` matches the engine's expected sentinel.
3. Start filling in subsystem stubs — first target: `FUN_004341fe`
   (storage init / lnkdatas loader) so the skeleton actually opens the
   game's index file. Good Sonnet-subagent task.

---

## 2026-05-19 — All three subagent tasks landed; capture pipeline works end-to-end

**Subagent 2: CRC hash port** — `src/lnkdatas_hash.{c,h}` +
`tools/extract/lnkdatas_hash.py`. Algorithm identified as
**CRC-16/CCITT-FALSE** (poly `0x1021`, init `0xFFFF`, MSB-first, final
`~crc`). Validates byte-identical against `recettear-repacker/crc.py`;
on the real `lnkdatas.bin` (sha256 `6c5b93cf…`) returns `0x8BAA`
(= `-0x7456`, the engine's "valid" sentinel).

**Subagent 3: Storage init port** — `src/storage.{c,h}`. Caught a
critical detail the first writeup missed: **`FUN_004341fe` has two
distinct format paths**. The JP build's `lnkdata.bin` (singular) has a
5-byte header skipped + a `byte' = 0x01 - byte` payload transform +
sentinel `0xC5E1`. The EN build's `lnkdatas.bin` (plural) is raw +
sentinel `0x8BAA`. Both implemented. Findings doc
[`winmain-and-bootstrap.md` §"Storage init"](findings/winmain-and-bootstrap.md)
corrected.

**Subagent 1: Frame capture** — `src/main.c` gained `--capture-to <dir>`
+ `--capture-every N` CLI flags. Renders BMPs at intervals via
`GetBackBuffer → LockRect → fwrite`. Initially silent-failed; root cause
identified as needing `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER` in the present
parameters AND capturing **before** `Present()` (we use
`D3DSWAPEFFECT_DISCARD` which makes post-Present back-buffer undefined).
Both fixed. Capture now runs at ~5000 FPS in the empty-tick state
(660 frames captured in 4 seconds of un-vsync'd rendering — capture
overhead is negligible).

**Subagent integration issues** worth noting for future use of the
AGENT-WORKFLOW pattern:
- The first attempt at subagent 1 (frame capture) failed because
  `isolation: worktree` requires an existing git commit — we have none
  yet. Re-ran without isolation; safe because the other two created only
  new files. **Action:** make an initial commit before relying on
  worktree isolation.
- Subagents 2 and 3 raced on the `lnkdatas_hash` signature: subagent 2
  used `(buf, size) → int16_t`, subagent 3 assumed `(size, buf) →
  uint16_t` and inlined a fallback impl in `storage.c`. Caused a
  duplicate-symbol link error. **Action:** when subagents share an
  interface, brief them with the exact signature, not "infer it".
- Subagent 3 caught the JP/EN dual-format detail in `FUN_004341fe` that
  the orchestrator (me, Opus) had missed in the initial writeup. Good
  outcome — second-pass careful reading by a fresh agent surfaced
  something a quick first read glossed over.

**End-to-end visual confirmation:**
- User reported seeing the debug-magenta window during a manual run.
- Captured BMP frame 60 center pixel = exactly `RGB(160, 32, 96)`.
- 4-tile contact sheet via `tools/contact-sheet.py` shows all magenta.
- Pipeline: mingw32 build → `openrecet.exe` (91 KB) → WSLInterop →
  Windows 32-bit process → `storage_init()` loads 1188 lnkdatas entries
  via the EN path → DX8 device with `LOCKABLE_BACKBUFFER` → tick loop
  → BMP captures → contact sheet → visual diff ready.

**Tooling fix:** `tools/contact-sheet.py` now sets
`ImageFile.LOAD_TRUNCATED_IMAGES = True` to bypass PIL's strictness on
32-bit BI_RGB BMPs with an X-padding byte. The BMPs are structurally
valid (file size, headers, pixel layout all correct — verified manually);
PIL treats the X byte as alpha and trips a bounds check. Not actually
truncated.

**Stop point.** Skeleton boots, has working frame capture, has real
lnkdatas integrity-validated load. Logical next milestones (pick one,
or parallelize via subagents per AGENT-WORKFLOW.md):

1. **Initial git commit** so future subagents can use `isolation: worktree`.
2. **Translate `FUN_004341d4`** — the file-size helper used by storage init
   (currently we reimplemented it inline; matching the original is cleaner).
3. **Translate `FUN_0047af52`** — the DirectInput8 init chain.
4. **Translate `FUN_00475270`** — the "init indexfile ok" subsystem, almost
   certainly the `bmpdata.bin` LZW loader (cross-reference with
   `/opt/src/recettear-repacker/bmp_unpack.py`).
5. **Translate `FUN_00454e69` + surroundings** — the D3D8 device creation
   site, so we can match the original's exact present parameters and
   render-state initial values (necessary for pixel-identical diffs once we
   have real rendering).
6. **First real rendering** — load a single TGA from the extracted assets
   and draw it via a screen-aligned quad. Confirms the texture pipeline
   before we tackle any of the engine's actual draw paths.
- Wire up `tools/ghidra-headless.sh` for batch decompilation.
- Confirm DirectX version from unpacked imports.
- First extractor: `xfile.py` (validate pipeline against known format).

---

## 2026-05-19 — First real rendering: TGA + screen-aligned quad

**What landed:** `src/tga.{c,h}` (uncompressed truecolor TGA Type 2, 24/32-bit,
bottom-up or top-down → BGRA), `src/sprite.{c,h}` (`IDirect3DTexture8` via
`CreateTexture(D3DPOOL_MANAGED) + LockRect + memcpy`, screen-aligned quad
via `D3DFVF_XYZRHW | DIFFUSE | TEX1` + `DrawPrimitiveUP`, with
`SRCALPHA/INVSRCALPHA` blending and the standard half-pixel offset).
Wired into `src/main.c` behind a `--show-tga <path>` CLI flag.

**Verification.** Ran `openrecet.exe --show-tga bmp/window.tga
--capture-to <dir> --capture-every-ms 500` for 4 seconds via WSLInterop;
captured 8 BMPs showing the 64×64 `window.tga` (a rounded UI button)
correctly alpha-blended over the debug-magenta clear. Math check on
frame 4: TGA pixel `(32,32) = (23,23,47, α=133)` blended over
`(160,32,96)` predicts `(88, 27, 70)`; captured pixel reads `(89, 27,
70)` — agrees to within rounding.

**Engine-accuracy gap recorded** in
[`findings/texture-loader.md`](findings/texture-loader.md). `FUN_0047193c`
(the original's loader) hands work to `D3DXCreateTextureFromFileInMemoryEx`
(identified by 15-arg call site + D3DXERR_INVALIDDATA error path). For
BMPs it applies the green color-key `0xFF00FF00`; TGAs use native alpha.
We deliberately bypass d3dx8 (not in nixpkgs, deprecated) and will grow
our own decoders to match the engine's output. Current `tga.c` handles
Type 2 only — BMP-with-color-key and RLE-TGA come next.

**Next milestones (unchanged from prior stop point except #6 done):**

1. Translate `FUN_004341d4` (file-size helper).
2. Translate `FUN_0047af52` (DInput8 init chain).
3. Translate `FUN_00475270` (`bmpdata.bin` LZW loader; cross-ref
   `recettear-repacker/bmp_unpack.py`).
4. Translate `FUN_00454e69` + neighbours (D3D8 device creation, for
   matching the original's present parameters and initial render states).
5. Port `FUN_0047193c` properly — read assets via `storage_*`, accept
   BMPs with the green color-key, add RLE-TGA. Replaces `--show-tga`'s
   direct-file path.
6. ~~First real rendering~~ — done (this entry).

---

## 2026-05-19 — DirectInput 8 init ported (keyboard + joysticks)

**Subsystem landed:** `src/input.{c,h}` — full port of `FUN_0047af52`
("init dinput ok") plus its cleanup at `FUN_0047b0ef` and the
WM_ACTIVATE Acquire/Unacquire dance. Wired into `main.c` after
`init_render` and into `WndProc:WM_ACTIVATE` so deactivation correctly
releases device focus (matches the original's behavior).

**Pieces traced and ported:**
- `FUN_0047af52` — outer init: `DirectInput8Create`, keyboard create +
  `SetDataFormat(c_dfDIKeyboard)` + `SetCooperativeLevel(FOREGROUND|NONEXCLUSIVE)`
  + `SetProperty(DIPROP_BUFFERSIZE = 100)` + `Acquire`; then
  `EnumDevices(DI8DEVCLASS_GAMECTRL, ATTACHEDONLY)` followed by per-joystick
  `SetProperty(DIPROP_AXISMODE = ABS)` + `DIPROP_BUFFERSIZE = 100` + `Acquire`.
- `LAB_0047b167` — joystick enumeration callback. Ghidra never decompiled
  this (came up as a label, not a function); read directly from objdump on
  `vendor/unpacked/recettear.unpacked.exe`. Calls
  `IDirectInput8::CreateDevice(lpddi->guidInstance, ...)` into a 4-slot
  array, then `GetCapabilities` as a liveness probe — failure releases the
  device and zeroes the slot. Caps the joystick count at 4
  (`cmp 4; setl` — explains the static `g_joys[INPUT_MAX_JOYS]` layout).
- `FUN_0047b1f2` — per-object enum callback for `IDirectInputDevice8::EnumObjects`
  with filter `DIDFT_AXIS|DIDFT_POV`. Sets each enumerated object's
  `DIPROP_RANGE` to ±1000 via `DIPH_BYID`. (Earlier writeup said ±5000 — that
  was wrong; bytes are `0xFFFFFC18` = −1000 and `0x03E8` = 1000.)
- `FUN_0047b0ef` — symmetric shutdown: Unacquire+Release for the keyboard,
  each joystick slot, then Release the `IDirectInput8` factory.

**Other corrections to the bootstrap findings:**
- Keyboard `SetProperty` is `DIPROP_BUFFERSIZE=100`, not "DIPROP_RANGE ±5000"
  as I'd transcribed initially. The ±5000 number was never in the binary.
- The `WM_ACTIVATE` decision uses both `LOWORD(wParam)` (active/inactive)
  and `HIWORD(wParam)` (minimized flag): paused = inactive OR minimized.

**Toolchain note:** had to add `-ldxguid` to `src/Makefile` so the linker
resolves `IID_IDirectInput8A`, `GUID_SysKeyboard`, and the data-format
GUIDs that `c_dfDIKeyboard` / `c_dfDIJoystick` reference internally.

**Verified:** `tools/smoke-test.py --target openrecet --scenario boot
--capture` runs cleanly for 5 frames — debug-magenta still reads
`(160, 32, 96)` flat across the back-buffer, no crash on init or
shutdown, no MessageBox.

Next-milestone candidates (unchanged ordering from the session-starter
memo): `FUN_004341d4` (file-size helper), `FUN_00475270` (bmpdata.bin
LZW loader), `FUN_00454e69` ("init render ok" — post-device render-state
init), or porting `FUN_0047193c` properly to read assets through
`storage_*` and accept BMP+green-key in addition to TGA.

## 2026-05-19 — D3D8 device creation properly identified + matched

**Correction:** the bootstrap doc previously labeled `FUN_0047ac6a` as
"second-stage init" and `FUN_00454e69` as "init render ok". After
reading the `WinMain` dispatch carefully, **`FUN_0047ac6a` is the actual
D3D8 device-creation function** (`Direct3DCreate8` + `CreateDevice`,
present-params, behavior-flag fallback) and `FUN_00454e69` is post-device
render-state init that runs the "init render ok" log on completion.

**Findings updated** in
[`winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md): full
present-params field map, the unusual fullscreen=COPY+VSYNC swap-effect
choice (vs. windowed=DISCARD), the CreateDevice behavior-flag fallback
chain `0x44 (HW+MT) → 0x80 (MIXED) → 0x20 (SW)`, and the
`[setup] screen` resolution-lookup table
(0=640×480, 1=800×600 default, 2=1024×768, 3+=1280×960).

**Skeleton updated** — `src/main.c init_render()` now mirrors the
present-params layout (windowed/fullscreen split, COPY+VSYNC for
fullscreen) and walks the same `0x44 → 0x80 → 0x20` BehaviorFlags
fallback. Deliberate deviations recorded in code comments:
`hDeviceWindow=hwnd` (original leaves NULL → focus-window fallback,
behaviorally equivalent), `Flags=LOCKABLE_BACKBUFFER` only when
`--capture-to` is set (capture-only toggle), and hardcoded 800×600 until
the `recet.ini` parser lands.

**Verified:** smoke test runs cleanly with the new HW+MT-first chain;
sprite blend pixel still reads `(89,27,70)` — no regression.

Next: port `FUN_0047af52` (DInput8) — next subsystem in the bootstrap
order, contained scope.
