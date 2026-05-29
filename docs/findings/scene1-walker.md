# Scene-1 HUD walker — FUN_0040a765 survey

**Status (2026-05-23):** Survey only. Written as part of the C7i chip
to inventory the 7558-byte function before any of its content ports.
No code lands from this doc; the chips that consume the survey are
C7j..C7n (or whatever granularity the next planner picks).

This survey shipped alongside the C7f/C7g/C7h ports — the small
brackets that surround FUN_0040a765 in the engine's render frame.
See `docs/findings/scene1-render.md` for the broader C7 ladder, and
`src/scene1_render.{c,h}` for the brackets themselves.

## Key correction — FUN_0040a765 is **not** the 3D mesh walker

The C7 ladder doc (`scene1-render.md`) tags this function as the
"main scene-1 render walker", and notes 7558 B with the "Mt. Everest
within Mt. Everest" label. Reading it confirms the size + the label
**but contradicts the "mesh walker" characterization**: the function
is overwhelmingly a 2D **HUD / overlay aggregator** that dispatches
non-3D draw work for the in-game HUD. The actual 3D mesh walker — the
one that calls `SetStreamSource` / `SetIndices` /
`DrawIndexedPrimitive` for the shop interior + props — lives at the
tail of **FUN_00459dfd** (the function we partially ported as
`mesh_set_default_render_state` in `src/mesh_draw.c`).

Concretely, here is what the engine actually does in the scene-1
render frame:

```
FUN_004547ab (1670 B, render thread top-level)
  ├── FUN_0045bbf9  (134 B)    ← C7f LANDED — camera + proj + state
  │     └── FUN_00459dfd (1444 B)
  │           ├── L86..L198    ← C7b LANDED as mesh_set_default_render_state
  │           └── L199..L257   ← *** the real 3D mesh walker ***
  │                              draws walls / floor / props / chr / lights
  │                              via FUN_00459847(0/1) / FUN_004552d0 /
  │                              FUN_00458bdf / FUN_00456f56 + per-mesh
  │                              FUN_004552d0 → FUN_00404a20 →
  │                              FUN_004047df → FUN_00404757 →
  │                              FUN_00404209 (SetStreamSource +
  │                              SetIndices + DrawIndexedPrimitive).
  │                              UNPORTED.  This is the real Mt. Everest;
  │                              FUN_0040a765 below is the Big Hill next
  │                              to it.
  ├── FUN_0040a765  (7558 B)   ← THIS DOC — 2D HUD / overlay aggregator
  ├── FUN_00417504  (506 B)    ← C7h LANDED — overlay-layer dispatcher state
  └── FUN_0045404b  (326 B)    ← C7g LANDED — FX tail (sin-shake quad)
```

`FUN_00459dfd`'s tail (L199..L257) is what the next porter actually
needs to climb to render the shop interior. It is mid-sized (~1 KB of
decompiled C), but each `FUN_00459847` / `FUN_004552d0` /
`FUN_00458bdf` / `FUN_00456f56` is its own multi-KB walker calling per-
prop transform math plus the mesh-draw chain. Treat L199..L257 as a
parallel survey to this one — its own roadmap, separate chips.

For this survey we stay scoped to **FUN_0040a765**, the function the
C7 ladder calls out by name. It is still load-bearing — the in-game
shop HUD (price popovers, character speech bubbles, news/order
panels, the day-counter flash, etc.) all originate here. Without it
the scene renders 3D geometry but no UI.

## What FUN_0040a765 actually contains

The decomp at `docs/decompiled/by-address/40a765.c` lays out as one
big control-flow chain — no early structuring — that the survey
groups into nine "passes". Each pass has its own gate (often on
`DAT_0438b1c0` = scene mode, `*DAT_068dd2f0` = stage type, plus
sub-menu state in `DAT_0438cc04` / `DAT_0438cc08`). The body is
flat-ish (no helper calls between passes that change global state in
a way that crosses pass boundaries), which is good news for porting:
each pass can land as its own chip with a narrow surface.

Sizes below are **post-decomp source lines**, not bytes. The total is
~750 lines in `40a765.c`; bytes ≈ 7558.

### Pass 1 — entry guards + 2D state preset (L45..L73)

