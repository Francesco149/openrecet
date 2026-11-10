# Probe: guild-fusion-confirm

- **Task:** `tools/drive/tasks/fusion-confirm.md` · **start:** end:dungeon3-run2-save
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 93 turns, $2.03, 11 min
- **Scenario:** `tests/scenarios/guild-fusion-confirm/`
- **Verdicts (2026-11-10):** check PASS (after the task's step-5 predicate was fixed); verify MATCH; port triage IDENTICAL f1–1083

The driver's report, as written (retail facts observed live through `rd`; treat code-read claims marked as such as leads):

---

**Run `fusion-confirm`: 783 frames, started from end:dungeon3-run2-save. The fuse worked: Rank 1 Helm (list index 15) became Knight's Helm+2. `rd verify` gives MATCH (frame 783, rng 6052575a). `rd check` FAILS on step 5 only, because that step's predicate can never be true in retail (details below).**

**Steps (frames from `rd check`):**
- Step 1, in the Merchants' Guild (scene 6): frame 575. `DAT_0963c5f0` = 0, so this is the guild variant.
- Step 2, Fusion list takes input (mode 0, c24 = 25): frame 635. The guild menu order is Buy, Sell, Fusion, Talk, Leave (5 options), so Fusion is c04 = 2. The list has `bf4` = 22 recipes.
- Step 3, mode 5: frame 671. The A press on index 15 is at f670; the "Fusing Helm. / Continue? / Yes No" box is up from about f690. "Helm" is the placeholder name because this recipe had never been fused.
- Step 4, mode 6: frame 702. Yes was pressed at f692 and the box closes before mode 6 starts.
- Step 5 never holds. `bfc` counts 1→60 by frame 762, the A press there takes it 61→74 (frames 763–775), and at frame 776 the mode becomes 0 with `bfc` = 0 on the same tick. Retail clears `bfc` when it goes back to mode 0 (the port's `scene_guild.c` line 919 does the same), so `c00 == 0 and bfc >= 61` is never true. The run reaches the state the step means: back on the Fusion list at frame 776. Suggested predicate: `s32(0x9642c00) == 0 and frame > <mode-6 frame>`, or watch for the change 6→0.

**Why the bag could not fuse anything as saved:** none of the 22 recipes had every ingredient in the bag. Every missing ingredient is a monster or dungeon material (item ids 3xxx/4xxx). Their item records have the guild (ギ) and market (市) stock bytes at 0, so neither shop sells them. One ingredient was in the shop instead of the bag: Slime Liver+2 (item 3106) sat on the centre display table, grid cell 83 (`0x44f717c`).
- I stood so the highlighted cell was col 3, row 4 (`0x438cbfc` = 3, `0x438cc00` = 4), at px −3.06, pz 2.86, frame 268.
- A opened the display menu (`cc04` = 1). I moved up to "Nothing" and pressed A at about f306; the item went into the bag (items 32→33, frame 313).
- Then I walked to the door (3.0, −3.4, facing right), went to the town map (scene 8, frame 531) and picked the guild (map cursor `0x9643684` = 3).

**What was fused:**
- Recipe index 15, output item id 1704 (Knight's Helm), output +2 (`bf8` = 2).
- Ingredients: Warrior's Helm 1702 (+0), Thankful Statue 3002 (+0), Slime Liver 3106 (+2). The plus starts at 15, drops to 13, so the output gets 15 − 13 = +2.
- The banner reads "Fused / Knight's Helm+2!". The description panel shows sell price 9,000.

**Gold and items:** gold stays 9,086 throughout; fusion costs nothing. Items: 32 at load, 33 after taking the Liver off the shelf, 31 after the fuse (three used, one gained).

**RNG in mode 6:** rng is 0x76a8bc73 before mode 6 and 0x76a8bc74 at f703–704 (`bfc` 1–2). The table-A burst at `bfc == 2` (SE 0x17b) then moves it to 0x6052575a, first seen at f705. It stays there through the return to mode 0. Nothing else in mode 5 or mode 6 draws.

**Strip:** `/mnt/ephemeral/openrecet/drive/runs/fusion-confirm/view/strip_690_770.png`. f690 shows the confirm box with the cursor on Yes. f706–738 show the sparkle burst and the banner growing. f754 shows the "Fused / Knight's Helm+2!" banner with the helm icon. f770 shows the banner sliding off. In the list, "Rank 1 Helm" is renamed "Knight's Helm".

**Checkpoints:** fusionlist (f416, from an earlier branch that was rewound, so it is not on this movie's path), removed (f313), onhelm (f670), fused60 (f762).

**Game facts learned:**
- Colours of recipe rows in the Fusion list: 0xff7fdf7f = fusable, 0xff7f7f7f = all ingredients seen but some short, 0xff5f5f5f = an ingredient not yet seen.
- The recipe ingredient fields hold item-DB indices. To compare with bag entries, convert each through the item record's `item_id` at +0x34 (records start at `0x95d37d0`, 0x2cc bytes each); bag entries are `item_id << 6 | plus`.
- `FUN_00469118` counts the item table at bank dword 6 only. Items on shop display (grid at bank dword 0x4e26 = `0x44f7030`) do not count.
- The shop door test is a distance check of under 1.8 from (3.0, −3.4) plus facing +π/2. Standing at x > 2.9 with a different z does nothing.

**Names worth sharing** (I defined the first two in this run): `gmode` = s32 @ 0x9642c00, `gcur` = s32 @ 0x9642c04. Also worth naming: `fcur` = 0x9642c30, `fscroll` = 0x9642c34, `ffx` = 0x9642bfc, `fplus` = 0x9642bf8, `wm_cur` = 0x9643684, display cell col/row = 0x438cbfc / 0x438cc00, `cc04` = 0x438cc04.

**What the tool lacked:**
- No string read for `eval` or `ram read`; I worked item names out from the hex dump's ASCII column.
- No list comprehensions or `hex()` in expressions.
- `--watch` output shows only the last 40 frames, so I had to rewind and replay to find the RNG burst frame.
