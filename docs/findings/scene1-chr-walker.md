# Scene-1 character / entity render walker — FUN_004176ff survey

> **⚠️ CORRECTED 2026-05-29 by the Cchr.0 retail trace
> ([`scene1-char-sprite-trace.md`](scene1-char-sprite-trace.md)).** This
> survey's central premise — "the player/characters are records_b entries
> drawn by FUN_004176ff, gated behind the 25.7 KB integrator FUN_0043ae20"
> — is **falsified by ground truth**. In retail free-roam HOUSE, records_b
> is empty, records_a holds only an ambient particle emitter, AND the
> people table is empty, yet Recette + Tear are on screen. The character
> sprites are **2D billboards from a dedicated sprite path**, not from any
> table this walker reads (their draw adds nothing to the per-frame
> `DrawIndexedPrimitive` count). Route A/B below would render
> particles/entities, not the player. See the trace doc for the next chip
> (Cchr.1: find the 2D player-sprite renderer). The FUN_004176ff body
> survey below remains accurate; only its "this is how you get characters"
> conclusion is wrong.

**Status (2026-05-29):** Survey only — no code lands from this doc. Done
as the C7m re-scope after the `scene1-walker.md` Pass-7 correction
revealed that the 2D HUD aggregator does **not** render characters; the
Recette/Tear/NPC avatars come from `FUN_004176ff`, the
`scene1_walk_chr_TODO` stub in the 3D mesh-walker chain
(`src/scene1_render.c:854`, inside `scene1_render_meshes`).

## TL;DR verdict

**Porting FUN_004176ff today renders zero characters on a fresh
new-game HOUSE.** It is a data-driven render walker over the table-B
records (`g_scene1_records_b`), and that table is **BSS-zero on HOUSE
entry** because its populator — the 25.7 KB game-logic integrator
`FUN_0043ae20` — is unported/stubbed in the port. The walker is
data-starved, not the bottleneck.

So "characters standing in the shop" is gated behind the **two largest
functions in the scene** (FUN_0043ae20 25.7 KB integrator +
FUN_004176ff 30 KB walker), not a single chip. See "What it takes"
below for the realistic ladder + the smoke-flag validation path.

## What FUN_004176ff is

`FUN_004176ff` @ 0x4176ff (30,395 B, ~5,308 decompiled lines) is the
unified **3D per-record entity/particle/character render walker**. It
draws the live records of the scene's two record tables as 3D meshes /
billboards with per-record D3DX transform math:

* **records_a** (`g_scene1_records_a`, base DAT_069b2fb0, stride 0x25
  dw, 4096 slots) — count `DAT_0076b960`.
* **records_b** (`g_scene1_records_b`, base DAT_069324b0, stride 0x49
  dw = 0x124 B, 512 slots) — count `DAT_0076b964`. This is the primary
  table for NPC / entity / character avatars (each record's
  `+0x10`/`+0x14` are entity/NPC owner pointers; `+0x98` age, `+0x4c`
  scale, `+0x60` mesh handle, `+0x948` on the owner = a class/state
  field).

The `DAT_0076b95c` guards sprinkled through the body (`if (DAT_0076b95c
!= <texaddr>)`) are the texture-cache "currently-bound texture" state
short-circuits — the walker is broken into many **texture-batched
sub-passes**, each binding one atlas (DAT_073d8ed0, DAT_073aa178,
DAT_073cc8c0, …) then drawing every record that uses it.

### Structure (section map)

| Lines | What |
|------:|------|
| L244-248 | one-shot init: `if (DAT_0064e818==1){ DAT_0064e818=0; FUN_0040f892(); }` (DAT_0064e818 armed by an upstream event at all.c:40619). |
| L350-364 | two `FUN_00414ee2(...)` overlay-dispatcher calls (the 2D screen-space particle dispatcher, O.1). |
| L381-430 | pre-pass loop over a separate table `&DAT_0695f1e0 .. &DAT_069b31e0` (projectile-region records); per-record D3DX matrix + scene-tree dispatch. |
| L456-873 | **main records_b loop #1** — `for slot in 0..DAT_0076b964`: per-record scale (`*0.0018/0.0025/0.003` etc. off `+0x4c`), matrix build via `thunk_FUN_004a*`, owner-chain walks (`+0x18`, `+0x948` class fields), mesh emit. |
| L884-1419 | **records_b loop #2** — second batched pass, different scale factors (`*0.001/0.005/0.004/0.002`) and per-type branches; reads `+0x4c` (`DAT_06932510`/`0c` matrix cols). |
| L1420-2008 | records_a / records_b passes keyed on `DAT_0076b960` + texture guards DAT_073d8ed0 / DAT_073aa178. |
| L2009-… | further records_b passes (`DAT_0076b964`) with additive/alpha fade math (`*0.03/0.02/0.0002`), age-based alpha (`(age)*8+0xff`). |
| … | continues through ~5,308 lines of per-type billboard math. |

### Sub-call inventory

371 draw-related sub-calls. The hot ones (all D3DX / mesh-leaf, mostly
already have port analogs in `math3d` / `mesh_draw`):

