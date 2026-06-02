# Findings index

Table of contents for the durable reverse-engineering writeups in `docs/findings/`.

## How the docs layer

- **`memory/MEMORY.md`** (auto-memory) — *pointers*. Short index entries that
  link out to the durable docs; survives across sessions but holds no detail.
- **`docs/findings/*.md`** (this dir) — *durable RE*. The long-lived
  per-subsystem / per-`FUN_`-cluster writeups: survey, call graph, offsets,
  corrections. Cite these, don't re-derive them.
- **`docs/PROGRESS.md`** — *narrative*. Chronological, dated log of what landed
  when and why; the running story.
- **`docs/STATUS.md`** — *headline*. The current one-screen "where are we"
  snapshot.

Freshness = last git commit that touched the file (`git log -1 --format=%cs`).
A survey-only doc may be stale-by-date yet still current if nobody has revisited
that subsystem.

## Boot / init

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [winmain-and-bootstrap.md](winmain-and-bootstrap.md) | WinMain (`FUN_0047bfb3`) + engine bootstrap / subsystem init chain; PE entry, WndProc, window class. | 2026-05-22 |
| [imports-and-layout.md](imports-and-layout.md) | `recettear.unpacked.exe` static DLL imports + on-disk asset layout. | 2026-05-19 |
| [title-fade-out.md](title-fade-out.md) | Title → NEW GAME black fade-out mechanism (supersedes an earlier wrong PROGRESS entry). | 2026-05-22 |
| [title-settings-submenu.md](title-settings-submenu.md) | Title "Options" submenu (`DAT_09643524==2`) inside `FUN_0049a59e`; volume-slider state machine. | 2026-05-21 |

