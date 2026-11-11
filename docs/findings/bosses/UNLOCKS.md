# Unlock state — dungeons, floor rows, adventurers, the story bytes the dungeons read

**code-read** (IDA `all_ida.c`; the two base saves decoded). Nothing run in retail; the `craft_pix_save.py` lines
were dry-run into a scratch store only (the tool accepts them; the game has not loaded the results).

Bank = the slot's 0x2dfc8-byte record; "dword D" = bank offset / 4 = what `craft_pix_save.py --set D=V` takes.
Flag bytes are byte-packed four to a dword (little-endian: byte at offset 4D = bits 0..7). `--set` writes the whole
dword, so each value below states all four bytes; "base" = their value in `64d43650865f` AND `a23d9e447eb4` (both
decoded: every dword listed here is 0 in both unless noted).

## 1. Dungeons selectable (guild load `FUN_0045bdc2`, IDA 59792..59803)

Dungeon count `DAT_06a49b74` — later tests override earlier ones (not cumulative):

| count | dungeons on the map | condition | byte | dword / value |
|---|---|---|---|---|
| 2 | 0, 1 | always | — | — |
| 3 | + 2 Amber Garden | bank+0x2bcb9 ≠ 0 | 0x2bcb9 | 0xaf2e = 0x00000101 (base: already; +0x2bcb8 is its trigger byte) |
| 4 | + 3 Obsidian Tower | bank+0x2bce1 ≠ 0 | 0x2bce1 | 0xaf38 (see §4 for the other three bytes) |
| 5 | + 4 Lapis Ruins | bank+0x2bcf3 ≠ 0 | 0x2bcf3 | 0xaf3c = 0x01010000 (+0x2bcf2 = event 12/2 seen, +0x2bcf3) |
| 6 | + 5 Crystal Nightmare | bank+0x2bc53 ≠ 0 (Arma on the roster) **and** bank+0x2bd00 == 1 (Lapis 100F cleared) | 0x2bc53, 0x2bd00 | 0xaf14 byte 3; 0xaf40 = 0x00000001 |

Natural writers: +0x2bcb9 — Charme (customer 3) in the shop with +0x2bcb8 == 1 and Charme hirable, event 3/7 (IDA
65624); +0x2bce1 — customer 2 in the shop with +0x2bce0 == 1, event 7/5 (65640); +0x2bcf3 — day dword 0xb0fb > 35,
all of ids 0..6 on the roster or carded, +0x2bcf2 set, event 12/3 (49590..49621; 12/2 sets +0x2bcf2 first);
+0x2bd00 — Lapis floor 99's last timeline step (`FUN_0044f13d` case 23, tick 300), which also sets +0x2bd02 the
first time; +0x2bc53 — entering the Adventurers' Guild map destination with +0x2bd02 set: event 12/20, item 3408
added, +0x2bc53 = 1 (`FUN_0049e163`).

Map cursor: left / right over 0..min(count−1, 4); down → dungeon 5 when count == 6 (`adventurers-guild-RE.md`).

## 2. Floor rows (`DAT_06a49b2c[d]`, IDA 59769..59790)

`rows = (deepest[d] + 1)/5 + 1`, `deepest[d]` = dword **0xb101 + d** (0-based floor of the deepest checkpoint
cleared; written by the checkpoint-clear code with `0x438b4c8` as index, so Boss Rush clears write `deepest[5]`).

- d = 0..4: capped at `cap[d]` = {1, 3, 6, 10, 20}. Then, d > 0: if best-time dword `0xb10a + 20·d + rows` ≠ 0 →
  rows + 1. At `rows == cap` that dword is the last group's best time (`0xb10b + 20·d + cap − 1`), and the extra row
  is "Boss Rush". (Below the cap the test reads the best time of the first group not yet reached; 0 in a played save.)
- d = 5: `min(rows, 4)`; row r starts at floor 10·r (rows 0..2 "Floor 1～10 / 11～20 / 21～30", row 3 "Boss Rush" =
  floor 30, the 29 boss floors + the end floor).
