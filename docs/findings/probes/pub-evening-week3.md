# Probe: pub-evening-week3

- **Run:** `pub-evening-week3` · **start:** save:64d43650865f · **task:** `tools/drive/tasks/pub-evening-week3.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 67 turns, $0.70, 10 min
- **Scenario:** `tests/scenarios/pub-evening-week3/` (3928 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/pub-evening-week3.md); verify --real MATCH; retail golden made (Windows, real device)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `pub-evening-week3`: 3,928 frames.** `rd check` gives PASS and `rd verify` gives MATCH (rng 04871f81, sem 4703fbc22d7ea6b3). Nothing forbidden happened: no scene 2, and the game never went back to the title.

**Step frames**
1. **The Pub can be entered: f1233.** The map is open with `stime` 2 and Pub state 2.
2. **Inside the Pub: f1303.** The Pub is **scene 15**.
3. **Home with control: f3838.** scene 1, cc08 1, dlg 0. `stime` goes 2→3 one tick later (it read 3 by f3853). The day did not end: the day dword stays 15 and Recette can move, so this is the first frame with control, not the night exchange. The movie ends at f3928.

**Route**
- Loaded the save (`.*60 A .*30 A`; in the house at f137). A search walked me to the door, and the map opened at f294.
- First trip:
  - Market at f342, left with D D A at f424.
  - I hit R by mistake from the Market cursor and went into the Town Square event. I skipped it with ESC → Yes (map at f680). This costs only ticks, not time.
  - Went home at f729: `stime` 0→1.
- Out again at f914, the Market again (f962–f1044), home at f1126: `stime` 1→2.
- Out again at f1233. Map cursor 0 → U (2) → U (5) → L (4 Pub) → A.

**What costs time (observed)**
- Visiting a town location (Market, Town Square, and the Pub itself) costs 0.
- Going home after visiting somewhere costs 1 period. This happened every time: 0→1, 1→2, 2→3.
- I did not test the store (1 period by the task's note) or a dungeon (the map says 2 periods).
- So the cheapest way to make two periods pass is two "visit somewhere → go home" loops, about 900 ticks in all.
- Going home without having visited anywhere costs nothing (my first stray A at f322 went home with `stime` still 0).
- At `stime` 2 the shop door needed facing +x and then A presses, and there is about 20 ticks of black fade before scene 8. A plain search from the door spot failed for 30 s; walking away and back right, then A, worked.

**Enable states at 0x9643588[0..6]**
- Morning, before the Town Square event: [1,1,2,1,0,1,1]. The Pub is disabled.
- Afternoon (`stime` 1, after I skipped the Town Square event): [1,1,2,1,0,1,1]. Town Square stays at 2 when the event is skipped, and the Pub is still disabled.
- Evening (`stime` 2), on arrival: **[1,1,1,1,2,1,1]**. The Pub is highlighted (an event waits), Town Square has dropped to 1, and closed[4] = 0.
- Evening, after the Pub event: **[1,1,1,1,1,1,0]**. The Pub drops to 1 and the **Adventurer's Guild becomes disabled**.
- `stime` 2 is evening on the HUD clock (the third slice).

**The Pub, first visit (event)**
- Script ids: `s32(0x5c7a2c)` = 6, `s32(0x5c7a30)` = 1.
- dlg 1 from f1304 to f3434, then back on the map (scene 8) without any menu.
- First line (Tear): "Merde. Into her cups again, I see..."
- Next lines: Charme, drunk: "Hahaaaah! Shilly fairy, you can't shtick a person in a cup!". A "??" man (blond swordsman): "Alright, alright. I can take a hint. I'll call it for the day." Charme: "Well, eitha whey, don' worry t'much 'bout 'im."
- The cast is Recette, Tear, Charme and the blond man. ESC Event Skip is offered.

**The Pub, second visit**
- Script ids 16 / 4. "It looks like nobody's here..." (the same script as the empty Chapel, which is 16/5).
- Short: dlg ends at f3751, map at f3768.
- **The Pub has no menu**, in either visit.

**Pictures:** Pub f1404, Charme f2863, and a strip of f1300–3700 every 300 frames: `runs/pub-evening-week3/view/strip_1300_3700.png`.

**Map cursor moves:** the arrow keys follow screen layout, not the grid at 0x5fd620 as I read it.
- 0 →U 2 →U 5 →L 4
- 4 →R 5 →D 2 →D 0
- 0 →L 1, and 2 →D 0
- After a visit the cursor sits on that place, but its grid position is not the obvious one: from the Market, R went to 2.
- Input is ignored for about 20 ticks after the map opens.

**State I named:** none new. I used `stime`, `wm_cur`, and raw reads of 0x9643588 and 0x5c7a2c / 0x5c7a30. I suggest shared vars for `wm_state[7]` and for the event script id pair.

**Tool gaps**
- `rd eval` has no list comprehension, so I had to type out each of the 7 enable states by hand.
- `rd text` only shows the current frame, and pages go blank between lines. I needed several stops to catch the lines; a text log over a frame range would help.
- The map box ("will take N periods") and the door prompt are sprites, so `rd text` cannot read them.
