# scene1 character sprite render — FUN_0045a56f subsystem (Cchr.2)

> Follows `scene1-char-sprite-trace.md` (Cchr.1, which *named* the player
> sprite path) with the *port* dependency map + chip ladder. Started
> 2026-05-29.

## What Cchr.2 actually is

Cchr.1 ground-truthed that the visible HOUSE characters (Recette / Tear /
NPCs) are 2D billboards drawn by the leaf renderer **`FUN_0045a56f`**,
driven by the actor walkers **`FUN_00456f56`** (player + companion +
people) and **`FUN_0045672a`** (records_b / records_a / people sprite
pre-pass). Porting "characters in HOUSE" is therefore not 3 functions —
the leaf renderer reads a **character-sprite-animation data subsystem**
that the port has not built yet:

```
 FUN_0045a56f  (leaf renderer, 1223 B)   ── Cchr.2b
   ├─ reads g_chr_formdata   (DAT_0438abe0)  chr/formdata.bin blob
   │     └─ loaded by FUN_004341fe tail  ── Cchr.2a  ✅ LANDED
   ├─ reads g_chr_desc[]     (DAT_0438cea8)  per-char descriptor (sheet
   │     dims / scale / frame LUT)            (stride 0x5058, 68 chars)
   │     └─ built by FUN_00479f78 (.idx parser) ── Cchr.2a  ✅ LANDED
   ├─ reads an actor sprite-state struct (param_1, stride 0x44)
   │     └─ advanced by FUN_00482a71 (frame tick) ── Cchr.2c
   │     └─ populated by the player/actor controller (DAT_056da1cc set
   │        by the state machine ~all.c L34365)     ── Cchr.2c
   ├─ uses DAT_0438cdf8 (billboard base matrix)  ✅ ported (scene1_camera.c)
   └─ D3DXMatrix thunks (translate/scale/mul/rotY) ✅ ported (math3d mat4_*)

 FUN_00456f56  (player/companion/people walker, 1982 B) ── Cchr.2d
 FUN_0045672a  (records_b/records_a/people pre-pass, 1317 B) ── Cchr.2e
 FUN_0045aa36 / FUN_0046f648  (shadows)  ── follow-up, lower priority
```

## Per-character descriptor block (Cchr.2a, LANDED)

Built by `FUN_00479f78` from one `.idx` text file per character.
Engine static array base `DAT_0438ce88`; reader anchor `DAT_0438cea8`
= base + 0x20. Stride **0x5058 bytes (0x1416 dwords)**, **68 chars**
(PTR list 0x5c80c4..0x5c81d4; parse terminus `&DAT_044e2608`).

| off  | field        | source                  | engine sym    |
|------|--------------|-------------------------|---------------|
|+0x00 | name[0x20]   | idx line 0 ("%s")       | (DAT_0438ce88)|
|+0x20 | path[0x20]   | sprintf "idx/<name>.idx"| (DAT_0438cea8)|
|+0x40 | hdr0         | idx line 1 field 0      |               |
|+0x44 | hdr1         | idx line 1 field 1      |               |
|+0x48 | sheet_w      | idx line 2 field 0      | DAT_0438ced0  |
|+0x4c | hdr3         | idx line 2 field 1      |               |
|+0x50 | scale_x100   | idx line 4              | DAT_0438ced8  |
|+0x54 | y_origin     | idx line 3              | DAT_0438cedc  |
|+0x58 | frame LUT    | animation blocks        | DAT_0438cee0  |

**`.idx` grammar** (resolved: all sscanf formats are `"%s"`, keyword
`"HALT"` @ 0x5cb994):
- line 0: sheet name.
- line 1: `hdr0,hdr1`.  line 2: `sheet_w,hdr3`.  line 3: `y_origin`.
  line 4: `scale_x100`.  line 5: discarded.
- then per animation: `"/"` starts/terminates an animation; frame lines
  are up to 6 comma ints packed 6-dwords-each; `"HALT"` = `0x3ff`×6 hold
  marker; the slot after the last frame gets `0xffffffff` on the next
  `"/"`.  Animations are 0x100 dwords apart.

States 1 & 2 only advance when the comma (second field) is present —
faithful to the engine (it walks for the comma unbounded; the port
bounds at NUL, a benign deviation for valid data).

## Actor sprite-state struct (param_1 to the leaf; Cchr.2c/2d)

`FUN_00456f56`'s player loop walks `DAT_056dacc0 .. &DAT_056dae14`
(stride **0x44** = 0x11 dwords, 5 slots) and the companion uses
`&DAT_056dab40` + pos `_DAT_056da1f0`.  Field map (dword index into the
0x44 struct, from the leaf + the walker + FUN_00482a71):

