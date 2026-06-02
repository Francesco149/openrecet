# HOUSE cap_05 — missing character billboard near the back window

> 2026-06-02. `house-walk-tables` cap_05 (retail frame 14892 / port frame 3986).
> The port is missing a **character billboard** drawn against the back window,
> screen-space **x≈372-398, y≈68-93** (just right of the top HUD banner, top-mid
> slightly left — exactly the user's "npc showing through the window" pointer).
> Retail draws a small figure there (green/cream outfit, light hair, apparent
> wings); the port draws nothing. Feed: "cap_05 missing NPC (back window)".

## What the three cap_05 character-region diffs are

Fresh clean diff (port 3986 vs retail 14892), big blobs:

| blob | bbox (screen) | what |
|---|---|---|
| 26708 px | x[3-404] y[13-159] | the **top HUD** (clock dial + Day badge + money) — see [[house-top-hud]] |
| 9080 px | x[14-296] y[679-738] | the **bottom HUD** item bar |
| 3512 px | x[485-574] y[357-431] | the **centre companion (Tear)** — present in BOTH, position-offset (the known "Tear pos CONFIRMED off") |
| 358 px | x[372-398] y[68-93] | **THIS — the missing back-window character** (port draws nothing) |

## Why it's a distinct, un-drawn actor (not Tear's offset, not Recette)

- **Recette (player)** renders **1:1** (confirmed-parity ledger) → no diff → she
  is drawn correctly somewhere and is invisible in the diff. So the back-window
  figure is not her being mis-drawn.
- **Tear (companion)** is the **centre** blob (silver hair + blue wing-glow,
  present in both port and retail, offset). She can't also be the back-window
  figure.
- ⇒ the back-window figure is a **third actor the port never spawns/draws** — a
  genuine background NPC (or a story actor) in the HOUSE intro free-roam.

## Dead ends ruled out

- **FUN_00431a80** (the roster loader memory flagged "deferred") is **DUNGEON
  only** — its sole caller early-returns in HOUSE (`scene1_preload.c:136`). Not
  the source of this HOUSE NPC.

## Next step (Frida actor-table dump — the prescribed method)

Per [[feedback_full_path_call_graph]] / [[feedback_trace_retail_ground_truth]],
dump retail's **actor/character table** at this frame (the DAT_056daae8
position-history ring + the actor roster the shop-walker FUN_004552d0 iterates)
and compare to the port's. That identifies the missing actor's index + type, and
who spawns it. Then port that spawn + wire it into the existing character-
billboard render (scene1_shop_walker / scene1_chr_sprite). Verify: the cap_05
x[372-398] y[68-93] blob clears.

Tooling: a retail `call_trace.jsonl` already exists for this run
(`runs/scenarios/house-walk-tables-both-20260602T042259Z/retail/`) — extend the
probe set to the actor-iterate functions, or add a Frida actor-ring dump.

## Call-graph evidence (2026-06-02)

Retail cap_05 calls the 3D character walkers `FUN_00459847` (4×), `FUN_004552d0`,
`FUN_00458bdf`, `FUN_00456f56`, `FUN_0045672a` (`docs/findings/house-cap05-retail-callgraph.txt`).
The port renders fewer characters (it draws Recette + the offset Tear). The
missing back-window actor is drawn by this walker family — pin the actor index
via an actor-ring dump, then ensure the port spawns + walks it.
