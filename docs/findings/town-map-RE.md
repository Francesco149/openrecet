# Town / World map — transition + scene RE (Phase 0)

**Scope:** the shop-exit → **TOWN MAP** (the overworld destination picker) transition and
the town-map scene itself. Reference recording `runs/recordings/town-map-load.{raw.jsonl,
save.bin}` (a Continue trace at the **tutorial** stage). Goal of the arc: port the whole
path so the port follows shop → door → fade → town map 1:1 vs retail. Plan:
`docs/plans/town-map-port.md`.

> **Status:** Phase-0 RE **done from the decompile** (`docs/decompiled/all.c`), cross-checked
> against the recording's anchors/inputs. Internally consistent end-to-end. The handful of
> items that still want a **live Frida/capture confirmation** are called out under
> "Live-check residue" — none are blockers for starting the port chips.

---

## TL;DR

- **The "town map" is the WORLD MAP**, engine top-level mode **`DAT_0438b1c0 == 8`**. Assets
  (texture **slot 10**): `worldmap_nomal.bmp` (day) / `worldmap_yugata.bmp` (evening) /
  `worldmap_night.bmp` (night) + `mappoint.tga` (the destination markers).
- **It is NOT a new map "loaded by id".** `FUN_0045281c`'s 2nd arg is a **load-step COUNT**,
  not a map id — the destination scene is selected purely by the **mode** (`DAT_0438b1c0`)
  that the loader restores. (This corrects the plan's "map_param 0x1e/0x3c/0x78" guess and
  its "0x11 = quit-to-title" note; `0x11` is just "17 load steps", used by almost every
  in-house transition.)
- **Exit trigger = the shop door** (user-confirmed): walk to door → door tooltip → **Z on
  door** → fade → world map. The door Z-handler is in `house_update` (`FUN_0048670f`).
- **Tutorial gating** (user-flagged "some highlighted, some disabled") is a per-destination
  **state array `DAT_09643588[0..6]`**: `0`=disabled (dim, "denied" SE on select),
  `1`=normal, `2`=highlighted (sin-pulsing). The world-map init `FUN_0049de20` sets these
  from the **tutorial progress flags `DAT_0450f3f9` / `DAT_0450f408`**.

### The mode map (full, from the preload + sim + render dispatchers)
`DAT_0438b1c0`: `0`=title, `1`=shop/house (ingame), `2`,`3`=ending, `6`=guild/market
"ivent", `7`, **`8`=WORLD/TOWN MAP**, `9`=transient fade, `0xb`,`0xd`,`0xe`,`0xf`,`0x10`,
`0xd`… Each mode has a preload (slot loader), a per-frame update (in the `FUN_004536cb`
update-dispatch), and a render (in the `FUN_004547ab` render-dispatch).

---

## 1. The exit/transition chain (shop → world map)

Reconstructed from the recording's anchors+inputs and the decompile. Button bits (engine
`g_sim_buttons` / `DAT_073dddd4`): `0x01`=Right, `0x02`=Left, `0x04`=Up, `0x08`=Down,
`0x10`=Z. The recording's exit window:

```
frame 409  Z (0x10) pressed at the door           (single frame; HF+211)
   …16 frames (the tile-dissolve fade) …
frame 425  LOADING_START (loading_active 0→1)  +  PAUSE_OPEN (DAT_0438b150 0→1)
frame 435  LOADING_END                             (world-map assets loaded, mode 8 live)
```

**Step by step:**

1. **Door tooltip.** While the player stands in the door zone (`bVar17` in `house_update`,
   computed upstream), retail shows a "press Z" prompt — the user's "tooltip at the door".
   *(The exact door-zone test + the tooltip renderer are an unported detail; see Live-check.)*

2. **Door Z-handler** — `house_update` `FUN_0048670f` @ `all.c:87637`:
   ```c
   if ((DAT_073dddd4 & 0x10) != 0) {          // Z pressed
     if (bVar17) {                            // player at the door
       if ((&DAT_0450f3f7)[slot] != '\0') goto …;   // already-exited guard
       DAT_074b2ec4 = 1;                      // ARM the stage-2 world-map load (see step 4)
       FUN_004526f5(0, 0x11);                 // begin the tile-dissolve fade/load
       DAT_056db000 = 0;
       if ((&DAT_0450f3fa)[slot] == '\0') {   // first time leaving the shop →
         (&DAT_0450f3fa)[slot] = 1;
         (&DAT_0450f3f9)[slot] = 1;           // ← TUTORIAL flag the world-map init reads!
         (&DAT_0450f3f7)[slot] = 1;
       }
       FUN_0049933c();                        // SE / effect
     }
   }
   ```
   `slot = DAT_0438b1e0 * 0x2dfc8` (the per-save-slot stride seen throughout the engine).

