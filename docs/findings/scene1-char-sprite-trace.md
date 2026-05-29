# Cchr.0 — retail HOUSE character-render trace (FUN_004176ff is NOT the player renderer)

**Status (2026-05-29):** Frida ground-truth trace.  Decisive result that
**corrects the C7m premise** in [`scene1-chr-walker.md`](scene1-chr-walker.md)
and [`scene1-record-populators.md`](scene1-record-populators.md): on a fresh
new-game HOUSE the visible character sprites (Recette + Tear) are drawn by a
**dedicated sprite path**, not by the `FUN_004176ff` record walker and not
out of any of the three tables that walker reads.

## What was traced

New tooling (this chip): `tools/frida_capture.py --dump-records-b` +
the `dump_records_b*` mode in `tools/frida/openrecet-agent.js`.  Drives
retail to HOUSE unattended (`--auto-z-spam --turbo --silent-audio
--hide-window`), anchors a dump window on the first frame either record
table populates (`count_a>0 || count_b>0`), then at configurable
frame-offsets dumps — to `<run_dir>/records_b_dump.jsonl` plus a
backbuffer screenshot per dump frame:

* **records_a** (`DAT_069b2f80`, 4096 × 0x25 dw) — live slots (TYPE != -1).
* **records_b** (`DAT_069324b0`, 512 × 0x49 dw) — live slots (TYPE != 0).
* **people table** (`DAT_0076bd54`, 128 × 0x2e9 dw) — `alive != 0` entries.
* the three per-pass counts (`DAT_0076b960/64/68`), `g_player_pos`
  (`DAT_056da1d8`), and a per-frame `DrawIndexedPrimitive` tally heartbeat.

Three captures (`runs/cchr0-*`), each confirmed by screenshot to be
free-roam HOUSE (the "Button 4: Change Camera" HUD, Recette + Tear
visible bottom-centre).

## Result — all three tables are character-empty in free-roam HOUSE

At free-roam frames (e.g. manual frame 19207, Recette + Tear on screen):

| table | live count | what's in it |
|-------|-----------:|--------------|
| records_a (`count_a`) | **6** | all TYPE `0x1f`, scale `0.1`, staggered ages 1..31 recycling every ~32 frames, clustered at a fixed point (~1.2, 4.0, 9.4) — a steady **particle emitter** (the sparkle by Tear), not a character |
| records_b (`count_b`) | **0** | empty the entire run (0..110k frames) |
| people (`alive`)      | **0** | empty at every dump frame |

Per-frame `DrawIndexedPrimitive` count holds at **~82** in free-roam HOUSE
(not the ~1878 an earlier note guessed for "walker-active") — that 82 is
the static room + furniture meshes.  The character sprites add nothing to
the indexed-primitive count → they are **2D billboard quads**, drawn
through the quad/sprite path, not as 3D `DrawSubset` meshes.

## Why this corrects C7m

* `scene1-chr-walker.md` concluded "characters in the shop are gated
  behind FUN_0043ae20 (25.7 KB integrator) + FUN_004176ff (30 KB walker)"
  because it scoped FUN_004176ff to its records_a/records_b reads and saw
  those tables BSS-zero on HOUSE entry.  **Porting that pair renders zero
  characters** — confirmed: retail itself keeps records_b empty in
  free-roam HOUSE, so the walker's records_b passes draw nothing there.
* `scene1-people-table.md` L116 tags FUN_004176ff as "renders the [people]
  table" — but retail's people table is **also empty** (alive=0) in
  free-roam HOUSE, so that path draws no characters here either.
* Net: **none of the three tables FUN_004176ff walks holds the player or
  Tear** in free-roam HOUSE.  The visible sprites come from a separate,
  un-surveyed **player / companion sprite subsystem** (2D billboards keyed
  off `g_player_pos` + an animation state), which is what actually needs
  porting to "see a character in HOUSE".

The earlier "characters live in records_b / people table" assumption was
never ground-truthed against retail; this trace does that and falsifies it.

# Cchr.1 — RESULT: the player/companion sprite path is the actor table + FUN_0045a56f

**Status (2026-05-29):** ground-truthed via the `--quad-hist` trace
(below).  The player (Recette), companion (Tear) and shop-object sprites
are NOT records-table draws and NOT 2D screen-space quads — they are
**world-space billboards** drawn by **`FUN_0045a56f`** (a sprite-sheet
cell → multi-quad mesh → `DrawPrimitiveUP`), driven by the scene-1 **actor
render walkers `FUN_00456f56` / `FUN_0045672a`** (called from the
already-ported `scene1_render_meshes` = `FUN_00459dfd`), reading the
**actor table based at `DAT_056da1b8`** (stride `0x44` = 68 B; the player's
record is the one whose pos field IS the `g_player_pos` global,
`DAT_056da1d8 = DAT_056da1b8 + 0x20`).

## How it was traced (`--quad-hist`)

Extended `tools/frida_capture.py --dump-records-b` with `--quad-hist`
(agent: `installQuadHistHooks` + `quad_frame`/`quad_hist` messages).  On
each free-roam dump-offset frame it records every call to the 2D quad
emitter `FUN_00404efc` (caller-VA + dst rect + texture-dim block) **plus**
every `DrawPrimitive(UP)` / `SetTexture` / `SetTransform(WORLD)` — so a
sprite drawn as a world billboard (not a screen quad) still surfaces, with
its bound texture and its world-matrix translation.  The decisive step:
pair each `DrawPrimitiveUP` with the preceding `SetTransform(0x100)`
translation and match it to `g_player_pos`.

