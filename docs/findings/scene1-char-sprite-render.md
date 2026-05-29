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

## Open questions

1. **Leaf frame-LUT stride — RESOLVED 2026-05-29 (objdump @ 0x45a56f).**
   No mismatch: `0x359 * 6 = 0x1416` **exactly** = the block stride in
   dwords (an earlier note miscomputed it as `0x141e`).  The leaf index is
   `facing + param_2*0x1416 + frame*6 + anim*0x100`, where
   `facing = DAT_005c5a54[param_1[6]]` is a **within-block bank offset**
   (per-facing-direction animation bank), NOT a stride term.  The parser's
   layout and the reader agree — `chr_meta_lut(char, anim, frame, field)`
   is directly usable by 2b; the facing bank is just an extra additive
   dword offset to fold in.
2. **`FUN_005038d0` — RESOLVED 2026-05-29.** It is `__alloca_probe` /
   `__chkstk` for the ~36885-float (0x24054-byte) local vertex buffer
   (`mov eax,0x24054 ; call 0x5038d0` at the prologue) — **not** a dropped
   FPU arg.  In C the buffer is a stack/heap array; no probe needed.
3. **`FUN_00482a71` indexing (2c).** Frame-tick reads `DAT_0438cef4`
   (= cee0+0x14) with `*param_1*0x400` + `param_2*0x359` strides.  Given
   `0x359*6 = 0x1416`, the `param_2*0x359` here is likely a *frame-entry*
   (6-dword) count, i.e. the same packing — re-derive the `*0x400` /
   `+0x14` offsets via objdump when porting 2c.
4. **Dropped FPU arg in the leaf — RESOLVED 2026-05-29 (objdump @ 0x45a85e).**
   `FUN_00503954` = `__ftol` (float→int trunc, standard).  `FUN_00503a44`
   = `sin(double on [esp])`; the dropped argument is
   `(float)param_1[10] · (π/2) / 20.0` (spawn age over a 20-frame ease),
   so the shimmer is `sin(age·π/2/20)·sheet_w·0.2` and is **0 for a
   standing actor** (`param_1[10]==0` → the `jle` short-circuits param_4
   to 0).  Ported faithfully in `chr_sprite_build_quads`.
5. **The 68 idx filenames.** `chr_meta_idx_names()` returns NULL until the
   PTR list at 0x5c80c4 is transcribed; `chr_meta_load()` is a no-op until
   then.  Transcribe with objdump (deref 68 LE ptrs) when wiring 2a into
   boot.

### Leaf formdata-blob (`DAT_0438abe0`) indirection (objdump-confirmed)

Per char `param_2`: `base = bigendian_u32(formdata[param_2*4 .. +3])`
(byte order b0<<24|b1<<16|b2<<8|b3).  The LUT cell index `edi` then
addresses `formdata + base + edi*2`, with parallel sub-tables at
`+0x400` (height/row) and `+0x600` (col) and `+0x800` (cell→atlas).
The Cchr.1 decompile (`CONCAT11`/`CONCAT31` byte assembly) is faithful.

## Cchr.2b leaf-renderer port spec (objdump-verified, turnkey)

`FUN_0045a56f(int *param_1, float param_2 /*char_id*/, int param_3,
undefined4 world_mtx, float param_5 /*color*/)` builds N textured
billboard quads (one per 32×32 sprite cell) into a stack vertex buffer
(FVF stride **0x18** = XYZ + DIFFUSE + UV) and draws via **DrawPrimitiveUP**.
(`param_2`/`param_3` are both the char id = `DAT_056da1cc`.)

**Prologue:** `mov eax,0x24054 ; call 0x5038d0` = `__alloca_probe` for the
~36885-float buffer — in C just a local/heap array.  `SetTransform(0x100,
world_mtx)` via vtable `[+0x94]`.

**Constants:** 0x519364 = 1.0, 0x519368 = 100.0, 0x519474 = 32.0 (cell
px), 0x51943c = π.  Helpers: `FUN_00503954` = `__ftol` (f→int trunc);
`FUN_00503a44` = `sin` (double on `[esp]`).

**Per-char setup:** `scale = chr_meta_scale_x100(char)/100.0`;
`sheet_w = chr_meta_sheet_w(char)` (= ced0); `y_origin = ced0+0x34`
(cedc); `cell = chr_meta_lut(char, anim, frame, facing-bank)` (the
formdata cell index `edi`); `ncells/start` come from the formdata blob
header (`DAT_0438abe0[base+0x400/0x600/0x800]`, base = bigendian u32 at
`formdata[char*4]`).

**Per-cell quad** (loop `local_18` cells; `iVar11 = sheet_w/32` =
cells/row):
- col = cell%iVar11, row = cell/iVar11; `px = col*32`; `x0 = -sheet_w/2 + px`
- `py = y_origin - row*32`; world Y: bottom `(py-32)*scale`, top `py*scale`
- atlas: `cols = ftol(tex_w/32)` (tex_w = `DAT_073a9b1c[param_3*0x10]`,
  tex_h = `DAT_073a9b20[...]`); `arow_px = (cell/cols)<<5`