3. **The fade** — `FUN_004526f5(0, 0x11)` @ `all.c:49680` is the **tile-dissolve transition**
   trigger (sibling of the loader): it sets `DAT_0438bf7c=1`, `DAT_0438bf78=1`,
   `DAT_0438bf80=0` (scene), `DAT_005c5934=0x11` (step count) and primes a 10×10 screen-tile
   dissolve grid (`DAT_06a48d6c…`, velocity-stepped). The fade runs ~16 frames; it is the
   **same phase/counter the existing `fade_tick` (0x4526ab) flow-trace hook already reads**
   (`phase=DAT_0438bf7c`, `counter=DAT_0438bf78`). `loading_active` flips at the load thread
   start → the **LOADING_START** anchor at frame 425.

4. **Stage-2: enter mode 8** — back in `house_update` @ `all.c:86877`, once the fade/load
   step completes (`FUN_004528b3()` true) **and** `DAT_074b2ec4 == 1`:
   ```c
   DAT_0438b1c0 = 8;       // → WORLD MAP
   FUN_0049de0e(<dest>);   // set the initial selected destination (DAT_09643684)
   FUN_00474d92();
   FUN_0045281c(0, 0x11);  // kick the world-map asset load (scene=0, 17 steps)
   FUN_00452cde();         // start the async load thread
   ```

5. **World-map load completes → LOADING_END (frame 435).** Mode 8 is live; the scene init
   `FUN_0049de20` runs (snaps the shared cursor → the **PAUSE_OPEN** anchor; see §2/§3).

> **Why "PAUSE_OPEN" fires at the exit and is a red herring:** `DAT_0438b150` is the *shared*
> menu/cursor flag. The world-map init raises it to point the destination cursor
> (`FUN_0043561a`+`FUN_00435693`, `all.c:102848`), so the anchor named "PAUSE_OPEN" is the
> **map's destination cursor coming up**, not the pause menu. Confirms the plan's suspicion.

---

## 2. The loader / transition primitives (shared)