Run `runs/cchr1-xform`, free-roam frame 18018, `g_player_pos =
(-0.30, 0.00, 9.35)` (Recette + Tear visible centre-bottom):

| draw (caller VA → fn) | world translation | identity |
|---|---|---|
| `0x45aa31` → **FUN_0045a56f** (12-prim) | **(-0.30, 0.00, 9.35)** | **PLAYER sprite (Recette)** |
| `0x45aa31` → FUN_0045a56f (12/14-prim) | (0.60, 2.95, 9.35) | **companion sprite (Tear)** |
| `0x45aa31` → FUN_0045a56f | far (z ≈ -12..-14) | shop **object sprites** (shelf items) |
| `0x45ae4a` → FUN_0045aa36 | (-0.30, 0.12, 9.35) | player **shadow** (binds shade tex `DAT_073cc8f0`) |
| `0x46f722` → FUN_0046f648 | scattered, y ≈ 0.08 | object **shadow blobs** (dark `0xff202020` quads) |
| `0x41e165` → FUN_004176ff | (~1.1, 3.8, 9.4) | **ambient sparkle particles** (records_a type `0x1f`) |

The 6 `FUN_004176ff` billboards land exactly where Cchr.0 saw the records_a
`0x1f` cluster ("the sparkle by Tear", ~1.2/4.0/9.4) — re-confirming that
the 30 KB walker renders **particles**, not characters.

## What this means for the port

* The minimal "see a character in HOUSE" path is **NOT** `FUN_0043ae20`
  (25.7 KB integrator) + `FUN_004176ff` (30 KB walker).  It is the actor
  walker `FUN_00456f56`/`FUN_0045672a` + the sprite renderer `FUN_0045a56f`
  + the `DAT_056da1b8` actor table (which is already populated on HOUSE
  entry — `g_player_pos` is live).
* `FUN_00456f56` / `FUN_0045672a` are two of the **14 walker stubs** inside
  the ported `scene1_render_meshes` (`src/scene1_render.c`) — so wiring a
  real port of them is the next chip, not a from-scratch subsystem.
* `FUN_0045a56f` is a generic sprite-sheet renderer (many call sites
  repo-wide, incl. the title/menu cluster `FUN_0046f6xx`); the HOUSE actor
  path is the scene-1 caller subset.

## Next chip (Cchr.2, proposed)

Port the actor-render path to get Recette/Tear visible in HOUSE:

1. Map the `DAT_056da1b8` actor-table struct (stride `0x44`): `+0x14`
   alive/anim sentinel (`DAT_056da1cc`), `+0x1c` (`DAT_056da1d4`), `+0x20`
   pos = `g_player_pos`, `+0x38` second pos triple (`DAT_056da1f0`); the
   loop terminus is `&DAT_056dae14`.
2. Port `FUN_0045a56f` (1223 B sprite-sheet → multi-quad billboard) — the
   leaf renderer.  Validate its draw against the captured per-sprite
   geometry (local ±16 × 48-104, 12-16 prims, stride-24 FVF + the
   `&DAT_0438cdf8` billboard base matrix).
3. Port `FUN_00456f56` (+ `FUN_0045672a`) and wire them in place of the
   `scene1_render_meshes` walker stubs.  Shadows (`FUN_0045aa36` /
   `FUN_0046f648`) are a separate, lower-priority pass.

## Repro

```
# Cchr.0 (record tables):
nix develop --command python3 tools/frida_capture.py \
  --run-dir runs/cchr0-people \
  --max-frames 1000000 --duration-ms 90000 \
  --hide-window --turbo --silent-audio --auto-z-spam \
  --dump-records-b --dump-records-b-capture \
  --dump-records-b-offsets 0,4000,9000,16000 --dump-records-b-heartbeat 4096

# Cchr.1 (quad/draw caller + world-transform trace):
nix develop --command python3 tools/frida_capture.py \
  --run-dir runs/cchr1-xform \
  --max-frames 1000000 --duration-ms 120000 \
  --hide-window --turbo --silent-audio --auto-z-spam \
  --dump-records-b --dump-records-b-capture \
  --dump-records-b-offsets 16000,18000 --dump-records-b-heartbeat 8192 \
  --quad-hist
```

Note: `--auto-z-spam` does **not** walk the player (pos is static across
the trace), so the player-sprite bucket was identified by world-transform
match to `g_player_pos`, not by dst-rect spread.  It reaches free-roam on
its own a few thousand frames after the records anchor.

## Cross-refs

* `tools/frida/openrecet-agent.js` — `dump_records_b*` + `quad_hist` modes
  (`installQuadHistHooks`: `FUN_00404efc` + `DrawPrimitive(UP)`/`SetTexture`
  /`SetTransform` capture, gated to dump-offset frames).
* `tools/frida_capture.py` — `--quad-hist` driver flag + `quad_trace.jsonl`.
* `docs/findings/scene1-chr-walker.md` — the C7m survey this corrects
  (FUN_004176ff = particle/entity walker, not the character renderer).
* `docs/findings/scene1-people-table.md` — people-table layout (L116
  FUN_004176ff-renders-it claim, now scoped to *customer* sprites only).
* `docs/findings/scene1-record-populators.md` — tables A/B/C populators.
* `src/scene1_render.c` — `scene1_render_meshes` (FUN_00459dfd) with the
  14 walker stubs that `FUN_00456f56`/`FUN_0045672a` belong to.