- UV: u = `((cell%cols)*32+0.5)/tex_w` .. `((cell%cols+1)*32-0.5)/tex_w`;
  v = `(arow_px+0.5)/tex_h` .. `(arow_px+32-0.5)/tex_h`
- **facing flip** gated on `DAT_005c5a74[param_1[6]]` (0 = right): picks
  `x0±shimmer` then world X `(x0±32)*scale`
- **shimmer** (`param_1[10]` = spawn age): 0 → none; `>0` → `sheet_w*0.2`;
  `<0x14` → `sin(arg)*sheet_w*0.2` (arg @ 0x45a85e: `±|fVar|*0x519434/
  0x519520` — re-dump for exact; **dormant for the standing player,
  param_1[10]==0**)
- **color/alpha gate** (`param_1[7..9]`): `[7]>=1` → `color|0xffffff`;
  else if `[8]>0 && [9]==0` → `color & 0xff9f209f | 0x9f209f`
- z = 0; 6 verts/quad via the two 6-elem replicate loops (quad→2 tris).

**Draw tail:** if `(param_1[8]<1 || param_1[9]!=0) && param_1[7]>0`:
`SetTSS[+0xfc] ; DrawPrimitiveUP[+0x120](TRILIST, ncells*2, buf, 0x18) ;
SetTSS[+0xfc]` else a single `DrawPrimitiveUP`.  (vtable offsets match
the rest of the scene1 ports.)

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
furniture (Cf.minimal) was de-risked.  **CHOSEN by the user 2026-05-29.**

Concrete 2b/MVP step list (strategy B):
1. Transcribe the 68 idx-filename PTR list @ 0x5c80c4 → `chr_meta_idx_names()`.
2. Wire `chr_formdata_load()` + `chr_meta_load()` into boot (after storage
   init) so `g_chr_formdata` + `g_chr_desc` hold real data.
3. Port `FUN_0045a56f` per the spec above → `scene1_chr_sprite.{c,h}`
   (host-test the per-cell quad geometry).
4. Frida-capture one retail player leaf-call: `param_1` struct (0x44),
   char_id (`DAT_056da1cc`), world matrix, color — at a HOUSE frame.
5. Inject behind `--force-player-sprite`, call the leaf for the player,
   A/B vs retail; then replace the injected `param_1` with the faithful
   actor-walker port (Cchr.2d).

## What landed (Cchr.2a, 2026-05-29)

`src/chr_sprite_meta.{c,h}` (data layer: alloc + `.idx` parser +
accessors, host-tested) + `src/chr_sprite_meta_load.c` (storage-backed
`chr_formdata_load` / `chr_meta_load`, real-build only).
9 host tests (`test_chr_sprite_meta.c`).  Not yet wired into boot
(awaits the 68-name list + a decision on when to populate the descriptor).

The 68-entry idx-filename PTR list @ 0x5c80c4 **was** transcribed
(`chr_meta_idx_names()` is operational); only the boot call of
`chr_formdata_load()` + `chr_meta_load()` (step 2) remains for real data.

## What landed (Cchr.2b, 2026-05-29) — the leaf renderer

`src/scene1_chr_sprite.{c,h}` ports `FUN_0045a56f`:

- **`chr_sprite_build_quads()`** — the pure per-cell geometry (host-tested,
  no D3D).  Faithful to objdump @ 0x45a56f: facing→bank/flip via the two
  8-entry tables `DAT_005c5a54` = `{0,2,4,3,1,3,4,2}` / `DAT_005c5a74` =
  `{1,1,1,1,1,0,0,0}` (8 dirs fold onto 5 banks + horizontal mirror); LUT
  cell → formdata frame entry (`base = be_u32(formdata[char*4])`, then
  `ncells`@+0x400 / `start`@+0x600 / sheet-pos@+0x800, all big-endian);
  per cell a 6-vertex TRILIST quad (emit order **V0,V1,V2,V3,V0,V2**) with
  the sheet-position → object XY, the *linear* atlas walk (`start+i`) →
  half-texel-inset UVs, the spawn-age shimmer (`sin(age·π/2/20)·sheet_w·0.2`,
  dormant at age 0), and the `[7]/[8]/[9]` color/alpha gate.
- **`scene1_chr_sprite_render()`** (Win32) — `SetTransform(WORLD)` →
  build → the flag-gated draw tail: when
  `(actor[8]<1 || actor[9]!=0) && actor[7]>0` it brackets DrawPrimitiveUP
  with `COLOROP=7/8` (verbatim from objdump; **pending Frida A/B** to pin
  their visual intent), else a single DrawPrimitiveUP. FVF 0x142, stride
  0x18.

