# HOUSE persistent top HUD — clock dial + Day badge + money

## ✅ SOLVED 2026-06-02 — it's `FUN_00406d50`, NOT the overlay system

The "Where it almost certainly IS" guess below (the overlay particle system /
`FUN_00414ee2` registrar) was **WRONG**. The persistent HUD is drawn by
**`FUN_00406d50`** (0x406d50, 1445 B), called *unconditionally* from
`FUN_0040a765` at decomp L6980 (so it runs every INGAME HUD frame; the
surrounding shop/summary passes are the dormant ones the static survey
mistook the whole aggregator for). Confirmed: `FUN_00406d50` fired 1× in the
cap_05 retail call graph, and the font text drawers (`FUN_0047d14c`/`ca05`)
did **not** — so "Day"/"1,000pix" are **sprite-glyph numbers**, not font text.

All draws use **`bmp/item_win.tga`** (DAT_073d8748, 1024×1024 =
`g_sysassets.item_win_tga`, already loaded):

- **Frame** (gold clock ring + banner + Day-badge disc): `FUN_00404efc` with
  src (480,0)-(768,128) → dst (0, `-128*letterbox`, 230.4, 102.4).
- **Clock hand**: `FUN_00406241(41.6, 57.6, angle, dst{-12.8,-43.2,12.8,8}, src_uv{0.4541,0.1260,0.4834,0.1865}, white)` — a **rotated** 4-vert
  triangle-strip (its own vbuf DAT_00605208, FVF 0x1c4, *normalized* UVs).
  `angle = π/2 − (DAT_0438b7d4·π/3)` where DAT_0438b7d4 is the time-of-day
  phase (advanced +0.005/frame in shop hours, 50646-50653).
- **Day number**: `FUN_00406a60(x≈90, 60.8, day+1, icon=0, white, comma=0)` —
  `day = DAT_0450fb84[slot] + 1`, capped 9999 (the "1" in the badge).
- **Money**: `FUN_00406a60(244.8, 22.4, DAT_0438b918, icon=1, white, comma=1)`
  — the "1,000pix": comma=1 inserts a thousands-comma sprite (src 752,144),
  icon=1 draws the "pix" sprite (src 776,144-830,174).
- `FUN_00406a60` digit `d` glyph: src (`d*24+512`, 144)-(`d*24+536`, 168),
  drawn right→left, 12.8 px pitch. Number formatted `"%d"`.
- A gated anim element (`DAT_00529704>0`, `FUN_0046c86f` scale/alpha) =
  new-event notification icon; dormant in steady free-roam.
- DUNGEON minimap block (gated `*DAT_068dd2f0 > 0`) — dormant in HOUSE.

For the new-game cap_05 the values are day=0→"Day 1" and money=1000 → matches
retail exactly. Port: `src/scene1_top_hud.{c,h}` (2026-06-02).

### Enable gate (when is the HUD shown) — FUN_0046c869 / DAT_073a3df0

The HUD (the whole `FUN_0040a765` scene-HUD block) is NOT drawn during a
full-screen-background cutscene.  In `FUN_004547ab`'s INGAME dispatch, when a
dialogue is active (`DAT_0438b1c8 == 1`) the scene+HUD block runs only if
`FUN_0046c869() == 0`, i.e. `DAT_073a3df0 == 0`.  `DAT_073a3df0` is the parsed
**bg-layer count of the active dialogue script**: the opening **iv1_1** has a
painted bg (`polybg:`) → non-zero → scene+HUD **suppressed** (only the dialogue
draws); **iv1_2** plays as an overlay **over the live HOUSE map** (no bg → 0) →
scene + HUD **drawn behind it**; free-roam (no dialogue) → drawn.  So the HUD
appears from iv1_2 onward, NOT during the first cutscene (user-reported
2026-06-02).  Port: `scene1_intro_dialogue_covers_screen()` (1 during
D_SCRIPT1=iv1_1) gates the `scene1_hud_render` call in main.c's INGAME render.

---

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
