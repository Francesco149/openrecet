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

## Next chip (Cchr.1, proposed)

Find the player/companion **sprite renderer**, not another record walker:

1. Frida-hook the 2D quad emit (`render_quad_add` @ `0x404efc`, already
   probed) during a free-roam HOUSE frame and bucket calls by source
   texture / caller return-VA — the character billboards are the quads
   whose screen position tracks `g_player_pos` projected to screen.
2. From the caller VA, name the per-frame player-sprite function and the
   struct it reads (player animation frame, facing, world pos).  That, not
   FUN_0043ae20 / FUN_004176ff, is the minimal path to one visible
   character in HOUSE.
3. Tear (companion) is a second sprite on the same path; customers (when
   present) likely populate the people table — re-run this dump during an
   active customer event to confirm the people table fills then (it would
   validate the people-table→FUN_004176ff render hypothesis for *customer*
   sprites specifically, separate from the player path).

## Repro

```
nix develop --command python3 tools/frida_capture.py \
  --run-dir runs/cchr0-people \
  --max-frames 1000000 --duration-ms 90000 \
  --hide-window --turbo --silent-audio --auto-z-spam \
  --dump-records-b --dump-records-b-capture \
  --dump-records-b-offsets 0,4000,9000,16000 \
  --dump-records-b-heartbeat 4096
```

Note: `--auto-z-spam` (button A) does **not** skip the opening tutorial
event ("ESC Key: Event Skip" needs the keyboard ESC, polled outside the
game button mask) — but it does reach free-roam on its own a few thousand
frames later, which the offset window above lands in.

## Cross-refs

* `tools/frida/openrecet-agent.js` — `dump_records_b*` mode (records A/B +
  people-table reader, anchor-on-populate, heartbeat, screenshot).
* `tools/frida_capture.py` — `--dump-records-b*` driver flags.
* `docs/findings/scene1-chr-walker.md` — the C7m survey this corrects.
* `docs/findings/scene1-people-table.md` — people-table layout (L116
  FUN_004176ff-renders-it claim, now scoped to *customer* sprites only).
* `docs/findings/scene1-record-populators.md` — tables A/B/C populators.
</content>
</invoke>
