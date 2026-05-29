# scene1 character sprite render — FUN_0045a56f subsystem (Cchr.2)

> Follows `scene1-char-sprite-trace.md` (Cchr.1, which *named* the player
> sprite path) with the *port* dependency map + chip ladder. Started
> 2026-05-29.

> **2026-05-29 — POPULATOR SURVEY (objdump-grounded): the memory note
> "FUN_00436f97 populates DAT_056dacc0" is WRONG. FUN_00436f97 *clears* the
> party render array; a separate ~18 KB per-frame subsystem *fills* it.**
> Objdump of the walker (FUN_00456f56) pass-2 loop @ 0x457205-0x45741b:
> it iterates `esi` from **`0x56dacc0` → `0x56dae14`** (5 slots, stride
> `0x44`), gating each on the **age field `[esi+0x38]`** (`jle` → skip).
> The leaf's `param_1` struct pointer differs per sweep: sweep 0 (i==0)
> uses `esi-0x154` (a parallel anim-state array at `0x56dab6c`), sweep 1
> uses `esi`; position `[esi+0x2c/30/34]` and age come from the
> `0x56dacc0` array in both sweeps. Companion pass-1 uses `0x56dab40` as
> param_1, gated on `DAT_056da1d4 != -1`.
>
> Base `0x56dacc0` has only **3 referents** in the whole binary:
> `0x4375ff` (FUN_00436f97 — the *clear* loop, sets age=0 for all 5 slots),
> `0x45722a` (the walker — *read*), and `0x48c961` (inside **FUN_0048b850**,
> 5030 B — the per-frame actor controller, the *writer*). FUN_0048b850 is
> called from **FUN_0048b3f6** (the INGAME actor tick) and, at its tail
> (`0x48c98f`+), populates each render slot via
> **`FUN_0044376a(0x56da1b8, 3, slot_idx)`** — the actor logical→render
> copier, **8538 B**. So the live chain for a visible player billboard is:
>
> 1. **FUN_00436f97** (4788 B, one-shot HOUSE init) — sets player char-id
>    `DAT_056da1cc` (=0 for HOUSE), companion flag `DAT_056da1d4` (=1),
>    player logical pos `DAT_056da1d8` (= player table `DAT_056da1b8`+0x20),
>    and **clears** the `DAT_056dacc0` render array. Furniture branch
>    (the `else` at decomp L34772+) already ported as
>    `scene1_postload_walker_phase2_init`; the char/actor blocks are NOT.
> 2. **FUN_0048b3f6 (663 B) → FUN_0048b850 (5030 B)** — per-frame player/
>    actor movement+anim controller; tail populates `DAT_056dacc0` from
>    `DAT_056da1b8` via FUN_0044376a. **Entirely unported.**
> 3. **FUN_0044376a (8538 B)** — actor logical→render-slot copier. Writes
>    the render slot via `[ebx+...]` (ebx = render slot ptr). **Unported.**
> 4. **FUN_00456f56** walker + **FUN_0045a56f** leaf — render. **Ported;
>    leaf bit-exact validated (Cchr.2b).**
>
> **Implication:** "port FUN_00436f97 → visible HOUSE characters" is false.
> Faithfully it needs ~18 KB across three large functions. The cheapest
> path to a *visually-validated walker* is an MVP **render-slot inject**
> into `DAT_056dacc0` (one hand-built standing-Recette slot: age>0, pos =
> groundtruth, anim 0) behind a flag, reusing the `--force-player-sprite`
> tooling pattern — deferring the faithful FUN_0048b850/FUN_0044376a port.
> See PROGRESS 2026-05-29 populator survey.

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

## Cchr.2c — the animation frame tick (FUN_00482a71, 0x482a71, 118 B)