- Depart (`FUN_0045c051`, IDA 59955..59990): d5 → floor 10·row; row < cap → floor 5·row; row == cap → dungeon 5,
  `0x438b4d0 = d`, floor 30 / 33 / 38 / 46.

| goal | dword | value | base 64d4 / a23d |
|---|---|---|---|
| d1 all rows | 0xb102 ≥ 9 | — | 14 / 14 (have) |
| d1 Boss Rush | 0xb121 ≠ 0 | — | 48407 (have) |
| d2 all 6 rows ("26～30" last) | 0xb103 ≥ 24 | 29 | 0 / 19 |
| d2 Boss Rush | 0xb138 ≠ 0 | 1 | 0 |
| d3 all 10 rows | 0xb104 ≥ 44 | 49 | 0 |
| d3 Boss Rush | 0xb150 ≠ 0 | 1 | 0 |
| d4 all 20 rows | 0xb105 ≥ 94 | 99 | 0 |
| d4 Boss Rush | 0xb16e ≠ 0 | 1 | 0 |
| d5 all 4 rows | 0xb106 ≥ 14 | — | 32 / 32 (have: the d1 Boss Rush clear wrote it) |

A best time of 1 tick draws as 0:00:00 on the row (cosmetic). The guild restores its cursors from dwords 0xb37f
(dungeon), 0xb380 (row), 0xb37e (adventurer id); stale values are clamped (IDA 60268..60280).

## 3. Adventurers

### Hirable (`FUN_0047f1ce`)

Id k is listed when bank+0x2bc4c+k ≠ 0 (joined by story) or bank+0x2bd18+k ≠ 0 (true card: set on a New Game+ carry
for each bag item 3401+k, IDA 113766..113774).

| k | name (0x5cc680 + 128k) | fee base (+0x28) | +100·level (+0x2c) | byte | natural writer |
|---|---|---|---|---|---|
| 0 | Louie | 50 | 100 | 0x2bc4c | guild intro (`FUN_0044c7c3`) |
| 1 | Charme | 100 | 100 | 0x2bc4d | shop event 3/5 (IDA 65616) |
| 2 | Caillou | 200 | 100 | 0x2bc4e | IDA 65901 |
| 3 | Tielle | 300 | 100 | 0x2bc4f | IDA 65741 |
| 4 | Elan | 500 | 100 | 0x2bc50 | IDA 65761 |
| 5 | Nagi | 500 | 100 | 0x2bc51 | shop event 8/6 (IDA 65665) |
| 6 | Griff | 1000 | 100 | 0x2bc52 | shop event 7/10 (IDA 65694) |
| 7 | Arma | 2000 | 100 | 0x2bc53 | guild map event 12/20 (`FUN_0049e163`) |

- dword **0xaf13** = bytes 0x2bc4c..4f (ids 0..3): base 0x00000101 → all four: **0x01010101**.
- dword **0xaf14** = bytes 0x2bc50..53 (ids 4..7): base 0 → all four: **0x01010101** (ids 4..6 only: 0x00010101).
- Fee flag bank+0x2bc81 (dword 0xaf20 byte 1): already 1 in both bases. Fee = base + 100·level, halved at merchant
  level dword 0xb100 ≥ 20; charged at Depart; the list refuses when gold < fee (row 1 excepted, IDA 60286).

### Record (dword **0xb3ac + 27·k**, 108 bytes; IDA `unk_4510648`)

