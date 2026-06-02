# HOUSE persistent top HUD — clock dial + Day badge + money

> 2026-06-02. The HOUSE/town free-roam screen has a persistent top-left HUD
> (`house-walk-tables` cap_05 shows it cleanly): a **time-of-day clock wheel**
> (colored sectors + a rotating hand, concentric ornate gold rings), a **"Day 1"
> badge** (dark-blue disc), an ornamental gold vine banner along the top, and
> the player's **money "1,000pix"** on the right of that banner. The port draws
> NONE of it — it's the big top-left blob in the cap_05 white-diff.

## Where it is NOT

Ruled out by reading the disasm:

- **`FUN_0040a765`** (the scene-1 2D HUD aggregator, the only HUD call in the
  HOUSE render path `FUN_004547ab` b1c0==1) — its passes are menu/summary panels
  (news summary, advance-order summary, level abilities, the *transient*
  "Survival Mode / Day N" day-start banner at center 320, item price markers).
  Full SetTexture + text-draw inventory taken: no clock-wheel texture, no comma
  money, no persistent Day badge.
- **`FUN_00409925` / `FUN_0046602e`** (Pass-4 HOUSE sub-walker + inner) — gated
  on shop/stocking sub-states (`DAT_0438cc08`, `DAT_0438b7b0`), dormant in
  free-roam. These are the shelf-management / price-marker UI.
- **`FUN_0040c962`** (free-roam b1c8==0 draw) — the held-item indicator.
- **`FUN_0049c439` / `FUN_0048ef00`** — the game-over score panel + the
  end-of-day "Daily Profits" screen (both draw "Day %d" but are not persistent).
- The master loop `FUN_0047be92` has no render call besides `FUN_004547ab`.

## Where it almost certainly IS

**The overlay system** — `FUN_004547ab` → `FUN_00417504` (the overlay-layer
dispatcher, port C7h `scene1_overlay.c`) → `FUN_00414ee2(layer, 1)` for layers
0/1/2/3. The persistent HUD is drawn as **registered overlay quads/sprites** in
one of those layers, populated each frame by a HUD-update *registrar* (not yet
identified). The port's overlay DRAW is wired but the HUD elements are never
registered → nothing shows.

## Next step (recommended: live call graph — Frida is always available)

Per [[feedback_full_path_call_graph]] / [[feedback_probe_via_input_traces]],
don't keep guessing from static disasm. Drive retail to a HOUSE free-roam frame
(e.g. the `house-walk-tables` segtrace) and trace **who writes the overlay table
that `FUN_00414ee2` consumes**, or hook the `SetTexture`/draw calls and find the
one binding the clock-wheel texture at screen (0..50, 0..50). That pins the
registrar; port it + the clock/day/money draw. The money formatter inserts a
thousands comma ("1,000") — distinct from the shop "%d pix" (no comma) — so the
persistent-money draw is identifiable by its format string too.

Verification loop (autonomous): `tools/pixel_diff.py` on
`tests/scenarios/house-walk-tables` cap_05, crop 0,0,360,150 — tweak until the
top-left blob clears.

## Call-graph evidence (2026-06-02)

Retail's cap_05 render-call counts (`docs/findings/house-cap05-retail-callgraph.txt`,
extracted from the captured retail call_trace) show **`FUN_00414ee2` called 9×**
this frame — the overlay-layer draw fires once per registered HUD element. That
is where the clock/day/money land. The registrar that pushes those 9 records is
the port-side gap. (Port vs retail va-sets are NOT directly comparable — separate
binaries — but the retail-side list is ground truth for what to port.)