Ported as `chr_anim_tick()` in `src/scene1_chr_sprite.c`. This is the
one-tick animation advance for an actor sprite-state struct — the half of
Cchr.2c that is a clean pure leaf (the "struct *populate*" half is the
walker's job and lands with Cchr.2d). Signature
`FUN_00482a71(int *state, int char_id, float dt)`.

**Algorithm** (verbatim from objdump @ 0x482a71):

```
dur = (float) LUT[char][state[anim]][frame=state[4]].field5   // +0x14, int→float
if (dur <= timer) {                 // x87 fcomp; advance when timer >= dur
    state[4] = frame + 1
    timer = 0
    marker = LUT[char][state[anim]][frame+1].field0            // +0x00 of NEXT frame
    if (marker == 0x3ff)        state[4] = frame                // HALT → hold
    else if (marker == -1)    { state[4] = 0; state[3] = 0 }    // end → wrap
}
state[3] += 1                        // frame counter, unconditional
timer += dt                          // dt = 1.0 at all engine call sites
```

The LUT addressing is `DAT_0438cee0 + (frame + char*0x359)*0x18 +
anim*0x400` — i.e. char descriptor block (stride 0x5058 = 0x359*0x18),
animation block (0x400 B = 0x100 dwords), frame (0x18 B = 6 dwords). This
is exactly the `chr_meta_lut(char, anim, frame, field)` accessor Cchr.2a
already exposes; the tick reads **field 5** (current frame duration) and
**field 0** of the *next* frame (the HALT/end marker).

**Decompiler correction — the timer [2] is a `float`, not an `int`.**
Ghidra typed `param_1` as `int *` and emitted `param_1[2] = (int)(param_3
+ (float)param_1[2])`, but the asm is pure x87: `flds 0x8(%ecx)` /
`fadds` / `fstps 0x8(%ecx)` (and `fldz` on reset, `fcomps` on the
compare). The slot holds a float accumulator. The port stores/loads it via
`memcpy` into the int32 slot (strict-aliasing-clean) — `test_chr_anim_tick_
timer_is_float` pins this by accumulating fractional dt=0.5 across three
ticks (an int-truncated timer would stay 0 forever). The engine durations
are integer frame-counts and dt is always 1.0, so the float only matters
for fidelity, not current call sites.

**Benign deviation:** `chr_meta_lut` bounds the read to the LUT region and
returns 0 past the end, where the engine reads raw adjacent memory. A
correctly-authored animation always carries a 0x3ff/-1 terminator before
that edge, so the marker read never falls off in practice; if it did, the
port advances the frame (marker 0 ≠ HALT/end) rather than reading garbage.

**6 host tests** (`test_chr_anim_tick_*`): below-duration accumulate,
advance-at-duration, HALT hold, animation-end wrap (counter reset→++=1),
the float-timer pin, and NULL-safe. 2935 total, all pass; both exe targets
build warning-free.

**Remaining for the ladder:** Cchr.2e landed 2026-05-29 (below). For
*visible* HOUSE characters the remaining work is the actor populator chain
`FUN_0048b3f6 → FUN_0048b850 → FUN_0044376a` (~14 KB; see the corrected
populator survey in this file's header — `FUN_00436f97` is the furniture
writer, already landed, NOT the character populator).

## Cchr.2e — the records / people sprite pre-pass (FUN_0045672a, 0x45672a, 1317 B)

Ported as `src/scene1_chr_prepass.{c,h}`. Dispatched from
`scene1_render_meshes` (FUN_00459dfd) at L246 — the call right after the
alpha-pre wrapper's `SetTSS(MIPFILTER, NONE)` (objdump: `0x45a472` SetTSS →
`0x45a479 call 0x45672a`), where the `scene1_walk_alpha_pre_TODO` stub used
to sit. Full asm @ 0x45672a → 0x456c4e.

Three record-draw sections, in order:

| § | source table | gate | draw | world matrix |
|---|---|---|---|---|
| A | `g_scene1_records_b` (stride 0x49 dw, count `DAT_0076b964`) | `TYPE==0x61` | `scene1_emit_record` (FUN_00455191) | `Scaling × T`, AGE-gated scale |
| B | `g_scene1_records_a` (stride 0x25 dw, count `DAT_0076b960`) | `TYPE==0x97 && !=-1` | `scene1_emit_record` | `rotY(ROT_X) × Scaling(-s,s,s) × T` |
| C | people table `DAT_0076b970` (stride 0xba4, 128 entries, **unported**) | active && alpha≠0xff && desc[+0x20]==0 | leaf `FUN_0045a56f` (Cchr.2b) | `base × Scaling(desc[+0x44]·0.05) × T` |