| dword | meaning |
|---|---|
| +0 | level (0-based; fee, stats, skills read it). chara.txt start levels: Louie forced 0, Charme 8, Caillou 10, Tielle 20, Elan 15, Nagi 15, Griff 30, Arma 1 (new game copies the parsed table value, `sub_48ffd9`-side loop IDA 103067..103088; the saves show Charme 7, Caillou 9, Tielle 19, Elan 14, Nagi 14, Griff 29, Arma 0 = file value − 1, so the chara.txt parser stores LV − 1 — parser not read; Louie 14 / 25 from play) |
| +1..+5 | equipped items, `id << 6 | 0x20` or −1 (weapon, armour, …) |
| +6..+10 | the default set (`DAT_005cf788`, 5 per id): Louie 1 / 1301 / 1501 / 2301; Charme 101 / 1201 / 1401 / 2401; Caillou 201 / 1101 / 1401; Tielle 301 / 1201 / 1401; Elan 501 / 1001 / 1401 / 2001; Nagi 401 / 1001 / 1401 / 2101; Griff 601 / 1201 / 2201 / 2401; Arma 701 / 1201 / 1601 |
| +11..+14 | AT, DF, MA, MD — `ftol((L100 − L0)·level/100 + L0)` (`FUN_0047a8c0`, all 8 records at once) |
| +15 | words: max HP, HP bonus | 
| +16 | words: max SP, SP bonus |
| +17 / +18 / +19 | EXP / this level's base / next level's threshold `150·n·(n+1)` (`FUN_0048a331`) |
| +20 | trips; +21..+23 counters |
| +24, +25 | five skill-slot bytes (0x01010101, 0x01) |

- **Stats, HP / SP and the EXP thresholds are recomputed from the level dword at every Depart** (`FUN_00435c98`
  → `FUN_0047a8c0`, `FUN_0048a331`; IDA 37345..37360). So a crafted level needs only dword +0. The guild's panel shows
  the stale stats until the first Depart.
- Level-up stops at level ≥ 98 (`FUN_0048a383`).
- Skills by level (`DAT_005c5060`, count then thresholds): Louie 0, 9; Charme 0, 9, 19; Caillou 0, 4, 9, 24, 29;
  Tielle 0, 4, 19, 24; Elan 0, 9, 19; Nagi 0, 9, 19, 29; Griff 0, 9, 19; Arma 0.
- Equipment: both bases hold every id's default set already (all 8 records initialised at new game).
- Level dwords: Louie 0xb3ac, Charme 0xb3c7, Caillou 0xb3e2, Tielle 0xb3fd, Elan 0xb418, Nagi 0xb433, Griff 0xb44e,
  Arma 0xb469.

## 4. Story bytes the dungeons' setups / events read

