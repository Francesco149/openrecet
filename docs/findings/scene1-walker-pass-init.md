# Scene-1 pass-init walker — FUN_00457714 survey

**Status (2026-05-26):** Survey only. Written after re-examining what
was previously called the "per-pass texture/shader uniforms" stub
(`scene1_walk_pass_init_TODO` in `src/scene1_render.c`). The label was
**wrong** — FUN_00457714 is a per-NPC mesh walker that consumes the
shop_table meshes loaded by our C0A worker (`src/scene_table.c`). The
L52952 inner draw loop is the **shop_table furniture renderer** for
HOUSE.

No code lands from this doc. The chip that consumes the survey is the
next planner's call — likely a multi-chip port that starts with
FUN_00455191 (217 B, the NPC-anchored single-mesh helper used in
several siblings) and grows into a FUN_00457714 inner-loop port.

See `docs/findings/scene1-render.md` for the broader C7/C8 ladder,
`docs/findings/scene1-walker.md` for HUD/2D walker context (which
also references DAT_073dddb4 as the status-screen flag), and the
HOUSE-blockers memory note for the higher-level reachability table.

## Old label vs corrected label

| | Old (HOUSE blockers survey 2026-05-26 AM) | Corrected (this survey, 2026-05-26 PM) |
|---|---|---|
| FUN_00457714 | "per-pass texture/shader uniforms" | "per-pass NPC-anchored mesh walker" |
| Body | "stub" / no draw output expected | TWO inner mesh-draw loops (DAT_068dcca0 + DAT_073b1ac8) per NPC |
| HOUSE reachability | "stub blocked by port" | **reachable + would emit furniture pixels if mesh-NPC binder fires** |

The lower-level survey (`scene1_render.c` L142-148 comments) called
it "per-pass setup" and "purpose unknown"; the corrected reading is
that this IS the HOUSE-furniture render pass.

## Decomp call site