| dw   | byte  | meaning                                              |
|------|-------|------------------------------------------------------|
|[0]   |+0x00  | animation set (×0x100 into the frame LUT)            |
|[2]   |+0x08  | frame timer (compared vs frame duration, FUN_00482a71)|
|[3]   |+0x0c  | frame counter                                        |
|[4]   |+0x10  | current frame index                                  |
|[6]   |+0x18  | facing index (→ DAT_005c5a54 facing table in leaf)   |
|[7]   |+0x1c  | flag — alpha gate (`<1` path in leaf tail)           |
|[8]   |+0x20  | flag                                                 |
|[9]   |+0x24  | flag                                                 |
|[10]  |+0x28  | flag — `>0` drives the time shimmer (FUN_00503a44)   |
|[0xb] |+0x2c  | pos.x  (player loop: `puVar4[0xb]`)                  |
|[0xc] |+0x30  | pos.y                                                |
|[0xd] |+0x34  | pos.z  (+0.02 bias in the player loop)               |
|[0xe] |+0x38  | alive / active (`>0` gate)                           |
|[0x10]|+0x40  | (one more dword to the 0x44 stride)                  |

`DAT_056da1cc` (+0x14 of the `DAT_056da1b8` actor container) = the
player's **character id** (= `param_2`/`param_3` to the leaf); set by the
player state machine around all.c L34365 (values 0x1b/0x1c/0x1e/0x1f/
0x28...).  `DAT_056da1d4` = the companion's id.  These select the
descriptor block.

## Open questions (resolve in the validated session)

1. **Leaf frame-LUT stride mismatch.** `FUN_00479f78` writes the LUT with
   char stride `0x1416` dw; the leaf reads with `char*0x359*6 = 0x141e` dw
   **+ a facing offset** from `DAT_005c5a54[param_1[6]]`.  0x141e − 0x1416
   = 8.  Likely the facing offset / a decompiler const-fold; **objdump
   `FUN_0045a56f` @ 0x45a56f before porting 2b** to nail the exact index
   expression.
2. **FUN_00482a71 indexing.** Frame-tick reads `DAT_0438cef4` (= cee0+0x14
   within a 0x18-byte frame entry?) with `*param_1*0x400` + `param_2*0x359`
   strides — does not obviously match the parser's 6-dword frame packing.
   Reconcile via objdump at 2c.
3. **Dropped FPU args in the leaf.** `FUN_005038d0` (ftol setup),
   `__ftol`, `FUN_00503a44` (time shimmer) — objdump-verify the QWORD
   loads (see [[feedback_argless_trig_decomp]]).
4. **The 68 idx filenames.** `chr_meta_idx_names()` returns NULL until the
   PTR list at 0x5c80c4 is transcribed; `chr_meta_load()` is a no-op until
   then.  Transcribe with objdump (deref 68 LE ptrs) when wiring 2a into
   boot.

## Two MVP strategies for first visible-pixel A/B (user's call)

- **A — faithful-loaders-first.** Finish 2a wiring (formdata + idx names +
  call from boot), port 2b (leaf), 2c (state + frame tick), then 2d (walker).
  First pixels appear only after ~4 chips, but each is faithful + validated
  against a Frida descriptor/vertex dump.
- **B — Frida-inject MVP (Cf.minimal pattern).** Port 2b (leaf) now,
  Frida-capture the player's descriptor block + sprite-state struct from
  retail HOUSE, inject behind `--force-player-sprite`, get first pixels in
  2 chips, then replace the injected data with the faithful loaders.

Strategy B reaches visible verification fastest and matches how HOUSE
furniture (Cf.minimal) was de-risked.  **Deferred to the user.**

## What landed (Cchr.2a, 2026-05-29)

`src/chr_sprite_meta.{c,h}` (data layer: alloc + `.idx` parser +
accessors, host-tested) + `src/chr_sprite_meta_load.c` (storage-backed
`chr_formdata_load` / `chr_meta_load`, real-build only).
9 host tests (`test_chr_sprite_meta.c`).  Not yet wired into boot
(awaits the 68-name list + a decision on when to populate the descriptor).

## Cross-refs

- `scene1-char-sprite-trace.md` — Cchr.0/Cchr.1 retail trace.
- `src/chr_sprite_meta.h` — the landed layout + accessors.
- all.c: `FUN_0045a56f` (leaf, 0x45a56f), `FUN_00456f56` (walker),
  `FUN_0045672a` (pre-pass), `FUN_00479f78` (idx parser, 0x479f78),
  `FUN_00482a71` (frame tick, 0x482a71), `FUN_004341fe` (storage init
  + formdata load tail).
