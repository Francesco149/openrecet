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
