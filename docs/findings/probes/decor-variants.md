# Probe: decor-variants

- **Run:** `decor-variants` · **start:** save:a8fac0798453 · **task:** `tools/drive/tasks/decor-variants.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 43 turns, $0.53, 4 min
- **Scenario:** `tests/scenarios/decor-variants/` (770 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/decor-variants.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

## decor-variants: all four decor kinds probed, task passes

Run **decor-variants** is a 770-frame movie from save:a8fac0798453. `rd check` gives **PASS** and `rd verify` gives **MATCH** (frame 770, rng 3a84374c). I looked at the real-device pictures of the previews and of the changed shop.

**Steps:**
- Load at f92; in the house at f137 (the morning event starts at f138).
- Event skipped with `~ .*50 A`: `dlg == 0` at f210, much earlier than the ~f504 the card gives.
- Walked to the counter with the known route; `cc08` 1 → 0 at f287.
- **Step 1** (counter menu list up): f297.
- **Step 2** (decor editor open, `cc08` 0x17): f323. It opens on Walls; `cc08` goes 10 → 23 at f323 and → 3 at f324.
- **Step 3** (free roam, `cc08 == 1 and dlg == 0`): f740, after D to Cancel and A (`cc08` 0 → 10 at f725, 10 → 1 at f740). The movie idles to f770.

**Per kind:**

| Kind (window title) | Rows offered | What I did | Result |
|---|---|---|---|
| Walls ("Wallpapers") | Lacquer Walls 5101 (default), Plank Walls 5102, Starry Wallpaper 5103 | Previewed rows 1 and 2, confirmed Starry with A (f387, back to `cc08` 0 at f393) | `d_wall` 0 → 2 |
| Flooring | Plank Floor 5001 (default), Tile Floor 5002, Stone Floor 5003 | Previewed Tile then Stone, confirmed Stone (f484, back at f490) | `d_floor` 0 → 2 |
| Carpet ("Carpets") | Red Carpet 5401 (default), Green Carpet 5402, Tiger Carpet 5403 | Previewed Green then Tiger, then **cancelled** with B (f581) | `d_carpet` 2 → 0 |
| Tables ("Counters") | Wooden Counter 5301 (default), Legendary Counter 5302, School Counter 5303 | Previewed Legendary, School, Legendary; confirmed Legendary (f702, back at f708) | `d_table` 0 → 1 |

- All eight `--add-item` ids are real decor items, and every list shows default + 2 in id order.
- Prices from the help box: Lacquer 500, Starry 1,000, Plank Floor 500, Stone 1,000, Red Carpet 500, Tiger 6,000, Wooden Counter 500, Legendary 100.
- In the tables preview the Legendary Counter turns every table into cardboard boxes.

**Variables (defined in this run's vars):**
- **Current / previewed variant:** the room's dwords themselves. `d_wall` = bank+0x2cde4 (0x451057c), `d_floor` +0x2cde8, `d_carpet` +0x2cdec, `d_table` +0x2cdf0. The value follows the highlighted row on the next tick (row 2 → 2), so preview writes the room live.
- **Row cursor:** `iw_row` = `s32(0x7337850 + 4*s32(0x734b968))`. 0x734b968 is the item-window tab and 0x7337210[tab] is the row count (3 here), found from IDA `sub_4692A4`.
- **Item under the cursor:** `s32(0x731f598 + 8*(row + s32(0x731f408 + 4*tab)))`, the id << 6 | 1. For example 0x4fb80 = 5102 Plank Walls.

**Bank bytes changed on confirm** (diff restricted to bank 0x44e3798..0x4520000):
- The room dword was already set during the preview, so it does not show up in the confirm diff.
- The chosen item leaves the bag: its slot in 0x44e3830.. is removed and the later slots shift down one, so the last slot becomes −1.
- The old default (put into the bag when the kind opened) **stays in the bag**.
- 0x450f2b0 goes 41 → 40 on every close; it went 40 → 41 on open, so it looks like a bag count.
- 0x44e37a0 is a running counter and changes regardless.
- **Cost: none.** `gold` stays 9,999,999 throughout.

**What cancel restores (Carpet):**
- B leaves at once: `cc08` 3 → 0 on the next tick, with no 15-tick dispatch.
- The room dword goes back to its value at open (`d_carpet` 2 → 0), and the restored Red Carpet (0x54640) leaves the bag again, so the bag is as before.

**Pictures** (real device, in `drive/runs/decor-variants/view/`):
- f339 Lacquer (default) and f386 Starry wall preview: the walls become a starry sky.
- f456 Tile and f483 Stone floor preview: the stone floor shows.
- f578 Tiger carpet preview.
- f673 School and f701 Legendary counter preview.
- f770 shop after: starry walls, stone floor, cardboard-box tables, red carpet.
- In each kind the camera eases toward that kind.

**Game facts beyond the task notes:**
- Confirm (A in `cc08` 3) returns to the counter menu after 7 ticks, with no confirmation dialogue.
- The counter-menu cursor stays where it was after a kind closes.
- The menu needs ~12 ticks before D is accepted again.

**What the tool lacked:**
- A whole-RAM diff is very noisy (2,262 dwords for one cursor move), and the item window's state is at 0x73xxxxx, outside the default .data range I narrowed to. A named `iw_*` set of vars for the shared item window (tab, row, count, item id) would save the IDA lookup; I'd suggest adding `iw_row` and `d_wall`/`d_floor`/`d_carpet`/`d_table` to the shared vars.