```
local_48 = &DAT_044e3798 + DAT_0438b1e0 * 0x2dfc8;   // per-save-slot base
FUN_0049065b();                                       // 314 B — sub-init
if (DAT_005c570c == 0) return;                        // global render gate
FUN_0049b425();                                       // render_quad_state_setup
if (state==1 && stage>0 && (cond_0x10 || DAT_056db104)) {
    // stamina/HP backdrop quad on DAT_073cc8f0
    SRCBLEND=ZERO DESTBLEND=INVSRCCOLOR
    render_quad_add(...)
    render_quad_flush()
    SRCBLEND=SRCALPHA DESTBLEND=INVSRCALPHA
}
```

* `FUN_0049065b` (314 B) — small enough to port standalone. Purpose
  unknown; survey suggests it's a per-frame counter reset / state
  prep but not confirmed.
* The stamina backdrop uses an unusual SRCBLEND=ZERO/INVSRCCOLOR pair
  — mask-by-destination — same idiom as C7h layer-3.
* Gate variables (`DAT_056db104`, `DAT_0438b1e0`, etc.) are all 0 at
  boot, so this pass is dormant.

### Pass 2 — angle / spin overlay (L74..L100)

```
local_c = _DAT_0438b1dc;
if (-0.1 <= local_c <= 0.1) local_c = 0;
if (0 < local_c) {
    SetTexture(0, DAT_073aa188);   // "system" texture (atlas)
    // top quad — height = local_c * 32
    render_quad_add(...)
    // bottom quad — at 480 - top_height
    render_quad_add(...)
    render_quad_flush()
}
```

* Pair of letterboxing quads, height keyed off `DAT_0438b1dc` (some
  kind of dramatic-angle / cinema-bars effect).
* Dormant — `DAT_0438b1dc` BSS-zero today.

### Pass 3 — status-screen takeover (L101..L104)

```
if (DAT_073dddb4 != 0) {
    FUN_004141c0();    // status screen render — separate sub-walker
    return;            // FUN_0040a765 ends here when the status screen is up
}
```

* Total bypass of every later pass when the status screen is open.
* `FUN_004141c0` is a sibling render function (size not yet checked).
  Worth a separate survey if/when status-screen interaction is in
  scope.

### Pass 4 — sub-walker dispatch (L105..L192)

This is where the bulk of the gameplay HUD lives. The pass is gated
on `DAT_0438b1c0 == 1` (INGAME state) and forks on
`*DAT_068dd2f0` (stage type: 0 = HOUSE / shop, >0 = DUNGEON).

```
FUN_0040c4eb();    // 1059 B — item-tooltip popup (held item description)

if (state == 1) {
    if (stage == 0)  FUN_00409925();   // 3434 B — HOUSE sub-walker
    if (stage > 0)   FUN_00407cac();   // 7289 B — DUNGEON sub-walker
    SetTexture(0, DAT_073cc920);       // chr portrait atlas

    // ── per-character speech bubble #1 (DAT_056db000) ─────────
    if (DAT_056db000 != 0 && FUN_00490c78(...) sets local_58 < 0) {
        // 30 lines of float math — bubble pos + size + tail
        render_quad_add(...) on DAT_073cc920
        render_quad_flush()
    }

    SetTextureStageState(0, 1, 8);  // TSS1 COLOROP=ADDSMOOTH

    // ── per-character HP/timer bubble (DAT_056da1cc == 0x1e) ──
    if (DAT_056da1cc == 0x1e && DAT_056daff4 > 0 && (counters) ) {
        // similar shape — different atlas glyph at +0xd9 row
    }

    // ── per-NPC bubble loop over &DAT_0076bd94 stride 0x2e9 ───
    for (piVar9 = &DAT_0076bd94; piVar9 != &DAT_007c8f94;
         piVar9 += 0x2e9) {
        if (piVar9[1] == 2 && piVar9[0] in {0x25, 0x26, 0x27, 0x28}
            && (9 - piVar9[0x1b3]/0x3c) in (0, 10)
            && FUN_00490c78(...) local_58 < 0) {
            render_quad_add(...) on DAT_073cc920
            render_quad_flush()
        }
    }
    SetTextureStageState(0, 1, 4);  // TSS1 COLOROP back to MODULATE-ish
}
```

