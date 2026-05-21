# Title settings submenu (`DAT_09643524 == 2`)

The title scene's "Options" submenu inside FUN_0049a59e. Sibling of the
in-game pause sound menu FUN_0047fc44 — the two share the same volume
slider globals (DAT_056e577*) but live on different code paths. This
doc covers only the title side; the in-game pause variant lives behind
FUN_0047fa76 and is unreachable until an in-game scene ports.

## State machine globals

| Global          | Meaning                                          | Range            |
|-----------------|--------------------------------------------------|------------------|
| `DAT_09643524`  | Current submenu (0 = main, 2 = settings, 1/3/4)  | 0..4             |
| `DAT_09643520`  | Slide-in tween counter                            | 0..10            |
| `DAT_09643528`  | Slide direction (1 = slide OUT to main, 0 = IN)   | 0/1              |
| `DAT_09643530`  | Submenu cursor row                                | 0..5 (settings)  |
| `DAT_09643534`  | Submenu scroll (load-game only — settings = 0)   | int              |
| `DAT_09643540`  | Main-menu cursor row                              | 0..count-1       |
| `DAT_09643544`  | A-press select-pulse                              | 0..15            |
| `DAT_09643548`  | Clear-data confirm modal flag                     | 0/1              |
| `DAT_09643560`  | Settings exit state (0=clean, 1=dirty,             | 0..3             |
|                 | 2=exit-save, 3=exit-no-save)                      |                  |
| `DAT_09643510`  | Main menu row count                               | int              |
| `&DAT_09643358` | Main menu row-id array (codes 0..8)               | int[count]       |

Bare-path summary: `DAT_09643528` is seeded to `1` by FUN_0049a3a3
(scene init), which causes the tween counter `DAT_09643520` to ramp
DOWN toward 0 — i.e. the main menu is visible at boot. Main-menu input
is gated on `DAT_09643520 == 0` (line 492 of 49a59e.c) and submenu
input on `DAT_09643520 == 10` (line 251).

## Slide-in animation (lines 239-250 of 49a59e.c)

```c
if (DAT_09643528 == 1) {            // exiting submenu (or boot)
    DAT_09643520 = DAT_09643520 - 1;
    if (DAT_09643520 < 0) DAT_09643520 = 0;
} else {                            // entering submenu
    DAT_09643520 = DAT_09643520 + 1;
    if (10 < DAT_09643520) DAT_09643520 = 10;
}
```

## Main → settings entry (lines 534-543)

After the 0xf-frame select-pulse on the OPTIONS row (menu code 2):

```c
if (iVar1 == 2) {
    DAT_005d1bd8 = 0xffffffff;       // some "draw cursor sprite" handle
    DAT_09643524 = 2;                // enter settings state
    DAT_09643528 = 0;                // start slide-in (cursor_anim → 10)
    DAT_09643530 = 0;                // settings cursor row = 0
    DAT_09643534 = 0;
    FUN_00435693(168.0, 168.0);      // cursor sprite teleport
}
FUN_0043561a();                       // mark "input gate closed" / similar
```

After this, the next frame's outer arm runs the cursor_anim ramp until
`DAT_09643520 == 10`; settings input then activates.

## Settings input dispatch (lines 371-475)

Gated on `DAT_09643524 == 2 && DAT_09643548 == 0` (modal not open).

### Buttons

| Bit              | Meaning           | Action                                                                                |
|------------------|-------------------|---------------------------------------------------------------------------------------|
| `0x10`+row==5    | A on clear-data   | Open "Clear all data — are you sure?" modal (`DAT_09643548 = 1`); SE `0x143`.        |
| `0x30` (A or B)  | Exit              | SE `0x143`. `DAT_09643560 = (was==1) ? 2 : 3`; jump LAB_0049b162 → FUN_00435612.    |
| `0x04` (UP)      | Cursor up         | `cursor = (cursor + 5) % 6`; SE `0x146`; cursor tween `(168, row*40+168)`.            |
| `0x08` (DOWN)    | Cursor down       | `cursor = (cursor + 7) % 6`; SE `0x146`; cursor tween same.                            |
| `0x02` (LEFT)    | Dec slider        | See "Per-row slider" below.                                                            |
| `0x01` (RIGHT)   | Inc slider        | See "Per-row slider" below.                                                            |

After any successful slider change: `DAT_09643560 = 1` (mark dirty).

### Per-row slider mapping

| Row | Var           | Range | Inc/dec SE feedback         | Notes                                  |
|-----|---------------|-------|-----------------------------|----------------------------------------|
| 0   | `DAT_056e5778` (BGM)   | 0..8 | none (silent)  — but `FUN_00499583` re-applies BGM volume | audio_fade_apply(BGM) |
| 1   | `DAT_056e5774` (SE-A)  | 0..8 | SE `0x146`                  | volume takes effect on next play       |
| 2   | `DAT_056e577c` (SE-B)  | 0..8 | filename SE via FUN_0049933c (`re_sys01a_b_005d1d{48,6c}` per direction) | volume takes effect on next play |
| 3   | `DAT_056e5784` (slider3)| 0..2 | SE `0x146`                 | default 1 (likely text-display speed)   |
| 4   | `DAT_056e5782` (slider4)| 0..1 | SE `0x146`                 | default 0 (likely a toggle)            |
| 5   | (button)               | n/a  | SE `0x143` on A             | "Clear all data" — opens confirm modal  |