Mode dword **0xb759** (IDA `v0 + 46937`): `== 2` skips every dungeon dialogue below and re-arms every boss (0 in both
bases; 2 is written by the title's mode pick, IDA 111821 — which menu entry: not traced).

Byte 0x2bc57 (dword 0xaf15 byte 3): ≠ 0 → every enemy spawns with HP × 0.1 (`FUN_0041f903`). 0 in both bases.

### Obsidian Tower (d3) — dword 0xaf38 = bytes 0x2bce0..e3, 0xaf39 = 0x2bce4..e7, 0xaf3e byte 0 = 0x2bcf8

| byte | set by | read by | effect |
|---|---|---|---|
| 0x2bce0 | shop event 7/4 | shop trigger of 7/5; town map marker (IDA 62452) | pre-unlock |
| 0x2bce1 | shop event 7/5 | guild | d3 selectable |
| 0x2bce2 | guild Depart to d3 row 0 while 0x2bce3 == 0 | `FUN_0045bcea`, `FUN_0044e08f`, inline events | **first trip = stage type 20 cutscene**: event 7/6 at tick 200, fade, at tick 460 clears itself, sets 0x2bce3, loads floor 0 |
| 0x2bce3 | that cutscene | guild Depart | 1 = no cutscene |
| 0x2bce4 | floor 29 timeline tick 150 | tick 120 | 0 → event 7/11 before Griff's 30F fight |
| 0x2bce5 | floor 29: all actors gone (stage 3 → 4) | `FUN_0044e08f` | 1 → **no Griff on 30F** (event stage 8, portal open); then event 7/12 once at stage 5 |
| 0x2bce6 | floor 49 tick 160 | tick 150 | 0 → event 7/7 before the 50F fight |
| 0x2bce7 | floor 49: all actors gone | `FUN_0044e08f` | 1 → **no Griff / hand on 50F** (stage 8, one portal) |
| 0x2bcf8 | floor 49 stage 5 tick 45 (with `deepest`, best time) | `FUN_0045bcea`, inline | stage 20 again: event 7/14 at tick 200, tick 300 → leaves the dungeon (`0x438be98 = 3`) and clears itself |

Natural first entry = the 7/6 cutscene; natural 30F / 50F = dialogue, then the fight. To fight Griff the "beaten"
bytes (0x2bce5 / 0x2bce7) must be 0.

### Lapis Ruins (d4) — dwords 0xaf3d (0x2bcf4..f7), 0xaf3f (0x2bcfc..ff), 0xaf40 (0x2bd00..03), 0xaf44 byte 3 (0x2bd13), 0xaf45 byte 0 (0x2bd14)

| byte | set by | effect |
|---|---|---|
| 0x2bcf4 | floor 9 after the boss | 0 → fade + event 10/4 (`FUN_00450877`) |
| 0x2bcf5 | floor 29 after the bombs | 0 → **leaves the dungeon into a story scene**: `FUN_00474d92`, 0x2bcfd = 1, stage = dword 0xb378, mode 1 |
| 0x2bcfd | that exit | (reader not traced) |
| 0x2bcfe | (writer not traced; the story scene's return) | ≠ 0 → floor 29 spawns no bombs, portals placed; the event raises them at tick 60 and clears it. Also "no boss is placed" in the generic placement (`boss-floor-common.md`) |
| 0x2bcf6 | floor 49: all actors gone | marker only |
| 0x2bd13 | floor 49 stage 5 | 1 → **no Arma on 50F** (stage 7) |
| 0x2bcf7 | floor 59 after the boss | 0 → event 12/4 |
| 0x2bd14 | **no writer found** | 0 → floor 79 plays 12/10, 12/11, then 12/12 when the player comes within 4.0 of 親父 (no fight) |
| 0x2bcff | floor 99 stage 4 tick 350 | 0 → the long intro (events 12/8, 12/9, 12/13, 12/14, 12/15); 1 → short path (stage 4 at tick 130 → stage 8) |
| 0x2bd00 | floor 99 stage 23 tick 300 | 1 → **no Arma golem on 100F** (stage 24, return portal only); opens d5 with 0x2bc53 |
| 0x2bd02 | same step, first time | 1 → next visit to the guild map destination plays 12/20 and adds item 3408 |

Floor 49's pre-fight event 12/6 and floor 99's 12/16..12/19 have no skip byte (only mode 2).

### Random encounters (`FUN_00431d54`, IDA 32660..32677)

Non-checkpoint floors, mode ≠ 2: fee flag set, 0x2bcc3 == 0, 0x2bd07 == 0, day ≥ 8, `rand % 10 == 0` → the type-31
special; day > 9, **0x2bcf3 == 0**, dungeon < 3, 0x2bd3c == 0, `rand % 10 == 0` → the type-30 special. Setting 0x2bcf3
(d4 open) removes the second from dungeons 0..2.

## 5. Craft lines (base `64d43650865f` = bank 19, day-15 morning, 470,000 pix)

```
B=tests/scenarios/_saves/64d43650865f1430ee0fd18d0d6e7430aece98ed3aa991570ea76faaa0d3f88c.sav.gz
```

**(a) every dungeon 0..4 and every floor row open, the fights intact, first-trip cutscene and pre-fight dialogues off**

```
python3 tools/craft_pix_save.py --base $B \
  --set 0xaf38=0x01000101 --set 0xaf39=0x00010001 --set 0xaf3c=0x01010000 \
  --set 0xaf3d=0x01000101 --set 0xaf3f=0x01000000 \
  --set 0xb103=29 --set 0xb104=49 --set 0xb105=99 \
  --set 0xb138=1 --set 0xb150=1 --set 0xb16e=1
```

- 0xaf38: 0x2bce0 = 1, 0x2bce1 = 1 (d3 open), 0x2bce2 = 0, 0x2bce3 = 1 (no 7/6 cutscene). Natural variant
  (cutscene on the first row-0 trip): `0xaf38=0x00000101`.
- 0xaf39: 0x2bce4 = 1, 0x2bce6 = 1 (7/11, 7/7 off), 0x2bce5 = 0x2bce7 = 0 (Griff present). Natural: leave 0.
- 0xaf3c: 0x2bcf2 = 0x2bcf3 = 1 (d4 open).
- 0xaf3d: 0x2bcf4 = 1 (10/4 off), 0x2bcf5 = 1 (no story exit on 30F), 0x2bcf6 = 0, 0x2bcf7 = 1 (12/4 off).
- 0xaf3f: 0x2bcff = 1 (100F short intro).
- 0xb103 / 0xb104 / 0xb105: deepest floors → all rows; 0xb138 / 0xb150 / 0xb16e: Boss Rush rows of d2 / d3 / d4.
- d5's four rows are already open in the base once d5 is selectable.

**(a′) the same + Crystal Nightmare selectable** — add `--set 0xaf40=0x00000001` and Arma's roster byte
(`--set 0xaf14=0x01000000`, or (b)'s 0xaf14). Cost: 0x2bd00 = 1 removes the Arma golem from Lapis 100F (it stays on
d5 floor 58 and on the last floor of Lapis's Boss Rush). Use (a) for the 100F fight, (a′) for d5.

**(b) every adventurer hirable, level 40 (0-based 39)**

```
python3 tools/craft_pix_save.py --base $B \
  --set 0xaf13=0x01010101 --set 0xaf14=0x01010101 \
  --set 0xb3ac=39 --set 0xb3c7=39 --set 0xb3e2=39 --set 0xb3fd=39 \
  --set 0xb418=39 --set 0xb433=39 --set 0xb44e=39 --set 0xb469=39
```

Fees at level 39: 3950 (Louie) .. 5900 (Arma). (a) and (b) combine into one line (dry-run: accepted, 22 dwords +
checksum). From `a23d9e447eb4` (bank 20, day 22 night, 9.69 M pix, `stime` 3: burn the period and sleep first) the
same lines apply — its 0xaf13 / 0xaf14 / 0xaf38.. are identical; 0xb103 is 19 there.

## 6. Not safe / not craftable by `--set`

- **d5 selectable and the Lapis 100F fight in one save**: both hang on 0x2bd00 (§5 a′).
- **Bytes not listed here**: `--set` replaces four flags at once; the values above assume the other bytes of
  0xaf38 / 39 / 3c / 3d / 3f / 40 / 14 are 0 — true for the two bases only. Re-read the dword before reusing a line
  on another save.
- **Story continuity**: the shop / town triggers of the join events (3/4, 3/5, 3/7, 7/4, 7/5, 7/9, 7/10, 8/5, 8/6,
  IDA 65600..65910) key on their own "seen" bytes, not on the roster — they still fire later in a crafted save
  (an unexpected cutscene in a shop session, not in a dungeon). The day > 35 trigger of 12/2 / 12/3 is off with
  0x2bcf2 / 0x2bcf3 set.
- **The type-20 cutscene stage** (0x2bce2 / 0x2bcf8): `FUN_0042df55`'s list for a stage index ≥ 20 is unresolved
  (`ROSTER.md` §4); the 50F clear always goes through it (0x2bcf8). Prefer 0x2bce3 = 1 for the first trip.
- **0x2bcfe / 0x2bcfd** (Lapis mid-story resume): writer / reader not traced — leave 0.
- **Arma hired before 12/20**: the roster byte alone lists her; item 3408 is not in the bag (only the true-card
  flag reads it). Hiring Griff (id 6) against Griff's floors, or Arma against type 41: nothing in the setups tests
  the hired id (only `FUN_0044cbe4`'s voice line), story sense aside.
- **Level**: stats are stale in the guild panel until Depart; EXP (+17) stays below the new base (+18) — the next
  level then needs the full `150·n·(n+1)` total.
- Merchant level, day and gold are untouched; nothing above depends on them.