Per-character bubbles step through one global array per character
type. The NPC loop walks 31 records of 0x2e9 stride each — that's the
chara record table (`&DAT_0076bd94..&DAT_007c8f94`).

* `FUN_0040c4eb` (1059 B) — item-tooltip popup. Self-contained;
  reads `DAT_006482xx` state, draws backdrop + multi-line text.
* `FUN_00409925` (3434 B) — HOUSE sub-walker. **Not actually 3D**
  either; spot-check shows it dispatches HOUSE-specific HUD (item
  price markers, customer indicators). Its inner call `FUN_0046602e`
  (2668 B) is similar.
* `FUN_00407cac` (7289 B) — DUNGEON sub-walker. By size, this is
  HUGE — at least as big as FUN_0040a765 itself. Likely the per-
  enemy / per-item-pickup / per-trap HUD aggregator. Out of scope
  for the HOUSE-first ladder; dungeon entry is deferred behind
  stage-transition support.

### Pass 5 — flat overlay tier (L193)

```
FUN_00406d50();   // 1445 B
```

Single call. Purpose unknown — survey suggests it's a per-frame
animation system tick (player-character animation cells? cursor
animation?). 1445 bytes is a small-ish chip in its own right.

### Pass 6 — shop terminal panel (L194..L253)

Gated on `(state==1 && stage==0 && DAT_0438cc04==2)`. The 2 in
`DAT_0438cc04` is the "shop terminal active" sub-menu state.

```
SetTexture(0, DAT_073d8748);
// 6-row stacked quads (each ~40 px tall, indexed via local_18 +=0x28)
// + scroll-up FUN_0048edee + scroll-down FUN_0048ee77 sentinels
// + per-row text via FUN_0047ca05 with state-coloured strings:
//     "Revert To Table"
//     "Done Stocking"
//     other rows pulled from &DAT_00529ac0 / FUN_005038ff sprintf
//     + FUN_0046add8 (item-count badge)
```

* `FUN_0048edee` / `FUN_0048ee77` (sizes unknown) — scroll-arrow
  textures.
* `FUN_005038ff` — global sprintf-into-buffer (used a lot here).
* `FUN_0047ca05` — text draw at (x, y, str, color, scale). 454 B —
  already ported analog: `font_draw_text` (see src/font_draw.c).
* `FUN_0046add8` — item-count badge draw. Size unknown.

### Pass 7 — chr render dispatch (L255..L258)

```
FUN_0046b00a(0, 0);                    // 3640 B — chr render walker
if (state==1 && stage==0) FUN_00466b7b();  // 5305 B — HOUSE-only chr extras
FUN_0043537e();                        // size unknown
```

**CORRECTION (2026-05-29, C7m survey):** the "chr render" labels below
are WRONG — same mislabel pattern this survey itself flags for
FUN_00459847.  Body reads of both Pass-7 functions show they are
**shop-menu / panel renderers**, not character-avatar renderers, and
both bind the shop-terminal atlas `DAT_073d8748` (NOT the chr atlas
`DAT_073cc920`):

* `FUN_0046b00a(0,0)` (3640 B) — the **Vendors / market-stocking
  purchase menu**.  Early-returns when `DAT_0734b98c == 0` (menu
  slide-in counter); draws a 400×320 panel + per-row item/price/count
  text ("Venders", "%s x%d", "%d Left", "Confirm").  Dormant on an
  idle shop.
* `FUN_00466b7b()` (5305 B) — a **HOUSE sub-panel transition
  animator**.  Early-returns when `DAT_0438b7b0 == 0`; runs a
  slide/scale transition (`DAT_0730b5d0` phase 1/2) over the same
  terminal atlas.  Dormant when no panel is opening.
* `FUN_0043537e` (660 B) — small post-pass helper between Pass 7/8.

**The actual Recette/Tear/NPC character avatars are NOT rendered here.**
They are drawn by `FUN_004176ff` (30395 B) — `scene1_walk_chr_TODO` in
the 3D mesh-walker chain (`scene1_render_meshes`, src/scene1_render.c),
still a stub.  That 30 KB function is the real "characters in the shop"
renderer and the single largest unported function in the scene; it is a
C8-series climb, unrelated to this 2D HUD aggregator.  So Pass 7 here is
low-value for visible HOUSE pixels (dormant menus); porting it only
matters once the Vendors menu / sub-panel UI is in scope.