Field map (objdump-anchored): in §A the iteration pointer sits at records_b
`OFF_AGE` (38); the `<0x46` branch tests AGE, scale-size field is dword 66,
pos at `OFF_POS_X/Y/Z` (23/24/25). §B reads records_a `OFF_TYPE` (12),
`OFF_SCALE` (14), `OFF_POS_*` (0/1/2), `OFF_ROT_X` (6, the rotY angle). §C
people-record byte offsets: sort key `+0x450`, desc idx `+0x424`, active
`+0x428`, alpha `+0x3dc`, alpha-mult `+0xaf8`, pos `+0x3f0/3f4/3f8`;
sprite-descriptor (`DAT_005c23f0`, stride 0x68) gate `+0x20`, char id /
texture-table index `+0x24`, scale `+0x44`.

`.rdata` constants (LE-decoded 2026-05-29): `0x5198d8`=0.2, `0x51935c`=0.5,
`0x519c2c`=-0.5, `0x519c7c`=0.14, `0x519d78`=0.04, `0x519d74`=-0.14,
`0x519630`=255.0 (the people-alpha `·255/255` is a verbatim no-op),
`0x5198f8`=0.05. The engine's two `Scaling(1,1,1)` multiplies (one per §A/§B
slot) are kept verbatim as commented no-ops. `FUN_00503954` == `__ftol`
(truncate toward zero).

D3D envelopes (one-time, applied lazily on the first drawn item):
- §A/§B (engine `FUN_00456c4f`): `AMBIENT=0xff000000`, `LightEnable(0,TRUE)`,
  `LIGHTING=TRUE`, `ZENABLE=TRUE`, `ZWRITEENABLE=TRUE`, `ALPHAREF=0`,
  `ALPHAOP=SELECTARG1`, **`MAG/MINFILTER=LINEAR`**, `COLOROP=MODULATE2X`,
  `SRC/DESTBLEND=ONE`, `FOGENABLE=FALSE`.
- §C (engine @ 0x456a76): `FVF=0x142`, `FOGENABLE=FALSE`,
  `ALPHAOP=MODULATE`, `COLOROP=8 (ADDSIGNED)`, `ALPHAARG1=TEXTURE`,
  `ALPHAARG2=DIFFUSE`, **`MAG/MINFILTER=POINT`**, `ZENABLE=TRUE`,
  `ZWRITEENABLE=FALSE`.

(The LINEAR-vs-POINT filter split, plus the `MIPFILTER=NONE` the alpha pass
sets just before, are the data points the texture-filtering 1:1 follow-up
wants.)

**DORMANT.** §A/§B are wired to the real `g_scene1_records_a/b` globals
(counts 0 in HOUSE → fire when their populators land). §C's people table is
unported; `chr_prepass_people_base()` returns NULL and the section is skipped
whole — same dormant pattern as the walker's NPC pass. No visible change from
this chip alone.

**Host-tested** the one non-trivial helper, the index co-sort
`chr_prepass_sort` (engine `FUN_0045526a` — a stable strict-`<` bubble sort
carrying indices). **5 tests** (basic permutation, pre-sorted, signed keys,
equal-key stability, n≤1 no-op). 2952 total pass; both exe targets build
warning-free.

## Cchr.2d — the character-sprite walker (FUN_00456f56, 0x456f56, 1982 B)

Ported as `src/scene1_chr_walker.{c,h}` — the per-frame driver that builds
the world matrix + diffuse color for every actor billboard and hands each
to the validated 2b leaf. Dispatched from `scene1_render_meshes` L248-L251
(second WIDE-frustum slot, z_far 2000), replacing the old
`scene1_walk_wide_b_TODO` stub. Full asm @ 0x456f56 + tail to 0x457713.