| addr | n× | role |
|------|---:|------|
| FUN_004a2a03 | 168 | D3DXMatrixMultiply (matrix compose) |
| FUN_004a33d2 | 70 | D3DXMatrixRotation* |
| FUN_004a3462 | 64 | D3DXMatrixTranslation |
| FUN_004a3670 | 32 | D3DXMatrixRotationAxis / quat helper |
| FUN_0041edee | 27 | mesh-emit leaf (per-record DrawSubset path) |
| FUN_004a3537 | 24 | D3DXMatrixScaling |
| FUN_00503a44 / 994 | 16/9 | sinf / cosf (FPU) |
| FUN_00415e90/eb4/f2e | 6/6/7 | mesh state / draw helpers |
| FUN_0040f892 | 1 | one-shot init (L247) |

## Why it is dormant in HOUSE (data-liveness sweep, 2026-05-29)

Confirmed via a cross-reference sweep (cited to all.c / src):

1. **Count writer.** `DAT_0076b964` is (re)computed at the top of
   `FUN_00459dfd` (= `scene1_render_meshes`, all.c L54376-54385) by
   scanning table B for the highest non-zero slot. It is **0 unless
   table B has live records**.
2. **Table B is reset, never filled, on HOUSE entry.**
   `scene1_preload_house` → `scene1_records_reset(1)`
   (`src/scene1_records.c:24`) zeroes all 512 slots. No allocator runs
   on the new-game HOUSE path.
3. **The real populator is unported.** Engine fills table B inside the
   25.7 KB integrator `FUN_0043ae20` (its INGAME arm `FUN_00442cef`);
   the port wires only the thin transition wrapper `FUN_004427d3` and
   **stubs the INGAME arm** (`src/scene1_sim.c`,
   `docs/findings/scene1-record-populators.md` L96-108).
4. **The port's table-B allocators exist but are unwired.**
   `scene1_record_b_spawn_entity/npc` (FUN_0044376a / FUN_00445a8c,
   `src/scene1_records_b_spawn.c`) are fully ported but called **only**
   behind the `--force-b-entity-type` / `--force-b-npc-type` smoke
   flags (`src/scene1_postload.c:212-246`).
5. **Recette is not separately spawned.** The port seeds only
   `g_scene1_player_pos[3]` (`scene1_postload_pose_player`); no table-B
   player record is created on HOUSE entry. In retail the player record
   is allocated by `FUN_0043ae20`'s state machine (unported), so the
   port has no live character record of any kind in HOUSE.

> Net: `g_scene1_records_b_count == 0` on a fresh new-game HOUSE → every
> records_b loop in FUN_004176ff is skipped → no pixels.

## What it takes to see ONE character in HOUSE

Two genuine routes:

**Route A — the real climb (multi-session).** Port a HOUSE-minimal
subset of `FUN_0043ae20` (25.7 KB) that allocates the player (and shop
NPCs) into table B during INGAME init, and wire its INGAME arm
(`FUN_00442cef`) into the per-frame sim. Then port `FUN_004176ff`
incrementally (texture-batched sub-pass at a time). This is the two
largest functions in the scene; expect several sessions. Before
committing, **confirm via a Frida retail trace which records_b sub-pass
+ record actually draws the player avatar** (trace SetTexture/DrawSubset
in retail HOUSE, map the record's owner-class field) so the minimal
player-only path can be scoped rather than porting all ~5,308 lines.

**Route B — validation harness (cheap, now).** Use the existing
`--force-b-entity-type <N>` smoke flag to inject one static table-B
record. With a record present, a ported FUN_004176ff sub-pass can be
exercised end-to-end (bind → matrix → DrawSubset) and visually verified
A/B against retail **without** porting the 25.7 KB integrator. The
injected body sits static (no tick), but it validates the render path
incrementally as each sub-pass lands. This is the recommended way to
de-risk a Route-A port.

## Recommended next step

Do **not** start a blind 30 KB port. Either:

1. **Frida-trace retail HOUSE first** — capture which FUN_004176ff
   sub-pass draws the player + which table-B record fields drive it
   (owner-class at `+0x948`/`+0x18`, mesh handle, scale). That trace
   turns "port 5,308 lines" into "port the one player sub-pass +
   allocate one player record", and tells us whether the player is even
   a records_b entry or a special-cased draw. **This is the highest-
   leverage move and the natural C7m/Cchr.0 chip.**
2. Then scope a HOUSE-minimal `FUN_0043ae20` player-allocation subset
   (Route A) validated against the `--force-b-entity-type` harness
   (Route B).

## Cross-refs

* `docs/findings/scene1-walker.md` — Pass-7 correction that re-scoped
  here (FUN_0046b00a / FUN_00466b7b are shop menus, not chr render).
* `docs/findings/scene1-record-populators.md` — table-B populator
  (`FUN_0043ae20`) survey + the stubbed INGAME arm.
* `docs/findings/scene1-table-b-allocators.md` — the ported-but-unwired
  C8j allocators + the `--force-b-*` smoke flags.
* `docs/findings/scene1-records-b-tick.md` — the 25.7 KB integrator body.
* `src/scene1_render.c:854` — the `scene1_walk_chr_TODO()` stub site.
* `src/scene1_records.{c,h}` — table A/B storage + counter scan.
* `docs/decompiled/by-address/4176ff.c` — the full Ghidra output.