### Pass 8 — dialog text + sub-menu panels (L260..L709)

The largest single block in FUN_0040a765 (~450 lines). Gated on
`DAT_0438b1c0 == 1`.

**Sub-pass 8a — dialog text (L261..L351):** if `DAT_0438b92c != 0`
(dialog active), this draws the speaker portrait + name plate + body
text. Body text uses dynamic word-wrap (`FUN_0047d0ea` measures width
at scale, `FUN_0047ca05` draws once a candidate line fits). 90 lines
of float math + nested loops over a per-character text array at
`local_48 + 0x2a6c4` (per-save-slot offset).

**Sub-pass 8b — HOUSE sub-menu panels (L352..L709):** gated on
`*DAT_068dd2f0 == 0` (HOUSE only). Dispatches on the iVar4 lookup
into `&DAT_074b28b0` indexed by `DAT_0438cc0c`. The four panel cases
all share the gate `DAT_0438cc08 in {0xf, 0x10, 0x11}` (one of
several "panel-active" states):

* **case 8 — News Summary** (L352..L476): scrolling list of news
  events, walks `local_48 + 0x275c8` stride 12. Per-news template
  string + variable substitution via `FUN_005038ff`. Word-wrap loop
  mirrors sub-pass 8a's.
* **case 9 — Advance Order Summary** (L477..L568): order log with
  per-row deadline, orderee name, and an item-list. Empty-deadline
  branch shows hardcoded "Caillou" + "Charred Lizard's Bat Wings" +
  "Slime Liver" strings (legacy debug-tutor row).
* **case 0 — Charm / Mood Meter** (L569..L602): per-stat gauge
  texture (`DAT_073d8658`) + a 6×6 grid of stat squares (`DAT_073d8748`).
* **case 5 — Level Abilities** (L603..L707): vertically scrolling
  level chart with per-level unlock text (`s_Starting_Level_…` and
  the trailing per-level descriptions).

Each panel case is ~50-100 lines and could land as its own chip.
They share the same render-state preset from Pass 1, so chip
ordering doesn't matter past Pass 1.

### Pass 9 — day-counter flash + dungeon flash tail (L711..L792)

Sits below `LAB_0040c1e4` (the `goto` target from sub-pass 8a). Two
final overlays both keyed on `DAT_0438b1c0 == 1`:

* **HOUSE day flash** (L713..L772): when `DAT_0438b928 == 1 &&
  DAT_0438b924 < 0x8c` (day-counter animation active), shows a
  black-fade backdrop + "Survival Mode" / "Endless Mode" + "Day N"
  text at the centre of the screen for the first ~90 frames after
  scene fade-in.
* **DUNGEON white flash** (L773..L791): when `*DAT_068dd2f0 > 0 &&
  DAT_0438b8b8 > 0`, a fading white overlay alpha-scaled by the
  counter. Deferred with the rest of dungeon work.

Both are ~30-line blocks; could land together as one final chip.

## Proposed chip splits

Roughly ranked by "what the user sees first" + "what's smallest":

* **C7j — Pass 1 + 2** (~80 lines). Stamina backdrop + letterbox
  bars. Both dormant today but small + structural. Lands the entry
  guards + 2D state preset that every later chip inherits.

* **C7k — Pass 5 + 9** (~60 lines + the FUN_00406d50 stub). The
  per-frame animation tick + the day/flash overlays. Both depend
  only on Pass 1 having set 2D state; both are visible on the
  golden path (the day-counter flash is the first thing the player
  sees post-fade in HOUSE).

* **C7l — Pass 4 NPC bubble loop** (~120 lines + FUN_0040c4eb's
  1059 B item tooltip). The held-item description + the per-NPC
  speech bubbles. Visible during normal play.

* **C7m — Pass 6 + 7 — shop terminal panel + Vendors/sub-panel menus**
  (~80 lines + the FUN_0046b00a 3640 B Vendors menu + FUN_00466b7b
  5305 B sub-panel transition). NOTE (2026-05-29): NOT chr render —
  see the Pass 7 CORRECTION above. All shop-menu UI, dormant unless a
  menu/panel is open, so LOW value for visible idle-HOUSE pixels. The
  real character avatars are FUN_004176ff (scene1_walk_chr_TODO, 30 KB)
  in the 3D walker — a separate C8-series climb.

