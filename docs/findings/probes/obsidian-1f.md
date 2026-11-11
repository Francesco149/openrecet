# Probe: obsidian-1f

- **Run:** `obsidian-1f` · **start:** save:824053720e01 · **task:** `tools/drive/tasks/obsidian-1f.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 108 turns, $1.63, 44 min
- **Scenario:** `tests/scenarios/obsidian-1f/` (9659 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/obsidian-1f.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     obsidian-1f                          f1676 FUN_00442cef < FUN_004536cb < FUN_0047be92 ⇄ - (+1)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

Run **obsidian-1f**: 9,659 frames, from save:824053720e01. It ends on Obsidian Tower 3F with control (floor = 2, dungeon id s32(0x438b4c8) = 3, lock = -1, hp 110/197). `rd verify` gives **MATCH** (fresh headless replay, frame 9659, rng 3da73351, sem 5e67b1061fbaa0ab). **`rd check` never ran:** it needs an extra session and the box was at its cap of 10 sessions all three times I tried, even after I stopped mine. So the step frames below come from the live --until/eval readings, not from check.

**Step frames**
1. In the Adventurers' Guild (scene 11): f384.
2. On 1F with control: f985. Id = 3, s32(0x438b4dc) = 12, dlg 0, lock -1. Departure was at f660.
3. 2F (floor 1, lock -1): f6137.
4. 3F (floor 2, lock -1): f9659.
- Forbidden state: I never saw the title after f137 (scene stayed 1/8/11), but check did not confirm this.

**Route**
- `.*60 A .*30 A .*120`: in the shop at f137.
- Search to the door: scene 8 at f335.
- Town map `D .*7 D .*7 D .*7 R .*7`: wm_cur = 6. Then `A`: guild at f384.
- Hire screen: the cursor starts on "Level:40 Swordsman Louie". The list is Leave / Louie / Charme / Caillou / Tielle, all level 40.
- `A` (Louie) goes straight to the item screen ("Pick the items you wish to bring with you…", Depart! and slots 1–5). Another `A` closes that help box; then `U A` on Depart!. **B on the item screen goes back to hiring.**
- Dungeon list (rd text): Hall of Trials, Jade Way, Amber Garden, Obsidian Tower, Lapis Ruins.
  - The map cursor is a sprite; I found it at **u32(0x6a49b50)** (index 0..4; it starts on 1, Jade Way). `R R` → 3, then `A`.
- Floor rows shown: Floor 1~5, 6~10, 11~15, 16~20, 21~25, 26~30, 31~35, every time "--:--:--". `A` on the first row departs at once (f660, fee 3,950 pix).
- In the dungeon, "Equip items in bag?" Yes/No appears at about f700 and holds lock at 0. It is a sprite, so rd text shows nothing. `A` (Yes) gives control 11 ticks later.

**Dialogue / cutscenes on this crafted save:** no first-trip cutscene and no dialogue played. The only boxes were the equip prompt and "Go to the next floor? Yes/No" on each portal. The floor ticker read "Consumables now half as effective!" on 1F and 2F.

**Floors**
- **1F** (f985–6137):
  - 33 actors at the start: types 53 ×6, 2 ×4, 1 ×3, 0 ×3, 45 ×4, 17 ×3, 44 ×5, 7 ×5.
  - HP 63 / 63 / 84 / 63 / 59 / 63 / 50 / 71. HP = table × 4.2, consistent with level 16. **The enemy HUD in the pictures says Lv17.**
  - Four more type-2 actors (slots 33–36) appeared mid-floor.
  - Killed one each of types 53, 2, 1 and 0. Types 45, 17, 44 and 7 were behind walls on 1F.
  - Portal found at about (-79, -240). Our HP went 197 → 117.
- **2F** (f6137–9659):
  - 22 actors: types 44 ×2, 7 ×2, 53 ×3, 17 ×3, 1 ×2, 2 ×2, 45 ×4, 0 ×4.
  - Killed types 17, 7 and 44. **Type 45 was never killed.**
  - Movement did nothing for about 360 ticks after the floor start, even though lock read -1.
  - Portal found at about (-199.5, -79.4). HP 117 → 110.
- **3F:** reached, not explored. I did not try 5F's boss.

**Game facts**
- **ex/ez (object at 0x695ef78) is not the exit in dungeon 3.** On 1F it read (-135, -23.6), behind the north wall of a dead end, while the real portal was at (-79, -240). Scored `-exitd` searches all ended in that dead end. The exit object needs a new address for d3.
- Navigation that worked: a score-free novelty search with goal "portal > 0", cell (int(px/10), int(pz/10)), walks of 24 and 48 ticks in 8 directions, avoid hp < 80. It found the portal in 62 s (1F) and 92 s (2F), each with 1 session.
- Combat: Louie (lv 40) does about 26–33 per sword hit; enemies took 2–3 hits.
  - What worked: `{dpad(tx-px,tz-pz) | (A if hypot(..)<3.4 and frame%4==0 else 0)}*N`. With a threshold of 6 it mostly missed.
  - Type 53 took a long time to finish by sword; the spin (B, 5 SP) ended it.

**Tool gaps**
- `rd check` needs an extra session, so a full box blocks it even when your own session is stopped. `-j 2` also fell back to 1 session for the same reason.
- No dungeon exit variable that works in d3.
- Named actor variables only cover slots 0–5; for higher slots I wrote raw f32(0x76b970+i*0xba4+…) expressions.
- Sprite prompts (equip, map cursor) are invisible to rd text.

The 1F strip (f985–2665, `strip_985_2700.png`) shows the tower floor, the fights and the "1F" HUD.