`scene1_render_meshes` (decomp `FUN_00457b3b` from L52599's caller
chain) at L188 calls `FUN_00457714(0)` as the first walker entry —
i.e. `scene1_walk_pass_init_TODO(0)` in our port.  Also called with
arg `1` and `3` from sibling walker entry points (see the 4×
per-frame contract documented in PHC #26 for FUN_00459847).  Our port
only wires the arg-0 call from `scene1_render_meshes`.

## Body map (decomp L52599-L53330)

The function has a single outermost gate at L52657
(`DAT_073dfcec == 0`, the alpha-pass guard — BSS-zero and never
written, so always TRUE in retail).  Inside that, an inner gate at
L52658 dispatches HOUSE vs DUNGEON:

```c
if (*(int *)(&DAT_068dd2f8 + DAT_0438b4dc * 0x1b3c) < 1) {
    /* HOUSE branch — L52659..L53044 */
} else {
    /* DUNGEON branch — L53046..L53330 */
}
```

`DAT_068dd2f8` is the per-stage palette base + offset; `DAT_0438b4dc`
is the per-stage selector.  HOUSE stages have palette field == 0 →
HOUSE branch fires.  Confirmed via `src/scene1_preload.c` palette
notes.

### HOUSE branch sub-structure

| # | Lines | Role | Data source | Gate | HOUSE reachable? |
|---|---|---|---|---|---|
| 1 | L52659-L52669 | clear-state | n/a | `DAT_0438b198 != 0` | dormant in retail (BSS-zero?) |
| 2 | L52671-L52701 | **setup phase 1** — build per-mesh transforms for the DAT_068dcca0 (wall/floor/jutan) array into `local_738[]` | `DAT_068dcca0` (mesh array, populated by scene_floor/jutan/walls loaders) | `DAT_0438bfb0 != 0` | **yes** — FUN_00436f97 sets non-zero (L34776/80/85/88) |
| 3 | L52704-L52803 | **setup phase 2** — build per-mesh transforms for the DAT_073b1ac8 (shop_table) array into `local_5f8[]` | `&DAT_0438c01c` (per-stage transform array, set by FUN_00436f97) | `DAT_0438bfb4 != 0` | **yes** — FUN_00436f97 sets non-zero (L34777/81/84/89) |
| 4 | L52806 | barrier | `FUN_00454f7c()` | unconditional | yes |
| 5 | L52809-L53043 | **NPC outer iteration** — per-NPC texture dispatch + two inner draw loops | `DAT_073be5e8[]` (people-table) + `DAT_073cb108` (NPC count) | `DAT_073cb108 != 0` | depends on people-table init |
| 5a | L52813-L52870 | NPC flag dispatch — picks a per-NPC SetTexture target based on 6 char-flag bytes at NPC + 0x1cf2c43..0x1cf2d3d | flag bytes (gameplay state) + various texture LUTs (DAT_073ac728, DAT_073b18d8, DAT_073cc630 etc) | per-flag + per-`param_1` arm | depends on flags |
| 5b | L52883-L52901 | per-NPC palette-state writes — sets DAT_0438bfbc..bff0 (a fan-out of color/material palette overrides) | `&DAT_005c5a00` LUT indexed by `local_24[0xb37b]` | unconditional | yes (writes BSS) |
| 5c | L52902-L52950 | **draw loop A** — DAT_068dcca0 (wall/floor/jutan) per-NPC mesh draw | DAT_068dcca0 + DAT_0438bfb8[] index array | `DAT_0438bfb0 != 0` | yes (same gate as #2) |
| 5d | **L52952-L53039** | **draw loop B** — DAT_073b1ac8 (shop_table) per-NPC mesh draw | DAT_073b1ac8 + DAT_0438bfcc[] index array | `DAT_073dddb4 == 0 && DAT_0438bfb4 != 0` | **yes** — status-screen closed + per-stage count set |

### Draw loop B inner shape (the shop_table renderer)

```c
if (DAT_073dddb4 == 0 && DAT_0438bfb4 != 0) {
    matrix_array = local_5f8;
    flag_array   = local_24 + 0xb1d4;   /* per-stage byte flags */
    idx_array    = &DAT_0438bfcc;       /* per-stage mesh-index array */

    for (i = 0; i < DAT_0438bfb4; i++) {
        if (flag_array[i] == 0) {
            /* shop_table path — uses C0A-loaded furniture mesh */
            mesh = &DAT_073b1ac8 + (idx_array[i] - 3 + selector*2) * 0x28;
        } else {
            /* wall/floor/jutan path — uses DAT_068dcca0 mesh array */
            mesh = &DAT_068dcca0 + (idx_array[i] - 0x28a0 + ((flag_array[i] >> 6) * 2)) * 0x28;
        }

        SetTransform(D3DTS_WORLDMATRIX(...), matrix_array[i]);

        if (mesh->vtable != NULL && mesh->face_count > 0) {
            for (face_i = 0; face_i < mesh->face_count; face_i++) {
                if (mesh->face_npc_ptr[face_i] == current_npc) {
                    /* level-abilities pulse — DAT_0438cc08==2 && local_1c==DAT_0438bea4 path */
                    /* sets a per-face color from DAT_0438b8cc * 0.2 */
                    if (DAT_0438cc08 == 2 && local_1c == DAT_0438bea4) {
                        push_pulse_color(DAT_0438b8cc);
                    }

                    SetTransform(D3DTS_WORLD, &mesh->per_face_matrices[face_i]);
                    mesh->vtable->draw(mesh, face_i);

                    /* re-clear after pulse */
                    if (DAT_0438cc08 == 2 && local_1c == DAT_0438bea4) {
                        FUN_00454f03(DAT_068dd2f0 + 0x1a40);
                        clear_pulse_color();
                    }
                }
            }
        }
        matrix_array  += 16;     /* 16 floats = 64 B = D3DXMATRIX */
        flag_array    += 1;
        idx_array     += 1;
    }
}
```

### Gate writer table

| Gate | Default | Writer(s) | Semantics |
|---|---|---|---|
| `DAT_073dfcec` | 0 (BSS) | none in binary | alpha-pass guard; **dead in retail**, stays 0 |
| stage palette mode at +0 of `DAT_068dd2f8 + DAT_0438b4dc * 0x1b3c` | per-stage data | data-driven (palette tables) | HOUSE = 0, DUNGEON = 1+ |
| `DAT_0438bfb0` | 0 (BSS) | FUN_00436f97 L34776/80/85/88 (per-stage) | mesh-count for wall/floor/jutan path |
| `DAT_0438bfb4` | 0 (BSS) | FUN_00436f97 L34777/81/84/89 (per-stage) | mesh-count for shop_table path |
| `DAT_073dddb4` | 0 (BSS) | FUN_00475270 (config "effectmode" parser); status-screen open/close writers at 0x476aaf / 0x476b8b | "**status-screen active**" flag — per scene1-walker.md L380 |
| `DAT_073cb108` | 0 (BSS) | people-table init (still partially unported) | NPC count |

### Cross-ref: FUN_00455191 — the reusable per-NPC mesh draw helper

`FUN_00455191` (217 B at 0x455191, decomp L51528) is currently a no-op
stub in `src/scene1_render.c::scene1_walk_initial_asset_TODO`.  Reading
its body shows it is **the same NPC-anchored draw pattern as draw loop
B, but for a single mesh** (parameter `param_1` is one mesh pointer):

```c
void FUN_00455191(mesh_t *m) {
    FUN_00454f7c();        // common pre-draw
    if (DAT_073cb108 == 0) goto epilogue;
    for (i = 0; i < DAT_073cb108; i++) {
        if (m->vtable == 0) continue;
        FUN_00454fe4(i);   // per-NPC SetTextureStageState (429 B at 0x454fe4)
        for (face_i = 0; face_i < m->face_count; face_i++) {
            if (m->face_npc_ptr[face_i] == i) {
                if (!texture_set) {
                    SetTexture(0, npc_table[i]);
                    texture_set = true;
                }
                SetTransform(D3DTS_WORLD, m->per_face_matrices[face_i]);
                m->vtable->draw(m, face_i);
            }
        }
    }
epilogue:
    SetTextureStageState(0, 0xd, 1);  // restore COLOROP1
    SetTextureStageState(0, 0xe, 1);  // restore COLORARG1
}
```

Callers: shop_walker (FUN_004552d0 at L51706/37/50/97 — C8c ports
still stub the per-record draws); alpha_pre walker (FUN_0045672a at
L52185/L52209 — still TODO).  And the L52952 block in FUN_00457714 is
an inlined version of this same idiom over an array of meshes.

**Porting FUN_00455191 unlocks four call sites at once.**  The
217 B includes both an NPC loop and a face loop, so the port has 4
non-trivial responsibilities: per-NPC state via FUN_00454fe4 (429 B),
per-mesh-face NPC association check, per-face matrix SetTransform,
and mesh vtable draw.

## Per-mesh-face NPC association — the dormant variable

The L51550 / L52973 check `mesh->face_npc_ptr[face_i] == current_npc`
is the crucial filter.  In retail HOUSE this only draws if the mesh
loader (or a per-stage binder) populated `face_npc_ptr[]` to point
into the people-table.

**Unknown today:** which code path populates `mesh->face_npc_ptr[]`?
If `mesh_load` writes them at .x parse time (using a per-face vertex
attribute like a bone index), then the data is intrinsic to the mesh
file.  If a separate binder walks the people-table after load and
writes pointers into the meshes, then porting the binder is a
prerequisite for visible pixels.

Either way, the survey conclusion stands: the L52952 block is **not
dead** — it has a clear gate, a clear data source, and a clear
visual purpose (furniture meshes that move with NPCs / are owned by
specific NPCs).

## HOUSE-pixels reachability verdict

| Layer | Status |
|---|---|
| C0A worker (shop_table.x meshes loaded into DAT_073b1ac8) | **DONE** — `src/scene_table.c` |
| FUN_00436f97 sets DAT_0438bfb4 nonzero (per-stage count) | partial — Cf.1 ports block 11+23; the L34770+ chunk that writes 0xbfb4 is in a different block (block 21? — not yet identified by chip number) |
| FUN_00457714 walker — body (L52599-L53330) | **stub** — `scene1_walk_pass_init_TODO` no-op today |
| FUN_00455191 helper — per-NPC single-mesh draw | **stub** — `scene1_walk_initial_asset_TODO` no-op today |
| FUN_00454fe4 — per-NPC TextureStageState picker | unported |
| `mesh->face_npc_ptr[]` populator | **UNKNOWN** — needs trace |
| People-table init (DAT_073cb108 > 0) | partial |
| Status-screen flag stays 0 outside menus (DAT_073dddb4) | yes (default) |

So a minimal HOUSE-furniture port needs **at minimum**: FUN_00455191
+ FUN_00454fe4 + the FUN_00457714 inner-loops + the
`mesh->face_npc_ptr[]` population path.

## Recommended next chips

In order of effort-to-pixel:

1. **Trace `mesh->face_npc_ptr[]` population** — read FUN_00472836
   (mesh_load) + the per-stage post-load binders (FUN_00474681,
   FUN_00474a55, FUN_00474d92) to identify if/where face→NPC
   association is written.  Until this is known, even a perfect
   FUN_00457714 port draws nothing in HOUSE.  **Lowest cost; highest
   information leverage.**
2. **Port FUN_00455191 (217 B)** — small, well-scoped, four callers
   already exist as stubs.  Useful regardless of whether HOUSE
   furniture renders today, because shop_walker (C8c) per-pass body
   stubs also call into this helper.  Treat as a self-contained chip
   labeled e.g. **PII.1**.
3. **Port FUN_00454fe4 (429 B)** — straight-line dispatch over 4 NPC
   flag bytes, clear D3D SetTextureStageState contract.  Chip e.g.
   **PII.2**.
4. **Port FUN_00457714 inner-loops only** — start with the L52704
   setup phase 2 (D3DX matrix build), then L52952 draw loop B.  The
   matrix-build phase is large (~100 lines of D3DX calls) but
   self-contained.  Chip e.g. **PII.3a / .3b**.
5. **Visible HOUSE smoke** — after PII.3 lands AND step #1 reveals
   the binder, a HOUSE boot should show shop_table meshes
   underneath / behind Recette in the shop.  If the survey from #1
   shows the binder is itself unported, that becomes PII.0.

## What is NOT in scope here

- The DUNGEON branch (L53046-L53330) — different mesh sources
  (DAT_005cc2ec count, DAT_073e03ac per-tile-grid).  Larger but
  parallel structure.  Defer to a sibling survey.
- The L52659-L52669 clear-state — only fires when `DAT_0438b198 != 0`
  (probably a debug or transition flag).
- The 6 NPC flag bytes at NPC+0x1cf2c43.. — these are gameplay-state
  bytes (probably "is the NPC currently showing a particular
  emote/effect").  Their writers are in the gameplay layer, not the
  render layer.

## Re-runnable verification

The decompile reads are reproducible from `docs/decompiled/all.c`:

```
grep -n "^void FUN_00457714\|^void FUN_00455191\|^void FUN_00454fe4" docs/decompiled/all.c
# 51429: FUN_00454fe4 (429 B)
# 51528: FUN_00455191 (217 B)
# 52599: FUN_00457714 (5323 B)
```

Asm verification of the gate writers:

```
nix develop --command i686-w64-mingw32-objdump -d -M intel \
    vendor/unpacked/recettear.unpacked.exe \
  | grep -E '0x438bfb0|0x438bfb4|0x73dddb4'
```

Cross-reference:

- `docs/findings/scene1-walker.md` L123 + L380 confirms
  DAT_073dddb4 = status-screen active flag.
- `docs/findings/scene1-render.md` L206 mentions FUN_00455191 in the
  shop_walker per-record draw chain.
- `docs/findings/scene1-wide-followup.md` L186 also references
  FUN_00455191 as the per-record draw entry.

## PII.0 findings — 2026-05-26 PM (research chip; doc-only)

PII.0 traced the populator for what this survey calls
`mesh->face_npc_ptr[]`.  **The naming was wrong throughout the
preceding sections.**  The populator is `mesh_load` itself
(FUN_00472836 at 0x472836), and **it is already ported** as
`src/mesh_load.c` — the "binder" question is moot because the data
the walker filters on is `m->texture_slots[]`, not an NPC-association
table.

### What the engine field actually is

- `param_1[1]` in FUN_00472836 (the `texture_indices` array per
  `src/mesh.h:86`) is allocated as `operator_new(local_c * 4)` where
  `local_c` is the **material count** returned by
  `D3DXLoadMeshFromXof` (FUN_004c8f74 wrapper).  Initial-fill is
  `0xffffffff`, then each material's `pTextureFilename` is matched
  against the global texture-name cache.
- The global texture-name cache lives at `&DAT_073be908` (200 entries
  × 256 bytes per name) with the count at `DAT_073cb108`.  Cache
  entries are inserted on first encounter; ten parallel
  side-tables at `DAT_073cb10c..DAT_073cb814` record the filename
  classification (water / hikari / kabe_ / yuka_ / shop_jutan /
  ext_tga / has_n_ / has_w_ / u_index / v_index).
- `DAT_073be5e8` (parallel-to-cache, 200 × 4 bytes) holds the
  `IDirect3DTexture8*` for each slot — populated by
  `FUN_00471b24` at L71518 (`FUN_00471b24(&DAT_073be5e8 + DAT_073cb108, local_248)`).
- Asm verification at 0x472cee-0x472d3f confirms the side-table writes
  are keyed by `DAT_073cb108`:
  ```
  mov  eax, ds:0x73cb108
  lea  eax, [eax*4 + 0x73be5e8]       ; &DAT_073be5e8[count]
  call 0x471b24                        ; insert sprite handle
  ...
  mov  ecx, ds:0x73cb108
  mov  [ecx + 0x73cb4f4], al           ; ext_tga side-table @ count
  mov  [eax + 0x73cb1d4], cl           ; hikari side-table
  mov  [eax + 0x73cb29c], cl           ; kabe_ side-table
  mov  [eax + 0x73cb364], cl           ; yuka_ side-table
  ```
  Verbatim mapping of `mesh_load.h:62-92`'s `mesh_tex_flags` struct.

So the engine table the survey calls a "people-table" is actually the
**global texture cache** — `g_mesh_tex_cache` in our port.

### What the walker actually does

FUN_00457714's outer loop at L52809 iterates `DAT_073cb108` slots:
**this is texture-cache slots, not NPCs.**  Per outer slot, the
function picks a `SetTexture` target via a flag-byte switch over the
side-tables and then walks every mesh in the per-stage arrays
(DAT_068dcca0 + DAT_073b1ac8), drawing each material whose
`texture_slots[face_i] == current_outer_slot`.  Classic per-texture
state-sorted draw batching:

```c
for (slot = 0; slot < DAT_073cb108; slot++) {       /* per texture cache slot */
    sprite = DAT_073be5e8[slot];                    /* IDirect3DTexture8* */
    if (DAT_073cb4f4[slot] /* .ext_tga */ ) ...      /* per-flag dispatch overrides */
    else if (DAT_073cb1d4[slot] /* .hikari */) ...
    else if (DAT_073cb29c[slot] /* .kabe_  */) sprite = DAT_073cc630[stage_kabe_idx * 4];
    else if (DAT_073cb364[slot] /* .yuka_  */) sprite = DAT_073b18d8[stage_yuka_idx * 4];
    else if (DAT_073cb42c[slot] /* .shop_jutan */) sprite = DAT_073ac728[stage_jutan_idx * 4];
    SetTexture(0, sprite);

    for each mesh in DAT_068dcca0[]:
        for face_i in 0..mesh->face_count:
            if (mesh->texture_slots[face_i] == slot)
                SetTransform(D3DTS_WORLD, mesh->per_face_matrices[face_i]);
                mesh->vtable->draw(mesh, face_i);

    for each mesh in DAT_073b1ac8[]:
        ... same shape, gated on DAT_073dddb4==0 && DAT_0438bfb4!=0
}
```

The L52813-L52870 "NPC flag dispatch" the survey describes is just
this per-slot flag-byte switch; the 6 magic offsets
`0x1cf2c43 / 0x1cf2c75 / 0x1cf2ca7 / 0x1cf2cd9 / 0x1cf2d0b / 0x1cf2d3d`
are dword-indexed reads from `local_28` (the slot counter
extended via uint*) that resolve via Ghidra's pointer-arith
canonicalisation to byte addresses 0x73cb10c / 0x73cb4f4 / 0x73cb1d4 /
0x73cb29c / 0x73cb364 / 0x73cb42c — i.e. the water / ext_tga / hikari /
kabe_ / yuka_ / shop_jutan side-tables at the current slot
(verified: `0x1cf2c43 * 4 == 0x73cb10c`).

### Verdict for HOUSE-pixel reachability

The earlier table in this doc that listed the populator as **UNKNOWN**
is **fully resolved**:

| Layer | Status |
|---|---|
| C0A worker (shop_table.x meshes loaded into DAT_073b1ac8) | **DONE** — `src/scene_table.c` |
| FUN_00436f97 writes DAT_0438bfb4 nonzero (per-stage count) | partial (Cf.1 ladder) |
| FUN_00457714 walker — body (L52599-L53330) | **stub** — `scene1_walk_pass_init_TODO` no-op today |
| FUN_00455191 helper — per-NPC single-mesh draw | **stub** — `scene1_walk_initial_asset_TODO` no-op today |
| FUN_00454fe4 — per-cache-slot TextureStageState picker | unported |
| `mesh->texture_slots[]` populator (formerly `face_npc_ptr[]`) | **DONE** — `src/mesh_load.c::mesh_load`, populates `g_mesh_tex_cache` |
| `g_mesh_tex_cache.count` (= DAT_073cb108) > 0 in HOUSE | **YES** — every `mesh_load` call grows it |
| `g_mesh_tex_cache.entries[i].sprite` (= DAT_073be5e8[i]) populated in HOUSE | **YES on Win32** — `mesh_load_finalize_win32` fills sprite handles after upload |
| Status-screen flag stays 0 outside menus (DAT_073dddb4) | yes (default) |

**There is no missing per-stage binder.**  Porting the walker bodies
WILL produce visible HOUSE-furniture pixels (driven by the
shop_table meshes loaded by `src/scene_table.c`), because the data
they filter on is already correctly populated by `mesh_load`.

### Revised PII.1+ chip ladder

The original ladder structure is unchanged — only the
"will-this-produce-pixels" risk is gone:

| Chip | Function | Bytes | Notes |
|---|---|---|---|
| PII.0 | (research) | — | **THIS LANDING** — texture-cache binder traced to mesh_load; survey's `face_npc_ptr[]` is `texture_slots[]` |
| PII.1 | FUN_00455191 | 217 | single-mesh per-cache-slot draw helper; 4 callers (shop_walker L51706/37/50/97 + alpha_pre walker L52185/L52209); reads `g_mesh_tex_cache` for sprite + flags |
| PII.2 | FUN_00454fe4 | 429 | per-cache-slot SetTextureStageState picker; called from FUN_00455191 inner-NPC loop |
| PII.3a | FUN_00457714 setup phase 2 | ~100 LoC | per-mesh world-matrix builder for shop_table array (DAT_073b1ac8 → local_5f8[]) |
| PII.3b | FUN_00457714 NPC outer loop + draw loop B | ~250 LoC | the actual HOUSE-furniture renderer; once landed, HOUSE entry should show shop_table meshes |
| PII.3c (optional) | FUN_00457714 draw loop A + DUNGEON branch | — | wall/floor/jutan path + DUNGEON; defer until HOUSE visually validated |

### Naming corrections to the earlier sections

Throughout the preceding sections of this doc, the following terms
were inaccurate.  They're left in place for git-diff continuity, but
when reading, mentally substitute:

| Survey wording | Correct meaning |
|---|---|
| `face_npc_ptr[face_i]` | `texture_slots[material_i]` (= our `m->texture_slots[i]`) |
| `mesh->face_count` | material count (= our `m->material_count`) — engine reuses one count for both materials and the parallel texture_slots array |
| `current_npc` | current outer-loop texture cache slot |
| `DAT_073cb108` "NPC count" | global texture cache count (= `g_mesh_tex_cache.count`) |
| `DAT_073be5e8[]` "people-table" | per-cache-slot `IDirect3DTexture8*` array (= `g_mesh_tex_cache.entries[i].sprite`) |
| `DAT_073be908` "NPC name table" | per-cache-slot texture filename (= `g_mesh_tex_cache.entries[i].name`) |
| "6 char-flag bytes at NPC + 0x1cf2c43..0x1cf2d3d" | 6 of the 10 texture-classification side-tables, indexed by cache slot (water/ext_tga/hikari/kabe_/yuka_/shop_jutan) |

### Re-runnable verification (PII.0)

Texture-cache write at the count-keyed lea + per-flag byte writes:
```
nix develop --command i686-w64-mingw32-objdump -d -M intel \
    --no-show-raw-insn vendor/unpacked/recettear.unpacked.exe \
    --start-address=0x472cd0 --stop-address=0x472d70
```
Expect: `lea eax, [eax*4 + 0x73be5e8]` + 10 `mov BYTE PTR [eax+0x73cb???], cl` side-table writes.

Walker outer loop count read:
```
nix develop --command grep -nE \
    'DAT_073cb108|DAT_073be908|DAT_073be5e8' docs/decompiled/all.c \
  | awk -F: '$1 >= 52599 && $1 <= 53330'
```
Expect: 2 outer-loop blocks (L52809-L53043 HOUSE, L53148-L53326 DUNGEON), each iterating from `&DAT_073be5e8` while `local_xx != DAT_073cb108`.

Confirmation that mesh_load.h matches the engine layout:
```
nix develop --command grep -n 'param_1\[1\]\|texture_indices\|FUN_00472836' \
    src/mesh.h src/mesh_load.h src/mesh_load.c | head -10
```
Expect cross-references in `mesh.h:86` and `mesh_load.h:14` confirming
the cache is the engine's DAT_073be908+DAT_073cb108 reservation.

## PII.3b landing — 2026-05-26 PM

**What landed:** the outer cache-slot loop + draw loop B of
`FUN_00457714`'s HOUSE branch (decomp L52806-L53043).  Renames
`scene1_walk_pass_init_TODO` → `scene1_walk_pass_init` and wires it
through `scene1_walker_pass_render_house(dev, param_1)` from
`src/scene1_walker_pass_init.c`.

**API surface:**

| Symbol | Purpose |
|---|---|
| `scene1_walker_pass_render_house(dev, param_1)` | Win32-only public entry; outer slot loop + draw loop B |
| `scene1_walker_classify_slot(...)` | Pure-C 6-flag dispatch → enum `scene1_walker_slot_action` |
| `scene1_walker_draw_b_mesh_index(mesh_type, flag, selector, *out)` | Pure-C mesh-index calculator (shop_table vs wall/floor path) |
| `g_scene1_walker_status_screen_open` | Engine `DAT_073dddb4`; gates draw loop B |
| `scene1_walker_set_{kabe,yuka,jutan,animated}_texture_hook(fn)` | Stage-texture lookup hooks (default NULL) |
| `scene1_walker_set_shop_table_selector_hook(fn)` | Engine `local_24[0xb37c]` (default → `g_scene_table_selector`) |

**Iteration shape:**

```c
scene1_emit_preamble(dev);                          // L52806 barrier
for slot in [0, g_mesh_tex_cache.count):
    action = scene1_walker_classify_slot(slot_flags..., param_1);
    if action == SKIP: continue;
    SetTexture(0, pick_texture_for_action(slot, action));
    scene1_emit_apply_material_state(dev, slot);    // L52883 FUN_00454fe4

    /* L52902 draw loop A — DAT_068dcca0; SKIPPED (PII.3c) */

    if g_scene1_walker_status_screen_open != 0: continue;
    for mesh_i in [0, phase2_n):
        flag = phase2_flag_hook(mesh_i);
        idx = mesh_type[i] - 3 + selector*2     (when flag == 0)
        m = g_scene_table[idx];                  (shop_table path)
        SetTransform(WORLD, phase2_matrices[mesh_i]);
        SetStreamSource; for submesh in m: if texture_slots[mat_i] == slot:
            SetIndices; SetMaterial; DrawIndexedPrimitive;
```

**Per-slot flag cascade** (decomp L52813-L52870 verbatim):

| Pass `param_1` | Selected action (when other flags zero) |
|---|---|
| 0 (default + kabe + yuka + jutan) | `DEFAULT` (cache sprite), `KABE`, `YUKA`, `JUTAN` |
| 1 (ext_tga) | `EXT_TGA` (cache sprite) |
| 2 (water) | `WATER` (animated overlay, armed-once) |
| 3 (hikari) | `HIKARI` (animated overlay, armed-once) |

Production call sites today: `scene1_render_meshes` → arg=0 (pre-pass)
+ arg=1 (alpha-pre).  arg=2/3 come from alpha walker `FUN_00458bdf`
sub-call (still stubbed in `scene1_alpha_walker.c`).

**Per-mesh-source selection** (asm 0x4583b8..0x4583f8):

- per-mesh flag (read via PII.3a `scene1_walker_phase2_flag_fn` hook,
  shares the same memory `local_24 + 0xb1d4 + i*4`)
- flag == 0 → shop_table path; idx = `mesh_type[i] - 3 + selector*2`;
  resolves into `g_scene_table[idx]` (= our scene_table.c storage).
- flag != 0 → wall/floor path; idx = `mesh_type[i] - 0x28a0 +
  (flag>>6)*2` against `DAT_068dcca0`.  **Skipped in PII.3b**
  (mesh array not exposed; PII.3c scope).

**Pulse path (L52974-L53028):** the per-face level-abilities pulse
inside draw loop B — gated by `DAT_0438cc08 == 2 && local_1c ==
DAT_0438bea4`.  Both gates BSS-zero in HOUSE retail.  Skipped in
PII.3b; the per-face draw fires directly without the pulse-color
wrapper.

**HOUSE-entry behaviour:**

- `g_scene1_walker_phase2_count == 0` (writer FUN_00436f97 chunk
  unported — Cf.* sub-chip).  Draw loop B short-circuits per slot;
  outer loop still runs the per-slot SetTexture + TSS picker for
  state-fidelity.
- All 4 stage-texture hooks default NULL; per-slot SetTexture binds
  the default cache sprite for flag-zero slots and NULL elsewhere
  (no visible effect since the geometry doesn't iterate).
- Canaries bit-exact: boot-idle 3/3, title-z-press 14/14,
  title-down-press 4/4, title-options 2/4 (frames 39/60 pre-existing
  regression untouched).

**Next chip on the PII ladder:**

- **Cf.* sub-chip** = port the FUN_00436f97 writer chunk that
  populates `DAT_0438bfb4` (= our `g_scene1_walker_phase2_count`) +
  the 5 per-mesh parallel arrays (mesh_type / rot_y / pos_x/y/z) +
  the per-mesh flag at `local_24+0xb1d4+i*4`.  After Cf.* lands, a
  HOUSE-entry boot should produce visible shop_table furniture
  pixels (drives PII.3b's draw loop B end-to-end).
- **PII.3c (optional)** = port FUN_00457714 setup phase 1 (DAT_068dcca0
  matrix builder, L52671-L52701) + draw loop A (L52902-L52950) +
  DUNGEON branch.  Defer until HOUSE visually validated and a
  DUNGEON stage scenario lands.

**Re-runnable verification (PII.3b):**

```
nix develop --command i686-w64-mingw32-objdump -d -M intel \
    --no-show-raw-insn vendor/unpacked/recettear.unpacked.exe \
    --start-address=0x4581df --stop-address=0x458570
```

Expect: per-slot dispatch starts at LAB_004581df (call 0x454fe4 =
scene1_emit_apply_material_state), BSS palette writes at
0x438bfbc..0x438bff0, draw-loop-A gate at 0x4581e8, draw-loop-B at
0x458382-0x458567 (status-screen + phase2 count gates, per-mesh source
selection at 0x4583b8-0x4583f8, per-face inner draw at 0x458425+).

## Cf.survey landing — 2026-05-26 PM (writer-chunk reachability + layout)

Survey of the writer chunk that PII.3b waits on.  Done before any
code lands so the production port can correct the existing
`scene1_walker_pass_init.h` field-naming swap (see §Layout
corrections below).

### Source location

The writer chunk is inside `FUN_00436f97` (4788 B, decomp L34770+ /
asm 0x4378e0..0x437b7c).  It lives in the function's `else` branch
("alt-stage arm" per `docs/findings/scene1-postload-init.md` block
21).  The gate at L34361-34364:

```c
iVar6 = (&DAT_068dd3fc)[DAT_0438b4dc * 0x6cf];
if ((iVar6 < 0) || (4 < iVar6)) {
    /* Block 20 — DUNGEON-class stage-class init */
} else {
    /* Block 21 — HOUSE-class stages, iVar6 ∈ [0..4]
     * THIS IS THE WRITER CHUNK */
}
```

`DAT_068dd3fc[stage*0x6cf]` is a per-stage selector loaded from the
stage record at `&DAT_044e3798 + DAT_0438b1e0 * 0x2dfc8 + 0x2cdfc`
(field offset within the per-stage record).  HOUSE-class stages
have this selector in `[0..4]` (5 sub-types: 0/1/2/3/4); DUNGEON-
class stages have it < 0 or > 4.

### Reachability question — and its answer

`FUN_00436f97` is reachable from two engine call sites (per the
postload-init survey):

1. `FUN_0049e163` — state-8 (dungeon combat) → INGAME transition
2. `FUN_0048526d` — "enter state-1" wrapper, called from
   `FUN_00462403:249`, `FUN_00442cef:335`, `FUN_0048670f:214/218`

**Initial title → HOUSE goes through `FUN_004547ab` case-1 →
`FUN_00474a9a` only.  FUN_00436f97 is NOT called on initial HOUSE
entry from title.**  Sub-scene → HOUSE re-entry (returning from
dungeon, ESC menu, day-end transition) DOES go through
FUN_0048526d → FUN_00436f97.

Confirmed re-runnable:
```
nix develop --command grep -nE 'FUN_0048526d\(\)' docs/decompiled/all.c
```
Three production call sites (L40767, L60379, L86747/L86751) — all
inside scene-transition / post-day-end dispatch paths.  Initial
boot from title-screen IS NOT one of them.

**Implication for production**: porting Cf.* alone is not enough
to make HOUSE furniture appear on a fresh boot.  Cf.* needs a
wiring stand-in (same pattern as `scene1_postload_pose_player()`
in `scene1_preload.c::scene1_preload_house`).  Wiring decision is
deferred to the production-port chip.

### Writer-chunk structure (decomp L34772-L34871 / asm 0x4378d6-0x437b7c)

Top-down outline.  Decomp line refs match `docs/decompiled/all.c`;
asm line refs match `vendor/unpacked/recettear.unpacked.exe`.

1. **Camera-yaw priming** (L34773-34774 / asm 0x4378d6-0x4378ea):
   `_DAT_073de39c = π`; `_DAT_056db060 = π`.  Both writes share
   the literal at .rdata 0x51943c = 0x40490fdb.

2. **Count dispatch** (L34775-34790 / asm 0x4378f0-0x437946):
   4-way switch on iVar6 selecting `(phase1_count, phase2_count)`:
   - iVar6 == 0: (2, esi)  — esi = iVar8 ∈ {0, 1} from outer state
   - iVar6 == 1: (2, 4)
   - iVar6 == 2: (esi, 6)
   - iVar6 ∈ {3, 4}: (5, 10)

   The `esi` value (= decomp `iVar8`) traces back to outer state
   from earlier in the function.  Setting either count to `esi`
   produces 0 in the default boot path (iVar8 stays 0 unless a
   ported caller sets it).  In practice **phase 2 count of 10 is
   the maximum** (iVar6==3/4 case; the 14-iter loop below
   populates exactly 10 entries).

3. **Scalar field setup** (L34791-34822 / asm 0x437946-0x437a47):
   Fan-out writes of mesh_type / rot_y / pos_x / pos_y for slots
   {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19}.  Layout per
   §Layout corrections below.  Slots 10-15 are not written here.

4. **Per-stage 10-iter position loop** (L34823-L34840 / asm
   0x437a4b-0x437ac5):

   ```
   eax  = &DAT_0438c10c             ; = pos_z[0]
   edx  = stage_record + 0x2ce14    ; = local_c + 0x2ce14
   esi  = 0                          ; loop counter
   do {
       /* edi = stage_record[i].x_int */
       edi = *(int*)(edx - 4)
       /* per-(scene_type, slot) world-anchor offset from .rdata */
       idx = i + DAT_068dd3fc[stage*0x6cf] * 10
       edi -= *(int*)(0x5c5120 + idx*8)
       /* pos_x[i] = 2.0f * (float)edi */
       float v = (float)(int)edi
       *(float*)(eax - 0xa0) = v + v
       /* edi = stage_record[i].z_int */
       edi = *(int*)edx
       edi -= *(int*)(0x5c5124 + idx*8)
       *(float*)eax = (float)edi + (float)edi   ; pos_z[i] = 2.0f * z
       *(float*)(eax - 0x50) = 0.0f             ; pos_y[i] = 0
       esi++
       edx += 8
       eax += 4
   } while (eax != &DAT_0438c134);
   ```

   Re-runnable verification of the .rdata anchor table at 0x5c5120
   (5 scene_types × 10 entries × 8 B = 400 B):
   ```
   nix develop --command python3 tools/analyze/pe.py bytes 0x5c5120 400
   ```

   The stage record at `local_c + 0x2ce14` is the per-stage
   furniture-position array (10 entries × 8 B int pairs).  Source
   data is loaded from a stage file we haven't traced; for HOUSE-
   class stages the values determine where each shop_table mesh
   lands in world space.

5. **Trailing BSS writes** (L34841-34857 / asm 0x437ac7-0x437b65):
   ~14 zero-resets at `DAT_0438c0bc..DAT_0438c0e0` (pos_y slots
   not handled by the loop) + 7 specific writes at `DAT_0438cbe8..
   DAT_0438cbfc` (camera/state metadata at addresses outside our
   port's tracked walker fields).  Last two writes (L34856-34857):
   `DAT_0438cbf4 = pos_x[17] - 1.0` and `DAT_0438cbf8 =
   pos_y[17] - 1.0`.

6. **Per-stage camera yaw branch** (L34858-34870 / asm
   0x437b67-0x437b76):
   ```c
   if (stage_record[+0x2cdf4] == 0) {
       _DAT_056db05c = π/2;             // 0x3fc90fdb
   } else if (stage_record[+0x2cde0] == 0) {
       _DAT_056db05c = -π/2;            // 0xbfc90fdb
       DAT_056dab00 = 2;
       DAT_056dab58 = 2;
   } else {
       _DAT_056db05c = 0;
       DAT_056dab00 = 4;
       DAT_056dab58 = 4;
   }
   FUN_004851e2();
   ```

7. **Post-chunk** (L34871+ / asm 0x437b7f+): single yaw_alt copy
   (L34873 `_DAT_0438b4ac = _DAT_056db05c` — already noted as
   "out of MVP scope" in Cf.1's postload doc) + the 200-iter
   ambient spawn loop (L34874-34883 — already ported as Cf.1).

### Layout corrections

The existing `scene1_walker_pass_init.h` has the per-mesh field
arrays **mislabeled** relative to engine semantics.  Engine
`D3DXMatrixTranslation(out, x, y, z)` is called at asm
0x457e48-0x457e64 with args pushed right-to-left (stdcall):

```
push [esi + 0xf0]    ; z
push [esi + 0xa0]    ; y
push [esi + 0x50]    ; x
push edi             ; out
call MatrixTranslation
```

Where `esi = &DAT_0438c01c` (rot_y base).  So the engine layout
is:

| Engine semantic | Address           | Offset from rot_y base | Current header label |
|---|---|---|---|
| rot_y[]   | 0x438c01c..0x438c068 | +0x00 | `pos_y` (WRONG: this is rot_y) |
| pos_x[]   | 0x438c06c..0x438c0b8 | +0x50 | `pos_x` (WRONG label, address now mismatches) |
| pos_y[]   | 0x438c0bc..0x438c108 | +0xa0 | `pos_y` (header says pos_x here) |
| pos_z[]   | 0x438c10c..0x438c158 | +0xf0 | `pos_z` (correct) |

The current `scene1_walker_pass_init.c::scene1_walker_phase2_compute`
reads:
```c
mat4_translation(world,
                 g_scene1_walker_phase2_pos_x[i],  // header says addr 0x438c0bc — engine's Y
                 g_scene1_walker_phase2_pos_y[i],  // header says addr 0x438c06c — engine's X
                 g_scene1_walker_phase2_pos_z[i]);
```

→ The matrix builder is feeding (engine Y, engine X, engine Z) as
(x, y, z) of `mat4_translation`.  This is a transposition bug.
Today it has no visible effect because count==0; once Cf.* lands
the bug becomes load-bearing.

**Required correction** (load-bearing for Cf.*):

| Header symbol (rename) | Address           |
|---|---|
| `g_scene1_walker_phase2_rot_y[]`  | 0x438c01c |
| `g_scene1_walker_phase2_pos_x[]`  | 0x438c06c |
| `g_scene1_walker_phase2_pos_y[]`  | 0x438c0bc |
| `g_scene1_walker_phase2_pos_z[]`  | 0x438c10c |

The `mat4_translation` call in `scene1_walker_phase2_compute` is
unchanged after the rename — the addresses now match the engine's
push order.

### Per-mesh scalar writes — verified asm-decoded values

For slots 0..9 (the meshes the 10-iter loop populates):

| Slot | mesh_type | rot_y | Notes |
|---|---|---|---|
| 0 | esi (= iVar8) | 0 (BSS default) | mesh_type=esi at asm 0x43795a |
| 1 | 4 | 0 | mesh_type=eax (popped 4) at asm 0x43799e; rot_y=0 at 0x437a29 (fldz; fstp 0x438c020) |
| 2 | 4 | π/2 | mesh_type at 0x4379af; rot_y at 0x437a31 (fld 0x519434=π/2) |
| 3 | 4 | -π/2 | mesh_type at 0x4379b4; rot_y at 0x437a3d (fld 0x519a18=-π/2) |
| 4 | esi | 0 (BSS) | mesh_type at 0x43796c |
| 5 | 4 | 0 (BSS) | mesh_type at 0x4379c5 |
| 6 | esi | 0 (BSS) | mesh_type at 0x437972 |
| 7 | esi | 0 (BSS) | mesh_type at 0x437984 |
| 8 | esi | 0 (BSS) | mesh_type at 0x43798a |
| 9 | esi | 0 (BSS) | mesh_type at 0x437998 |

Slots 0..9 pos_x / pos_y / pos_z come from the 10-iter loop:
- pos_x[i] = 2.0 × (stage_record[i].x - rdata_anchor[scene_type][i].x)
- pos_y[i] = 0.0
- pos_z[i] = 2.0 × (stage_record[i].z - rdata_anchor[scene_type][i].z)

Slots 16..19 are scratch slots (NOT walked — phase2 count ≤ 10):

| Slot | rot_y | pos_x | pos_y | pos_z | Notes |
|---|---|---|---|---|---|
| 16 | -2.0 (asm 0x437946 fld 0x519908) | 0 (asm 0x437957 fldz; fstp 0x438c0ac) | -1.0 (asm 0x437966 fld 0x5196b8) | 0 (BSS) | |
| 17 | 13.0 (asm 0x43797e fld 0x519b40) | 0 (asm 0x4379a3 fldz; fstp 0x438c0b0) | -1.0 (asm 0x4379a9 fld 0x5196b8) | 0 (BSS) | |
| 18 | -2.5 (asm 0x4379bf fld 0x519bc8) | 0 (asm 0x4379e0 fldz; fstp 0x438c0b4) | 8.0 (asm 0x4379e6 fld 0x519378) | 0 (BSS) | |
| 19 | 13.0 (asm 0x4379fe fld 0x519b40) | 0 (asm 0x437a15 fldz; fstp 0x438c0b8) | 8.0 (asm 0x437a1d fld 0x519378) | 0 (BSS) | |

These look like dimensions/anchors used by other code paths (HUD
overlay setup or camera bounding), NOT actual furniture meshes.
The "rot_y" values for 16-19 (-2.0, 13.0, -2.5, 13.0) are clearly
not rotation radians.

`mesh_type[10..15]` and `mesh_type[16..19]` are not touched by
this writer chunk — they stay at their prior BSS-zero or
previously-set values.

### Palette / mesh-slot extras (asm 0x4379d4-0x4379f2)

The writer also fan-outs four palette-index writes at
`DAT_0438bfbc / _0438bfc0 / _0438bfc4 / _0438bfc8` (each = uVar13
= edi = 1 in production).  These are NOT phase-2 per-mesh fields
— they're texture-palette IDs read by the PII.3b draw loop's
classifier (`scene1_walker_classify_slot`).  Already exposed via
the engine BSS state in our port.

### Cf.* port scope decision

Two viable scopes:

1. **Cf.minimal** — port only the count + scalar writes + 10-iter
   position loop.  Skip trailing BSS clears (slots 10-15 stay
   BSS-zero, irrelevant since count ≤ 10) and the camera-yaw
   branch (yaw can stay BSS for HOUSE-default).  Scope: ~150 LoC,
   ~10 tests.  Output: phase 2 arrays populated correctly for
   slots 0..9 from per-stage data.

2. **Cf.full** — Cf.minimal + scratch slot 16-19 writes + camera-
   yaw branch + DAT_0438cbe8..cbfc BSS housekeeping.  Scope:
   ~250 LoC, ~15 tests.  Output: engine-faithful replication of
   the full writer chunk including dormant scratch fields.

**Recommendation: Cf.minimal** — the scratch slots and camera-yaw
writes have no consumer in the current walker port (PII.3b reads
only slots [0, count) and ignores rot_y[16..19]).  Camera-yaw is
already handled separately by scene1_camera.c.  Cf.minimal
unlocks visible HOUSE furniture; Cf.full can land later if a
sibling consumer ports.

### Wiring stand-in

The chip needs a wiring decision since FUN_00436f97 doesn't fire
on initial HOUSE entry.  Options:

- **A (recommended)**: call from `scene1_preload_house()` after
  `scene1_postload_init_stage_defaults()`.  Same pattern as the
  existing `scene1_postload_pose_player()` /
  `scene1_postload_ambient_spawn()` stand-in wiring.
- **B**: gate on a CLI flag like `--force-walker-phase2` (test
  scaffold; doesn't run on default boot).

Option A produces visible HOUSE furniture pixels on default boot
(the goal of the Cf.* chip ladder).  Document as "stand-in until
FUN_0048526d / FUN_0049e163 / sub-scene transitions port".

### Required data inputs the writer chunk reads

| Input | Address / accessor | Status today |
|---|---|---|
| `DAT_068dd3fc[stage*0x6cf]` (scene_type 0..4) | per-stage field at +0x2cdfc inside stage record | UNPORTED — stage record at `&DAT_044e3798 + stage*0x2dfc8` not modelled |
| `DAT_005c5120/24` (.rdata 5×10 anchor pairs) | static .rdata, 400 B | portable — read at port-time and embed as static array |
| `stage_record + 0x2ce14` (per-stage furniture position pairs, 10×8 B) | per-stage scratch | UNPORTED |
| `stage_record + 0x2cdf4 / +0x2cde0` (camera-yaw branch gates) | per-stage scratch | UNPORTED |
| `iVar6 = stage_record + 0x2cdfc` (scene_type 0..4 selector) | per-stage scratch | UNPORTED — same field as DAT_068dd3fc[..] |

The unported stage record means Cf.minimal needs stand-in
accessors for:
- scene_type (0..4)
- per-stage furniture positions (10× int x, int z pairs)
- camera-yaw gate values (2 ints)

Best modelled as host-installable hooks (default: scene_type=0,
positions all zero, camera-yaw gates both zero) so Cf.* can land
without porting the full stage record.  Mirrors C8j-tick.0's
stand-in pattern for similar per-stage data.

### Pending human checks for Cf.*

The port will introduce candidates for the standard PHC queue:

- **scene_type default for HOUSE on first boot** — Frida read of
  `*(int*)(&DAT_044e3798 + stage*0x2dfc8 + 0x2cdfc)` at the moment
  PII.3b first fires for HOUSE.  Our stand-in default 0
  corresponds to iVar6==0 case (phase2_count = esi = 0 → walker
  short-circuits).  Default 3 or 4 would give count = 10.
- **Per-stage furniture position table** at offset +0x2ce14.
  Frida dump after stage init for HOUSE; verify our stand-in's
  10 (int x, int z) pairs match.
- **.rdata 0x5c5120 anchor table** — already verified statically
  via `tools/analyze/pe.py bytes 0x5c5120 400` — re-runnable but
  no Frida read needed.

### Re-runnable verification

```
nix develop --command i686-w64-mingw32-objdump -d -M intel \
    --no-show-raw-insn vendor/unpacked/recettear.unpacked.exe \
    --start-address=0x4378e0 --stop-address=0x437b7c
```

Expect: count dispatch at 0x4378f0..0x437946 (4 cases); scalar
field setup at 0x437946..0x437a47; 10-iter loop body at
0x437a4b..0x437ac5; trailing BSS writes at 0x437ac7..0x437b65;
camera-yaw branch at 0x437b67..0x437b76.  Constants @ .rdata:
0x519908=-2.0, 0x5196b8=-1.0, 0x519bc8=-2.5, 0x519378=8.0,
0x519b40=13.0, 0x519434=π/2, 0x519a18=-π/2, 0x51943c=π,
0x519364=1.0.