* **C7n — Pass 8 sub-menu panels** (~450 lines, split per case).
  News Summary / Advance Order / Charm Meter / Level Abilities —
  one chip per case, or one chip if the panels are mechanically
  similar enough.

* **C7o — Pass 3 status-screen takeover** (~3 lines here +
  FUN_004141c0 entire body). Whole sub-tree behind one `if`. Chip
  costs whatever FUN_004141c0 weighs.

* **C7p — the HOUSE / DUNGEON forks** (FUN_00409925 + FUN_00407cac).
  FUN_00409925 (3434 B) is HOUSE-only; FUN_00407cac (7289 B) is
  DUNGEON-only. Each is its own multi-KB chip; the DUNGEON one
  parallels the eventual stage-transition work and shouldn't block
  HOUSE-first development.

## Asset / global inventory

Everything FUN_0040a765 touches that has no porter yet:

### Textures (D3D8 ID3DXBaseTexture* — bound via SetTexture)

| Symbol         | Hint / Source                                    |
|----------------|--------------------------------------------------|
| DAT_073cc8f0   | Stamina/HP overlay sprite (Pass 1)               |
| DAT_073aa188   | System / FX atlas (letterbox bars, Pass 2)       |
| DAT_073cc920   | Chr portrait atlas (Pass 4 bubbles)              |
| DAT_073d8748   | Shop terminal background atlas (Pass 6 / 8 panels)|
| DAT_073d9ff0   | News-summary / order-summary backdrop (Pass 8 panels) |
| DAT_073d8658   | Charm-meter backdrop (Pass 8 case 0)             |

The two singletons we already preload in `scene1_preload.c`
(`leve_win.tga` → DAT_073d9ff0; `mood_para.tga` → DAT_073d8658) feed
two of these. The chr portraits cover DAT_073cc920. Remaining slots
need their loaders mapped — most likely `sysassets_load_all`'s tail
(see `src/sysassets.c`) already covers them but we haven't matched
the symbols.

### Per-save-slot record base

```
local_48 = &DAT_044e3798 + DAT_0438b1e0 * 0x2dfc8
```

`DAT_0438b1e0` is the current save-slot index (0..3). The 0x2dfc8
(187,848-byte) stride per slot is the full per-save record — items
inventory + per-NPC affection + day counters + adventure flags + …

Several offsets show up in passes 6 and 8 (e.g. `+ 0x2a6c0`,
`+ 0x2c400`, `+ 0x2dd64`, `+ 0x275c8`, `+ 0x2a600`, `+ 0x2bcc9`,
`+ 0x2c798`); each will need a typed field in whatever struct lands
to back the save record.

### Other globals worth watching

| Symbol           | Role                                            |
|------------------|-------------------------------------------------|
| DAT_005c570c     | Global render gate (Pass 1 early-return)        |
| DAT_0438b1c0     | Scene mode (1 = INGAME)                         |
| DAT_068dd2f0     | Stage palette pointer (`scene1_preload`-bound)  |
| DAT_0438b1d0/d8  | "Loading / between-scene" flags (FUN_0040c4eb gates) |
| DAT_0438b1dc     | Letterbox-bar height (Pass 2)                   |
| DAT_073dddb4     | Status screen active flag (Pass 3)              |
| DAT_056db000     | Speech-bubble #1 active flag (Pass 4)           |
| DAT_056da1cc     | Adventurer state code (0x1e = "in shop")        |
| DAT_056daff4     | Adventurer HP / timer counter                   |
| DAT_0076bd94..   | NPC record table (31 × 0x2e9 stride)            |
| DAT_0438cc04     | Sub-menu state (`2` = shop terminal active)     |
| DAT_0438cc08     | Panel-active state (0xf/0x10/0x11/0x12 codes)   |
| DAT_0438cc0c     | Panel-type index (look up in DAT_074b28b0)      |
| DAT_074b28b0     | 4-entry panel-type LUT                          |
| DAT_0438b8c0     | Sub-menu slide-in animation counter (Pass 8)    |
| DAT_0438b8c4     | Level-abilities scroll position (Pass 8 case 5) |
| DAT_0438b8c8     | Level-abilities scroll offset                   |
| DAT_0438b8cc     | Level-abilities pulse animator                  |
| DAT_0438b92c     | Dialog active counter (sub-pass 8a)             |
| DAT_0438b924     | Day-counter flash phase (Pass 9)                |
| DAT_0438b928     | Day-flash arm flag (Pass 9)                     |
| DAT_0438b8b8     | DUNGEON white-flash counter (Pass 9)            |