Row 5 doesn't have a numeric value — A on it opens the modal; LEFT/RIGHT
are ignored.

## Settings → main exit (LAB_0049a5d3 + dirty-save check)

At top of FUN_0049a59e each frame, before the ramp:

```c
if (DAT_09643560 == 2) {              // dirty + exit pressed → save
    FUN_004905a8(0xffffffff);          // save.dat + _save.dat
    goto LAB_0049a5d3;
}
if (DAT_09643560 == 3) goto LAB_0049a5d3;

LAB_0049a5d3:
    DAT_09643544 = 0;                  // clear pulse
    DAT_09643528 = 1;                  // start slide-OUT
    FUN_00435612();                    // cursor visibility off
    FUN_0049a43d();                    // refresh main menu (save flags
                                       // may have changed)
    // Find main-menu row whose code == 2 (OPTIONS); set DAT_09643540
    // to that index so the cursor returns to where the user came from.
    DAT_09643540 = 0;
    if (DAT_09643510 != 0) {
        piVar6 = &DAT_09643358;
        do {
            if (*piVar6 == 2) break;
            DAT_09643540++;
            piVar6++;
        } while (DAT_09643540 != DAT_09643510);
        if (DAT_09643540 == DAT_09643510) DAT_09643540 = 0;
    }
    DAT_09643560 = 0;                  // clear exit state
```

So pressing A or B in the settings submenu sets `DAT_09643560 = 2` (if
dirty) or `3` (if clean). The frame *after* that, FUN_0049a59e sees the
non-zero state at the top, saves if dirty, and folds back to main with
the cursor seeded on the OPTIONS row.

## Cursor sprite (DAT_0438ab*-DAT_0438ac*)

Shared across all menus. Driven by two helpers:

- `FUN_00435693(x, y)` — teleport (no tween). Used on submenu enter.
- `FUN_00435710(x, y)` — tween over 6 frames. Used on cursor-row move.

The settings sub-menu uses pixel coords `x = 168, y = row * 40 + 168`.
The settings panel's row stride is 40px (matching the FUN_0049c050
render layout).

`FUN_00435612()` sets `DAT_0438b150 = 0` — disables the cursor sprite
(used when leaving the menu). `FUN_0049b162` is a small fall-through
that calls FUN_00435612 + goto LAB_0049b415 — used in several "input
done, defer to next frame" spots.

## What the port covers (this milestone)

- **Sim:** producer logic for the 5 sliders + cursor + exit/enter
  transitions. Lives inside `src/scene_title.c` to keep the title
  scene self-contained (matches the engine's structure — FUN_0049a59e
  *is* the title sim, all submenus inclusive).
- **Audio wiring:** BGM/SE-A/SE-B slider changes call
  `audio_fade_set_slider` + (BGM only) `audio_fade_apply`. SE feedback
  uses a new `audio_play_se_by_id` lookup over the existing SE table.
- **Non-audio sliders (rows 3 & 4):** new module state for
  `g_settings_slider3` (0..2) and `g_settings_slider4` (0..1). Engine
  default values mirrored. Their downstream consumers (text-display
  speed at FUN_004... line 4969, boolean flag at line 67189) live in
  unported subsystems — for now the state just persists.

## What's deferred

- **Visual render (FUN_0049c050).** 1001 bytes. Depends on a font
  system (FUN_0047ca05 text helper) for the slider labels. Without
  the font system the panel can render as a sprite + cursor but the
  numeric values would be unreadable. Slated for the font-system
  milestone. See "Open next-session candidates" #3 in PROGRESS.
- **Filename-based SE-B feedback (FUN_0049933c).** Requires loading
  arbitrary .wav files via the DirectMusic loader (separate from the
  resource-baked SE table). Port falls back to SE `0x146` for row-2
  inc/dec, matching the other rows. Documented as an engine deviation.
- **Save-on-exit (FUN_004905a8).** Save IO not ported yet. Exit-save
  path mirrors the engine's state transitions but `FUN_004905a8(-1)`
  is a no-op stub. Sliders persist in audio_fade module state for the
  lifetime of the process — fine for visible playtesting.
- **Clear-data modal (row 5 + FUN_00434def).** No save IO → no data
  to clear. Row 5 selection is acknowledged with the entry SE (`0x143`)
  but the modal flow exits immediately. Documented.

## Engine-quirk file additions queued

- **#48** — DAT_09643528 BSS-zero would have main menu sliding OFF at
  boot if FUN_0049a3a3 didn't explicitly seed it to 1.
- **#49** — Row 0 (BGM slider) plays no SE feedback (FUN_00499583
  re-applies the running BGM volume instead). All other rows that
  *change a value* play SE 0x146; row 0 is silent except for the
  BGM-volume change the user already hears.
- **#50** — Row 2 (SE-B slider) plays a separate filename-based SE
  per direction (`re_sys01a_b` w/ inc vs dec variants). The two
  files are otherwise identical in shipped vendor data; engine
  authors apparently wanted a different "feel" for the SE-B slider
  vs. the SE-A slider but didn't follow through with distinct samples.
