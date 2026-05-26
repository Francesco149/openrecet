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
