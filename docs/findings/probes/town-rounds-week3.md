# Probe: town-rounds-week3

- **Run:** `town-rounds-week3` · **start:** save:64d43650865f · **task:** `tools/drive/tasks/town-rounds-week3.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 55 turns, $0.93, 7 min
- **Scenario:** `tests/scenarios/town-rounds-week3/` (3930 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/town-rounds-week3.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `town-rounds-week3`: 3,930 frames. `rd check` gives PASS and `rd verify` gives MATCH** (rng 62d6b1ca, sem 6b0cf6fb67f2241b).

I visited every destination the map allowed, in map order. All five open places can be visited in one morning without the day ending. The Pub is the only one that was disabled.

**Task steps (frame where each became true)**
1. Town map: f249. Loaded the save with `.*60 A .*30 A` and was in the house at f137 with `dlg 0` (no morning event). A search then walked me to the door.
2. Inside a location: f288 (the Market, scene 6).
3. Back on the town map: f385.
4. Home with control: `rd check` says f3844; the scene-change event fires at f3861 (scene 8→1, `stime` 0→1, `b928` = 2). The movie ends at f3930. Nothing forbidden happened (no scene 2, no title).

**What the map offers (day dword 15, the HUD says Day 16, `stime` 0)**
- The menu grid at 0x5fd620 (3 columns × 5 rows) reads `[-1,-1,-1 / 4,5,6 / 1,2,3 / 1,0,3 / 0,-1,0]`.
- Destination numbers: 0 Recettear (home), 1 Market, 2 Town Square, 3 Merchant's Guild, 4 Pub, 5 Chapel, 6 Adventurer's Guild.
- On entry the enable states (0x9643588) were `[1,1,2,1,0,1,1]`: Town Square was highlighted (it has an event today) and the Pub was disabled. No location was marked "Closed" (0x964362c all 0).
- After the Town Square event its highlight dropped, giving `[1,1,1,1,0,1,1]`.

**Each destination**
- **1 Market:** scene 6, no event (`dlg` stayed 0; `s32(0x5c7a2c)/0x5c7a30` = 1/1, apparently left over). The menu has Buy, Sell, Leave, with Recette saying "We came to the market... What do we do now?". I left with D D A. In at f288, back on the map at f385.
- **2 Town Square:** scene 14, an event plays. Script id `0x5c7a2c` = 11, `0x5c7a30` = 1. First lines: "Being in the plaza reminds me. I overheard some of the customers discussing a rumor abou[t]…" — a rumour/hint talk with the purple-haired girl. It is long: about 2,700 ticks of A taps (event f425→f3104, `dlg` 1→0 at f3104), then back on the map at f3122. ESC (Event Skip) is offered, but I did not use it.
- **3 Merchant's Guild:** scene 6, no event. The menu has Buy, Sell, Fusion, Talk (marked "New"), Leave; the line on screen is "Before you stock up, make sure to read up on everything you can!". I left with D×4 A. In at f3155, back on the map at f3272.
- **4 Pub: disabled.** Pressing A at f3315 did nothing and the scene stayed 8. It is shown dimmed, and selecting it shows the same box as the other places: "This action will not take any time". The map says nothing else. This fits the decompile note "dest 4 disabled when time-of-day < 2", i.e. it opens in the evening.
- **5 Chapel:** scene 13, an event plays. Script id 16 / 5. Recette says "It looks like nobody's here..."; it is short (`dlg` 1 from about f3378 to f3613), then back on the map at f3630.
- **6 Adventurer's Guild:** scene 11, no event. It opens the "Adventurer Selection" menu: Leave, Level:15 Swordsman Louie (fee 1450, gear: Longsword, Scrap Plate, Wooden Shield, Old Muffler), Level:8 Thief Charme. B leaves straight back to the map (scene 11→8 at f3770). Selecting this destination shows "Going to a dungeon will take 2 periods of time".
- **0 Recettear:** A at about f3827 takes you home at f3861; `stime` goes 0→1 and `b928` = 2 (the return beat).

**Time cost**
- `stime` is the s32 at 0x450fb88 (bank dword 0xb0fc). In this run I named it `tod`.
- It stayed 0 through all five visits: visiting a town location costs no time.
- Only going home costs a period (1, once you have been out). The map's box switches to "Returning to the shop will take 1 period of time" once you have visited somewhere. The flag that controls this, 0x45105a0, went 0→1 after the first visit, together with 0x451059c.

**Pictures:** Market f349, Town Square event f1100, map with the cursor on the Pub f3345, Chapel f3532, Adventurer's Guild f3750, home f3925. They are in `runs/town-rounds-week3/view/`. I did not open the Merchant's Guild frame (f3218); its menu comes from `rd text` only.

**State I named:** `tod` = `s32(bank + 4*0xb0fc)`. It is the same address as the `stime` from payday-week2, and that is the better name. I suggest adding `stime`, plus `wm_state` = 0x9643588[7] and `wm_closed` = 0x964362c[7], to the shared vars.

**Tool gaps**
- An `rd text` log over a range of frames would help: `text` only shows the current frame, so I had to stop mid-event to read lines.
- During a long event the `line` variable is often −1 between pages, which makes stop conditions for "event stalled" unreliable. My first long tap looked stuck when it was only slow.