## Scene-1 render (the Mt. Everest ladder)

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [scene1-render.md](scene1-render.md) | C7+ master ladder doc — scene-1 render path end-to-end; chip plan. | 2026-05-23 |
| [scene1-postload-init.md](scene1-postload-init.md) | `FUN_00436f97` (Cf.0) — INGAME state-entry init; BSS reset + spawn + 200-iter ambient loop. | 2026-05-23 |
| [scene1-camera-helpers.md](scene1-camera-helpers.md) | `FUN_00441c3e` + `FUN_004424e7` + `FUN_0040120c` (Cc) — camera pose / view-matrix builder; eye-vs-lookat erratum. | 2026-05-23 |
| [scene1-spawn.md](scene1-spawn.md) | `FUN_00447f4f` (C8i) — table-A particle spawn API (~134 per-type handlers, landed). | 2026-05-23 |
| [scene1-particles-tick.md](scene1-particles-tick.md) | `FUN_0040fb3a` (C8h) — table-A per-tick particle integrator. | 2026-05-23 |
| [scene1-per-frame-open.md](scene1-per-frame-open.md) | `FUN_00414929` (PFO) — per-frame open; table-A/B tick bodies + allocators + template tables. | 2026-05-25 |
| [scene1-overlay-dispatcher.md](scene1-overlay-dispatcher.md) | `FUN_00414ee2` (O.1) — 2D screen-space overlay particle dispatcher (sparkle/smoke/hud). | 2026-05-24 |
| [scene1-record-populators.md](scene1-record-populators.md) | Table-B / table-C render-record populators survey; base addrs + sentinels + count globals. | 2026-05-24 |
| [scene1-table-b-allocators.md](scene1-table-b-allocators.md) | `FUN_0044376a` + `FUN_00445a8c` (C8j) — table-B initial-state allocators. | 2026-05-24 |
| [scene1-records-b-tick.md](scene1-records-b-tick.md) | `FUN_0043ae20` — 25.7 KB table-B per-frame integrator (consumer of the C8j allocators). | 2026-05-25 |
| [scene1-records-b-state-machine.md](scene1-records-b-state-machine.md) | `FUN_0043865e` — per-record combat state machine (NPC scan, attack input, damage, knockback, hit FX). | 2026-05-25 |
| [scene1-walker.md](scene1-walker.md) | `FUN_0040a765` (C7i) — scene-1 HUD walker survey; corrects the "3D mesh walker" mislabel + the Pass-7 "chr render" mislabel (it's shop menus). | 2026-05-23 |
| [scene1-chr-walker.md](scene1-chr-walker.md) | `FUN_004176ff` (30 KB, `scene1_walk_chr_TODO`) — character/entity 3D record walker. **⚠️ premise corrected by Cchr.0** — it does NOT draw the player; see scene1-char-sprite-trace.md. | 2026-05-29 |
| [scene1-char-sprite-trace.md](scene1-char-sprite-trace.md) | Cchr.0 + **Cchr.1 (RESOLVED)** retail Frida traces. Cchr.0: records_b + people empty, records_a only particles. Cchr.1 (`--quad-hist` world-transform trace): player/Tear/object sprites = world billboards from **`FUN_0045a56f`** (sprite-sheet→multi-quad→DrawPrimitiveUP), walker **`FUN_00456f56`/`FUN_0045672a`** reading the **`DAT_056da1b8` actor table** (player pos = `g_player_pos`), dispatched from `scene1_render_meshes`. Names the Cchr.2 port path. | 2026-05-29 |
| [scene1-char-sprite-render.md](scene1-char-sprite-render.md) | **Cchr.2** port dependency map + chip ladder for the `FUN_0045a56f` character sprite subsystem. Descriptor block layout (`.idx` grammar resolved), actor sprite-state struct, the leaf frame-LUT stride open question, two MVP strategies. **Cchr.2a LANDED**: `chr_sprite_meta.{c,h}` (formdata + `.idx` descriptor loaders). | 2026-05-29 |
| [scene1-bg-npc.md](scene1-bg-npc.md) | **Background-window NPCs** (`scene1_bg_npc`, formerly misnamed "ambient motes") — 6 townsfolk drifting past the shop's back window. `FUN_0046f2a3` sim + `FUN_0046f648` dark contact-shadow + **`FUN_0046f737` bright character sprite (LANDED 2026-06-02, un-stubbed)**. type→sheet `DAT_005c7ce0[type*2]` → chr{10,35..39}. User-verified rendering. | 2026-06-02 |
| [conversation-pose-driver.md](conversation-pose-driver.md) | **`FUN_0048407f` conversation-pose branch + the talk-event flag `DAT_0450f470`** — RE'd. During iv1_2 (any face-to-face talk) the HOUSE freeroam actors strike conversation poses: Recette anim **6** (「ティアの話を聞くよ」, frames 38/39 = look-up + **blink**), Tear anim **4** (「ルセットと会話」), facing each other. Port spec for the `intro-iv2-gap` gap (`opening-prologue.md` #4). | 2026-06-02 |
| [scene1-walker-pass-init.md](scene1-walker-pass-init.md) | `FUN_00457714` — per-NPC mesh walker = the HOUSE shop_table furniture renderer (relabel of pass-init stub). | 2026-05-26 |
| [scene1-house-render-gaps.md](scene1-house-render-gaps.md) | HOUSE render diffs vs retail post-PII.3c — floor/wall/rug textures (fixed) + god-ray/blinds lighting gaps (deferred to a scene-1 lighting chip). | 2026-05-29 |
| [scene1-wide-followup.md](scene1-wide-followup.md) | `FUN_004161c7` — wide-frustum followup draw (z_far=2000) survey. | 2026-05-23 |
| [scene1-leaf-chain.md](scene1-leaf-chain.md) | Mesh-emit leaf chain (5 fns in `FUN_00459dfd`); flat-`mesh_t` adapter notes. | 2026-05-23 |
| [scene1-people-table.md](scene1-people-table.md) | `DAT_0076bd54` — 128-entry × 2980 B in-shop "people"/NPC table layout. | 2026-05-23 |
| [sim-step-a-dispatch.md](sim-step-a-dispatch.md) | `FUN_004536cb` / `sim_step_a` — full survey + chip ladder for the INGAME sim caller. | 2026-05-23 |

## Input / UI

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [esc-skip-event.md](esc-skip-event.md) | Context-sensitive ESC dispatch (WndProc `FUN_0047b2e7`) + the skip-event yes/no prompt (`FUN_00453384`/`454191`); pause-menu gate. Phase A (dispatch) landed. | 2026-06-02 |

## Audio

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [audio-backend.md](audio-backend.md) | DirectMusic 8 backend — init, BGM track-swap, sin-curve volume fade, per-tick fade animation. | 2026-05-21 |

## Loaders / formats

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [tables-loader.md](tables-loader.md) | `FUN_00475270` (`tables_load_all`) — outer "index file" gameplay-table dispatcher. | 2026-05-20 |
| [item-table.md](item-table.md) | `data/item.txt` parser — the ~600-record master item table; gates the item-name resolver. | 2026-05-20 |
| [texture-loader.md](texture-loader.md) | `FUN_0047193c` — texture loader; disk/storage lookup + BMP/TGA dispatch. | 2026-05-20 |
| [mesh-loader.md](mesh-loader.md) | `xfile/*.x` mesh loader (`FUN_00472836` chain) — parser + build + D3D8 upload + orchestrator. | 2026-05-23 |

## Diff / trace harness

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [pure-function-diff.md](pure-function-diff.md) | Phase D.1 — pure-function differential testing port-vs-retail via Frida + ctypes. | 2026-05-26 |
| [d3d-trace.md](d3d-trace.md) | Phase D.4 — Frida-side `IDirect3DDevice8` vtable state-trace → JSONL. | 2026-05-26 |
| [render-diff.md](render-diff.md) | Phase D.6 — render-diff orchestrator; surfaces per-frame state-trace divergences. | 2026-05-26 |

## Misc / reference

| Doc | Covers | Last touched |
|-----|--------|--------------|
| [engine-quirks.md](engine-quirks.md) | Curated tour of weird/charming engine behaviours found while RE-ing "Azumanga". | 2026-05-25 |
| [cross-references.md](cross-references.md) | The three prior Recettear RE projects cloned as siblings; what each contributes (archive format etc.). | 2026-05-19 |