The save-record layout will eventually want a struct typed by these
offsets; the inventory above is the start of that map. Today they
are all BSS-zero, which means the entire function is `if (gate) {…}`
short-circuits across every pass — porting one pass at a time
lets the others stay dormant on the (zero) BSS path.

## Sub-call sizes (port effort estimate)

| addr        | size  | role (best guess from spot-checks)        |
|-------------|------:|-------------------------------------------|
| `0x4049b425`|  1182 | render_quad_state_setup — already ported  |
| `0x40c4eb`  |  1059 | Pass 4 item-tooltip popup                 |
| `0x49065b`  |   314 | Pass 1 sub-init (unknown specifics)       |
| `0x409925`  |  3434 | Pass 4 HOUSE sub-walker (price/cell HUD)  |
| `0x407cac`  |  7289 | Pass 4 DUNGEON sub-walker                 |
| `0x46602e`  |  2668 | Inner of FUN_00409925 (sub-walker leaf)   |
| `0x406d50`  |  1445 | Pass 5 animation tick                     |
| `0x46b00a`  |  3640 | Pass 7 chr render walker                  |
| `0x466b7b`  |  5305 | Pass 7 HOUSE chr extras                   |
| `0x435747`  |   300 | post-pass helper (between 7 and 9)        |
| `0x435117`  |   615 | post-pass helper (between 7 and 9)        |
| `0x43537e`  |     ? | between Pass 7 and 8                      |
| `0x4047ca05`|   454 | text draw at (x, y, str, color, scale)    |
| `0x47d0ea`  |    98 | text width measurement                    |
| `0x47d14c`  |   399 | centered-text draw                        |
| `0x5038ff`  |     ? | sprintf-to-buffer (libc-style)            |

Total decompiled-byte footprint of just FUN_0040a765's *sub-call
tree* (excluding the genuine 3D walkers and the text-rendering leaves
we already have) is around **35 KB** — comparable to the entire
title-screen render chain. The honest expectation is C7j..C7p as
**6-8 chips spread over multiple sessions**, with C7m alone fanning
into 2-3 sub-chips for the chr walker.

## Suggested next move

If the next porter wants to see something visible on screen quickly,
**C7k (Pass 5 + 9)** is the smallest "real player-facing" chip — the
day-counter flash is the first overlay the player sees after the
scene fades into HOUSE, and the animation tick at Pass 5 is needed
before any chr HUD can update its state machine.

If the next porter wants depth-first coverage of one feature, **C7l
(Pass 4 bubbles + item tooltip)** is the next-smallest "real
gameplay" chip — held-item descriptions + speech bubbles cover most
of the moment-to-moment shop UI.

If the next porter wants to unblock the 3D walker survey instead,
the right move is to leave FUN_0040a765 alone for now and write a
parallel survey of **FUN_00459dfd L199..L257** — that's where the
shop interior actually paints, and right now nothing in the C7
ladder addresses it.

## Related files

* `docs/findings/scene1-render.md` — overall C7 ladder + status of
  the brackets around this function.
* `src/scene1_render.{c,h}` — C7f/C7g/C7h ports (the brackets).
* `src/mesh_draw.{c,h}` — `mesh_set_default_render_state` ports
  FUN_00459dfd L86..L198. The L199..L257 tail (real 3D walker) is
  unported and is the *other* Mt. Everest.
* `docs/decompiled/by-address/40a765.c` — the full Ghidra output
  this survey indexes.
* `docs/decompiled/by-address/459dfd.c` — the 3D mesh walker
  (FUN_00459dfd, partial port).
* `docs/decompiled/by-address/4547ab.c` — the render-thread top-
  level that calls the four brackets back-to-back.