Constants decoded from the binary: 1.0/100.0/0.5/32.0/0.2/(π÷2)/20.0 at
0x519364/68/5c, 0x519474, 0x5198d8, 0x519434, 0x519520.

9 host tests (`test_scene1_chr_sprite.c`): ncells/count, flipped +
non-flipped geometry, atlas advance across cells, all three color-gate
branches, NULL/degenerate/truncated-formdata safety, `out_max` clamp.

## Strategy-B steps 4–5 tooling (2026-05-29) — capture + inject

The Frida-inject MVP path is now scaffolded end-to-end:

1. **Capture (retail).** `frida_capture.py --chr-leaf` rides the
   `--dump-records-b` HOUSE free-roam drive (same arming as `--quad-hist`)
   and hooks `FUN_0045a56f` at ENTER + its two in-leaf DrawPrimitiveUP
   sites.  Per dump-offset frame it writes one `chr_leaf` record to
   `<run_dir>/chr_leaf.jsonl`: `leaf_in` (the 5 inputs **plus** the
   descriptor + formdata-derived fields — `sheet_w`, `scale_x100`,
   `y_origin`, facing `bank`, resolved `cell`, `fd_base/ncells/start`,
   `fd_pos[]`) and `leaf_out` (the FVF-0x142 vertex buffer retail built).
   Self-contained → bit-A/B with no asset files.

   Example (HOUSE free-roam, adjacent offsets so the player moved):
   ```
   frida_capture.py --remote cutestation.soy:27042 --run-dir runs/cchr2b \
     --turbo --silent-audio --auto-z-spam \
     --dump-records-b --dump-records-b-offsets 8,9 --chr-leaf
   ```

2. **Inject (port).** `tools/chr_leaf_to_inject.py runs/cchr2b/chr_leaf.jsonl
   -o inject.txt` picks the player's leaf call (char id == `player_char_id`,
   matrix translation nearest `player_pos`) and writes the flat inject file.
   Then `tools/run-openrecet.sh ... --force-player-sprite inject.txt`:
   the loaders (`chr_formdata_load` + `chr_meta_load`) wire at boot so the
   descriptor + blob are real, and the ported leaf draws the player
   billboard over the HOUSE scene (inheriting its VIEW/PROJECTION).  Bind
   the sheet with `--sheet <path>` on the converter (else diffuse-only =
   white silhouette, which already validates geometry/placement).
   `--emit-expected` prints retail's `leaf_out` verts for the picked call
   to compare against the port.

## Cchr.2b VALIDATED — bit-exact A/B vs retail (2026-05-29)

Ran the capture against retail (run `runs/cchr2b`, free-roam HOUSE frame
17544, player Recette at (-0.30, 0, 9.35)).  The leaf fired 8× that frame;
the player call (char 0) had inputs: `sheet_w=128, scale_x100=100,
y_origin=114, facing=6` (bank 4, **not** flipped), `anim=0, frame=2` →
LUT cell 10; formdata `base=0, ncells=6, start=60, pos=[5,6,9,10,13,14]`;
`color=0xff808080`, `tex=512×1024`.

**`chr_sprite_build_quads` reproduces retail's full 36-vertex
DrawPrimitiveUP buffer bit-for-bit** — locked in as the regression test
`test_chr_sprite_retail_recette_house`.  The geometry math (sheet-pos →
object XY, linear-atlas → half-texel UVs, the non-flipped facing branch,
the `V0,V1,V2,V3,V0,V2` emission order) is ground-truth-correct.

The standing player's flags `[7][8][9] = 0/0/0` take the **single
DrawPrimitiveUP** tail (the `COLOROP=7/8` bracket is a *different* flag
state — not exercised by a normal standing actor, still transcribed
faithfully but A/B-unconfirmed for that branch).  Retail's `color =
0xff808080` (mid-grey) is doubled to white by the scene's MODULATE2X —
consistent with the brightness finding ([[openrecet_house_brightness_resolved]]).

**Remaining:** the actor-walker port (Cchr.2d) that *builds* `param_1` per
frame, which then feeds this validated leaf — at which point the
`--force-player-sprite` inject is replaced by the real walk path and
step-2 boot-wiring folds into the normal boot.  A port-side rendered
visual (vs. the bit-exact proof here) additionally needs the sheet-texture
load path; lower priority now that geometry is ground-truthed.

## Cross-refs

- `scene1-char-sprite-trace.md` — Cchr.0/Cchr.1 retail trace.
- `src/chr_sprite_meta.h` — the landed layout + accessors.
- all.c: `FUN_0045a56f` (leaf, 0x45a56f), `FUN_00456f56` (walker),
  `FUN_0045672a` (pre-pass), `FUN_00479f78` (idx parser, 0x479f78),
  `FUN_00482a71` (frame tick, 0x482a71), `FUN_004341fe` (storage init
  + formdata load tail).