**Four passes** (+ a preamble/mid/tail D3D-state envelope, all live):

| pass | what | leaf call |
|------|------|-----------|
| preamble | FVF 0x142, additive-billboard TSS/RS, LightEnable(0,F), LIGHTING=F, `FUN_0047047b` (a 2nd dormant 0x43-billboard sub-walker — stubbed) | — |
| 1 companion | `DAT_056da1d4 != -1`: one billboard, blend (ONE,ONE) | `(…,2,2,M,0xff7f7f7f)` |
| 2 player/party | `DAT_056da1cc != -1`: 2-sweep loop over the 0x44-stride actor array (i=0 player @ slot-0x154, i=1 party gated on `DAT_056daae0`); spawn-pop ease + draw-order alpha; blend (SRCALPHA,INVSRCALPHA) | `(actor,id,id,M,alpha<<24\|0x7f7fff)` |
| 3 NPC | people record table (the `DAT_0076c464` family, stride 0x2e9 — the SAME table `scene1_shop_walker` models): off-screen fade ramp | `(rec,0x43,0x43,M,a<<24\|0x7f7f7f)` |
| 4 NPC sub | same table, type==1: `FUN_00456d48` (no-op stub in port, as in shop Pass F) | — |

**Pure scalar math** (host-tested, the part where decoded constants live):
- `chr_walker_fadein(counter)` — `(0x5a-counter)/30`, clamp ≤1.0.
- `chr_walker_spawn_ease(age,&sx,&sz)` — age<20: `sx×=age/20`, `sz×=((20-age)/10+1)`.
- `chr_walker_actor_alpha(age,is_party,prio,daae0)` — `(0x254-age)*8` skip-if-neg + 0x9b clamp; party override `daae0<10 → (daae0-10)*15+prio`.
- `chr_walker_npc_alpha(pos,mult)` — pos<-75 skip; ramp `(pos+70)*50+255` over [-75,-70); ×mult; both stages `__ftol` (=`FUN_00503954`, truncate).

All engine float constants decoded from .rdata @ 0x519xxx (byte-reversed
LE): −75/−70/+70/50/255 (npc fade), 0.05 (npc scale), 0.02 (player z-bias),
10/20 (ease), 0.03 (fade scale), 30 (fade divisor). The COLOROP=8
(`D3DTOP_ADDSIGNED`) preamble value is verbatim from objdump; blend-value
6 = `D3DBLEND_INVSRCALPHA`.

**DORMANT in HOUSE.** The actor sprite-state array (`DAT_056dacc0` /
companion `DAT_056dab40`) and the people table are populated by
`FUN_00436f97` (4788 B) — the unported "Cf.* writer chunk" STATUS.md lists
as the top HOUSE-pixel blocker. Until it ports, the four pass bodies
iterate nothing (the `chr_walker_*` accessors return NULL / count 0) and no
character billboards appear — exactly the state of the sibling walkers
(`scene1_shop_walker` / `scene1_alpha_walker`). The D3D state envelope IS
live and correct. When `FUN_00436f97` ports, point the accessors at the
real engine state and the bodies fire verbatim.

**12 host tests** (`test_chr_walker_*`); 2947 total pass; both exe targets
build warning-free. NPC pass-3 record-field offsets (-0x6a4 pos, +4 mult,
scale via `&DAT_005c23f0[type*0x68]+0x44 × 0.05`) are documented in the
code for the follow-up that fills the people draw once the table populates.

## Cross-refs

- `scene1-char-sprite-trace.md` — Cchr.0/Cchr.1 retail trace.
- `src/chr_sprite_meta.h` — the landed layout + accessors.
- all.c: `FUN_0045a56f` (leaf, 0x45a56f), `FUN_00456f56` (walker),
  `FUN_0045672a` (pre-pass), `FUN_00479f78` (idx parser, 0x479f78),
  `FUN_00482a71` (frame tick, 0x482a71), `FUN_004341fe` (storage init
  + formdata load tail).