| VA | name (proposed) | what it does |
|----|------|------|
| `FUN_0045281c(scene, steps)` `all.c:49740` | `scene_load_begin` | sets `DAT_0438bf80=scene`, `DAT_005c5934=steps`, `DAT_0438bf7c=-1`, resets the tile grid. **`steps` (0x11/0x1e/0x3c/0x78) is a load-step COUNT, not a map id.** |
| `FUN_004526f5(scene, steps)` `all.c:49680` | `scene_fade_begin` | like the above but `DAT_0438bf7c=1` + primes the dissolve grid with a 30-step pre-roll. The **fade** variant (what the door uses). |
| `FUN_00452cde()` `all.c:49836` | `load_thread_start` | `CreateThread` → the async scene loader (`LAB_0045293d`). Flips `loading_active`. |
| `FUN_004528b3()` `all.c:49774` | `load_step_done?` | returns 1 when `DAT_0438bf78 == DAT_005c5934` (progress reached the step count). |
| `FUN_0045281c`/`fade` consumed by `fade_tick` 0x4526ab | — | already a flow-trace hook (phase=`bf7c`, counter=`bf78`). |
| `FUN_00453384(kind)` `all.c:50170` | `scene_transition_mode9` | the **mode-9** fade manager (saves prior mode in `DAT_06a499a8`, restores + re-inits on completion via `FUN_004682d0`/`00435612`/`004844ef` + `FUN_00473c03/668/672` by kind `DAT_06a4997c`). **Used by other transitions; the shop→town door does NOT route through mode 9** — it uses `FUN_004526f5` + the stage-2 `DAT_0438b1c0=8` set. (The plan's mode-9 hypothesis applies to other scene changes, not this door.) |

---

## 3. The world-map (mode 8) scene

### Dispatch
- **Preload (textures):** render-reload switch `all.c:51296` `case 8: FUN_004735ad()`.
  `FUN_004735ad` @ `all.c:71899` loads texture **slot 10**:
  `DAT_073da000`=worldmap_nomal, `DAT_073da010`=worldmap_yugata, `DAT_073da020`=worldmap_night
  (each 0x400×0x200), `DAT_073aa7d8`=mappoint.tga (0x100×0x400). Unload = `FUN_0047360f`
  (`FUN_00471905(10)`).
- **Scene init:** `FUN_0049de20` @ `all.c:102826` (populates destinations + tutorial states;
  see §4). Called on mode-8 entry via the scene-init dispatch (indirect — no direct caller in
  the decompile; **Live-check** the exact call site).
- **Per-frame UPDATE/sim:** update-dispatch `FUN_004536cb` (0x4536cb), `case 8: FUN_0049e163()`
  (`all.c:50605`). `FUN_0049e163` @ `all.c:103020`.
- **Per-frame RENDER:** render-dispatch `FUN_004547ab` (0x4547ab), `case 8: FUN_0049e686()`
  (`all.c:51186`). `FUN_0049e686` @ `all.c:103249` → `FUN_0049e3a3(1.0)`.

### Update — `FUN_0049e163` (mode-8 sim, 575 B)
- `_DAT_09643628` (float) = **entry timer**. `<3.0`: just arrived, keep snapping the cursor
  to the selected dest. `≤10.0`: still easing in. `>10.0`: accept input.
- When idle (no Z): `FUN_0049dfc1(0)` = the **cursor-move** handler.
- On **Z** (`DAT_073dddd4 & 0x10`) with the dest **enabled** (`DAT_09643588[sel] != 0`):
  unload slot 10 (`FUN_0047360f`), `FUN_00436f97` (stage furniture positions), begin the next
  scene load (`FUN_0045281c(0,0x11)`+`FUN_00452cde`), and set the **destination's mode**:

  | `DAT_09643684` (sel) | new `DAT_0438b1c0` | extra | dest (likely) |
  |---|---|---|---|
  | 0 | 1 | restore shop map (`DAT_0438b4dc`), `DAT_0438b4e0=0`, `DAT_0438b928=2` | **your Shop / home** |
  | 1 | 6 | `FUN_00490e16(1)` | guild/market ivent (uVar5=1) |
  | 2 | 0xe | `FUN_0045e019()` | — |
  | 3 | 6 | `FUN_00490e16(0)` | **Market (ichiba)** — tutorial target |
  | 4 | 0xf | `FUN_0045e196()` | — |
  | 5 | 0xd | `FUN_0045e3cd()` | — |
  | 6 | 0xb | — | — |

- On Z over a **disabled** dest (`DAT_09643588[sel]==0`): play denied SE `0x16a`, no transition.

### Cursor-move — `FUN_0049dfc1(param)` (409 B, `all.c:102920`)
- Reads directional edges `DAT_073dddd6` (bit `2`→up −1, `1`→down +1, `4`→left −1, `8`→right
  +1; none → return). `param!=0` ⇒ positions scaled by 0.7 (the zoomed-out view).
- Destinations are laid out in a **3-col × 5-row grid `DAT_005fd620[col + row*3]`** (15 cells,
  `-1`=empty). It finds the current `DAT_09643684` in the grid, steps by the input wrapping
  (`%3`, `%5`), finds the next **present** dest (id in `DAT_096435d8[]`), sets `DAT_09643684`,
  slides the shared cursor (`FUN_00435710`) to `DAT_005fd590/594[dest]`, and plays move SE
  `0x146`.

### Render — `FUN_0049e3a3(scale)` (739 B, `all.c:103136`); wrapper `FUN_0049e686`→`(1.0)`
1. **Background** (`all.c:103162`): the worldmap photo with a **time-of-day crossfade**. Two
   passes blend `DAT_073da000[tod]` ↔ `[tod-1]` by an alpha derived from the time fraction;
   `tod = DAT_0450fb88[slot]` (0=day,1=evening,2=night). dst = `scale*640 × scale*480`.
2. **Destination markers** (`mappoint.tga` `DAT_073aa7d8`, `all.c:103193`): loop `DAT_005fd588`
   destinations. Per dest `i` (map-pos `DAT_096435d8[i]`):
   - alpha `uVar1`: default `0x7f`; **`DAT_09643588[i]==0` → `0x40` (DISABLED, dim)**;
     **`==2` → sin-pulsing** (`__ftol(sin(DAT_09643628*0.15))`, `FUN_00503a44`=sinf → the
     tutorial HIGHLIGHT throb).
   - the **selected** dest (`pos==DAT_09643684`): drawn bigger (180×56 vs 144×44.8) + α`0xff`.
   - quad src row from `DAT_005fd598[pos]`, dst at `DAT_005fd590/594[pos]`, colour
     `ARGB(sizeAlpha, uVar1, uVar1, uVar1)`.
3. **"Closed" labels** (`all.c:103233`): where `DAT_0964362c[i] != 0`, draw centered red
   (`0xffff3737`) **"Closed"** (`s_Closed_005fd65c`, `FUN_0047d14c`, scale `0x3f99999a`=1.2)
   at the dest position.

---

## 4. Destination model + TUTORIAL gating (the user-flagged feature)

The world-map init **`FUN_0049de20`** (`all.c:102826`) populates the destination set and the
state array each time you enter the map:

- **`DAT_005fd588 = 7`** destinations. `DAT_096435d8[0..6] = 0..6` (identity → map-position).
- Zero `DAT_0964362c[0..6]` (closed flags) and **`DAT_09643588[0..6]` (states)**.
- **Tutorial/progress branch** (`all.c:102868`):
  ```c
  if (DAT_0450f3f9[slot] == 0) {           // tutorial flag A clear
    if (DAT_0450f408[slot] == 0)           //   flag B clear  → fully unlocked:
      for i in 0..6: DAT_09643588[i] = 1;  //     ALL destinations NORMAL
    else                                    //   flag B set:
      DAT_09643588[0] = 2;                  //     only dest 0 highlighted
  } else {                                  // flag A SET (the door-exit set it!) →
    _DAT_09643594 = 2;                      //   dest 3 (Market) HIGHLIGHTED; rest stay 0 (disabled)
  }
  ```
  **In the recording** (first exit ever), the door set `DAT_0450f3f9[slot]=1`, so the init
  takes the **`else`** branch → **destination 3 (Market) is highlighted (state 2), the other
  six are disabled (state 0)**. That is exactly the user's "some highlighted, some disabled":
  the tutorial forces you toward the Market.
- **Time-of-day / day closures** (`all.c:102883`): `DAT_0450fb84[slot] % 7 == 3` and
  `tod = DAT_0450fb88[slot]` flip individual states/`DAT_0964362c` (e.g. dest 1
  `DAT_0964358c=0` when `tod==3`; dest 4 `_DAT_09643598=0` when `tod<2`; dest 5 closed when
  `tod==3`).
- **Event upgrade** (`all.c:102905`): for dest 1..6, `FUN_0045de68(dest,…)` (a "does this
  destination have an event today?" probe, `all.c:56440`) — if it returns nonzero and the
  dest is enabled, bump its state to **2** (highlight). So highlights = tutorial-forced **or**
  has-an-event.

**State model `DAT_09643588[i]`:** `0`=disabled (dim α0x40, denied SE on Z), `1`=enabled
(normal α0x7f), `2`=highlighted (sin-pulse). The tutorial special-case is purely these flags
being driven by `DAT_0450f3f9`/`DAT_0450f408` — **no separate "tutorial render path"**, so the
port reproduces it for free once `FUN_0049de20` + the flag plumbing are ported.

### Scene-state globals (world map)
| global | meaning |
|---|---|
| `DAT_09643684` | selected destination index (cursor) |
| `_DAT_09643628` (f) | entry/intro timer (snap<3, ease<10, input>10) |
| `DAT_0964367c` | exit-in-progress counter |
| `DAT_005fd588` | destination count (=7) |
| `DAT_096435d8[i]` | dest `i` → map-position id |
| `DAT_09643588[i]` | dest state (0/1/2) ← tutorial gating |
| `DAT_0964362c[i]` | dest "Closed" flag (red label) |
| `DAT_005fd620[col+row*3]` | 3×5 grid → dest-id (`-1`=empty) — **.data table, extract** |
| `DAT_005fd590/594/598[pos*0xc]` | per-dest screen x / y / mappoint sprite-row — **.data table, extract** |
| `DAT_0450fb88[slot]` | time-of-day (0 day/1 eve/2 night) |
| `DAT_0450f3f9`,`DAT_0450f408`[slot] | tutorial progress flags |
| `DAT_073da000/010/020`, `DAT_073aa7d8` | worldmap day/eve/night + mappoint textures (slot 10) |

---

## 5. Chip plan (refines `plans/town-map-port.md` Phases 1–N)

Each chip its own commit, verified vs the captured retail ground truth (`town-map-load-*`
retail frames + `call_trace.jsonl`) via `scenario-test --target both` + `flow_diff` /
`pixel_diff`. Annotate the town funcs on **both** sides (the flow-trace IS the comparison
tool) as we go.

- **T1 — door exit trigger + fade.** Port the door-zone test + the Z-handler
  (`house_update` `all.c:87637`): arm `DAT_074b2ec4`, set the tutorial flags
  `DAT_0450f3f9/3fa/3f7`, call the dissolve-fade `FUN_004526f5(0,0x11)`. Then the stage-2
  `all.c:86877` block (`DAT_0438b1c0=8` + `FUN_0049de0e` + load). **Verify:** the port emits
  `LOADING_START`(+`PAUSE_OPEN`) ~16 frames after the door Z, like retail (anchors match the
  recording's 409→425→435). *(The door tooltip render can be a follow-up; gate the trigger on
  the same `bVar17` door-zone.)*
- **T2 — world-map load + mode-8 plumbing.** Port `FUN_004735ad` preload (slot-10 textures)
  + the scene-init `FUN_0049de20` (destination tables + state array + tutorial branch) +
  hook mode 8 into the port's update + render dispatch. **Extract the .data tables**
  `DAT_005fd620` (3×5 grid), `DAT_005fd590/594/598` (per-dest x/y/sprite) like the furniture
  base-template extraction. **Verify:** the port reaches `LOADING_END` in mode 8 (no longer 0
  frames); `DAT_005fd588=7`, the tutorial state array matches retail (dest 3 = 2, rest 0).
- **T3 — world-map RENDER `FUN_0049e3a3`.** bg time-of-day crossfade → mappoint markers
  (per-state alpha/size + selected) → "Closed" labels. Pixel-verify vs the retail town frames
  (`town-map-load-fixcheck/retail/frames`, 92; `town-rerecord-fix` ~240). Reuses the ported
  `FUN_00404efc` quad + `FUN_0047d14c` centered text.
- **T4 — world-map SIM `FUN_0049e163` + cursor-nav `FUN_0049dfc1`.** Entry timer, grid nav
  (3×5), Z-select → the destination→mode table, the disabled-denied SE. **Verify:** drive the
  recording's post-load Up/Left/Down (frames 489–744) `--target both`; `DAT_09643684` +
  cursor position match retail per frame via `flow_diff`.
- **T5 — full-trace replay** of `town-map-load --target both`: shop → fade → town map; windows
  pair (port==retail kept-count), PHASE-CLEAN under `{phasepin}`+`{rngseed}`.

---

## 6. Live-check residue (Frida/capture confirmations — not blockers)

The static RE is internally consistent; these would *confirm* details and are cheap to fold
into the first both-capture (Phase 0.1/0.2 of the plan):
1. **The init dispatch call site** for `FUN_0049de20` (indirect in the decompile). Hook it on
   retail to see who calls it on mode-8 entry, and the `DAT_09643684` value passed to
   `FUN_0049de0e` at `all.c:86882`.
2. **Confirm the tutorial branch** actually taken in the recording: read `DAT_0450f3f9` /
   `DAT_0450f408` / `DAT_09643588[0..6]` on retail right after the world-map loads (expect
   `f3f9=1` → dest 3 = 2, rest 0).
3. **The door zone test (`bVar17`)** + the tooltip renderer — the exact position predicate
   and the prompt draw (for T1's tooltip follow-up).
4. **Fade duration** (the 16-frame Z→LOADING_START gap) — confirm it's `FUN_004526f5`'s
   dissolve, not an intermediate door animation.
5. **Destination names** (which index is Guild vs Market vs Adventurer's Guild, etc.) — read
   off the retail town frames in T3.

A transition-window capture (`town-map-load` with a `{caprange}` starting a few frames before
the door Z @ ~409 through `LOADING_END`+N) gives all of the above on the retail leg (the port
can't follow until T1/T2). The frida-safe VA set does **not** currently include `0x45281c` /
`0x453384` / `0x4526f5` / `0x49de20` — add field reads (mode `DAT_0438b1c0`, scene
`DAT_0438bf80`, step `DAT_005c5934`, `DAT_074b2ec4`, the tutorial flags) to confirm cheaply.

---

## Cross-refs
- Plan: `docs/plans/town-map-port.md`. Recording: `runs/recordings/town-map-load.*`.
  Retail ground truth: `runs/trace-studio/town-map-load-fixcheck/` (92 town frames + call
  trace), `town-rerecord-fix`/`-160153` (~240).
- Engine-quirks to log when porting: the world map's shared-cursor reuse for the destination
  pointer (the "PAUSE_OPEN at the exit" red herring); `FUN_0045281c` arg-2 = load-step count.
- Related: the shared cursor `title_save_dialog_cursor_*` (`FUN_00435693/710/61a`) is the same
  one the title/options/skip-prompt/display-menu use — the world map drives it too.
