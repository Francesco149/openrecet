# Probe: lapis-100f-long-intro

- **Run:** `lapis-100l` · task `tools/drive/tasks/lapis-100f-long-intro.md` · **start (CRAFTED):** `save:15b20dc3c34e` =
  `lapis-81f-90f`'s end save `6809401b79a9` (shop, day dword 30 = HUD Day 31 morning, `stime` 0, Louie L46, slot 020, 58,580 pix)
  with bank dword `0xaf3f` 0x01000000 → 0, i.e. byte **`+0x2bcff` cleared** (`tools/craft_pix_save.py --base 6809401b79a9 --set
  0xaf3f=0`). At boot: `+0x2bcff` 0, `+0x2bd00` 0, `+0x2bd01` 0, `+0x2bd02` 0; the guild's Lapis list still shows "96～100 00:00:00"
  (the row comes from another byte, not `+0x2bcff`).
- **Goal:** the 100F LONG intro (floor 99 stages 0..3, events 12/8, 12/9, 12/13, 12/14, 12/15), which `lapis-91f-100f` could not
  reach (`+0x2bcff` was 1 there → short path). Then the golem, the clear, town, day end, Save.
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`). Floors: BFS + one scored search per cell (`-j 2`, `--hold 45
  --hold-no-goal`, zero HP slack) + a "leg stuck → kill the 4 nearest" fallback; golem: the `lapis-91f-100f` phase script
  (hit / dodge keyed on the hands' beam clock). **Host:** every session native on the Windows box (`RHOST_WIN=win-wifi`,
  `RD_HOST=win`); nothing under wine on maximus.
- **Movie:** 76,913 frames: Day 31 shop → guild (no Market) → one trip 96F → 100F → long intro → golem → events → CLEAR → town
  (event 12/20) → home ×2 (day end) → Day 32 morning event → Save. Filed as `tests/scenarios/lapis-100f-long-intro/trace.jsonl`.
  `rd check` **PASS** (steps at f20987 / 21082 / 27250 / 65298 / 74069). **No retail golden** yet (next: `rhost.py golden
  lapis-100f-long-intro`, `rhost.py triage lapis-100f-long-intro`).
- **Verdict (2026-11-14):** `rd verify lapis-100l` (headless, Windows) and `--real`:
  ```
  fresh headless replay: frame 76913 rng e060f521 sem 5f3989846fd0a697 dev d6a57c6b28db0898  (unskipped retail)  [win]
  live session:          frame 76913 rng e060f521 sem 5f3989846fd0a697 dev d6a57c6b28db0898  (skips 414ee2: dev not compared)  [win]
  MATCH
  real-device replay:    frame 76913 rng e060f521 scene 9  (Windows, Direct3D 8)
  MATCH
  ```
- **End save:** `save:b50b09b8aa6e` (slot 020 overwritten from the shop's pause menu at f76913: day dword 31 = HUD Day 32, `stime` 0,
  54,030 pix, SCORE 8414 → 8944, TIME 4:39:16 → 5:00:34).
- **Damage taken: none** (`rd scan` 27250..61493 `hp < 224`: 0 rows; nothing on 96F..99F either). Louie L46 the whole trip (no
  level-up; `lapis-91f-100f` got L47 on its 95F mob floor, which this trip skipped). SP 50 → 0 on 96F (kill searches' `B` spins).

## Before the dungeon

| frame | what |
|---|---|
| 172 | shop, Day 31 morning (`.*60 A .*30 A .*80`); control. Bank `+0x2bcff..+0x2bd02` all 0 |
| 295 | door by search → town map (`wm_cur` 0); `U U R U L R` → `wm_cur` 6 → `A` → guild (`scene` 11) |
| 462 | guild: Louie **L46** fee 4,550; Charme / Caillou / Tielle L40 |
| 462..1946 | hire `A A`; bag: Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots, Taiyaki ×6, Beef Bowl ×2, Watermelon ×2 (14/20). Storage food list: Apple ×13, Apple+3, Cherry ×2, Watermelon ×2, Beef Bowl ×2, Taiyaki ×6, Baked Yam (no Market trip) |
| 1946..2207 | `U` ×20 + `A` → plates → `A` → Lapis rows (… 86～90 00:03:23, **91～95 --:--:--, 96～100 00:00:00**), `D` ×3 → 96～100 |
| 2208 / 2224 | `A` → **96F**, fee 4,550 (58,580 → 54,030), `stime` 0 → 1 |
| 2424 / 2485 | "Equip items in bag?" (`equipq` counts up from 1 at 2424; an `A` before the box is drawn is eaten) → `A` → ATK 61 → 64, DEF 63 → 84, HP 224 |

## Per floor (96F..99F)

L44 table (96F..100F). Enemy lists = the actor table at nav start (`type × count (HP)`; total).

| floor (`floor`) | frames | enemies | grid, start → exit |
|---|---|---|---|
| 96F (95) | 2526 → 10656 | 8×4 (176), 9×3 (196), 10×2, 11×4, 12×6 (49), 14×5 (196), 15×6, 16×8 (196), 17×5, 18×3, 19×4, 20×2, 22×2, 36×5, 39×4, 40×2, 44×2 (118), 45×3 (137), 46×4 (176), 47×2 (343), 50×5, 51×4, 53×5; 90 | 11 × 11, (1, 5) → (6, 9). Banner **"This level is full of traps. Enjoy!"** (f2526) |
| 97F (96) | 10656 → 13808 | 8×2, 9, 10×2, 11×3, 12×2, 14×2, 15×7, 16×6, 17, 18×3, 19, 20×4, 22×3, 36, 39×2, 40×2, 44×2, 45, 46×2, 47×2, 50×4, 51, 53; 55 | 9 × 9, (5, 4) → (1, 3) |
| 98F (97) | 13808 → 17445 | 8×4, 9×2, 10×7, 11×2, 12×2, 14×8, 15×7, 16×6, 17×2, 18×2, 19×2, 20×4, 22×4, 36×3, 39×2, 40×2, 44×5, 45×4, 46×2, 47×2, 50×3, 51×2, 53×2; 79 | 11 × 9, (3, 3) → (8, 4) |
| **99F (98)** | 17445 → 20987 | **5×4 (98), 12×3, 14×5, 16×2, 17×5, 18×4, 19×8, 20×5, 35×3, 36×2, 48×5**; 46 (the 99F list again) | 9 × 9, (2, 1) → (3, 5); **event 12/21** at the exit |

- **96F traps / blockers:** twice a leg search "arrived" and every later search saw one cell (`found: false 1 cells`): a type-20
  (blue tentacle, HP 147) standing in the 1-wide corridor behind / in front of Louie (f3747, contact 224 → 186 on a probe step
  that was undone), and a pack (types 18 ×2, 11, 20 ×2, 47, 16) at cell (6, 7) (f5628). Fixed by killing the nearest actors
  (`--hold 30`, `--avoid "hp < H"`), then resuming the route. Zero damage in the kept movie.
- **99F exit → 100F:** event 12/21 on the portal (`+0x2bd01` 0 → 1 by f19407, `portal` 120, `floor` 98 throughout); same lines as
  `lapis-91f-100f` (not re-transcribed). `dlg` 1 → 0 f20970; **100F loaded f20987** (`floor` 99).

## 100F: the long intro (stages 0..4, `+0x2bcff` == 0)

Stage = `s32(0x438b8a0)` (`est`), tick = `s32(0x438b8a4)` (`etk`; frozen while an event's dialogue runs). Seen bytes = bank
`+0x2d670 + event id` (dword `0x4510e10` / `0x4510e14` with bank `0x44e3798`). `rd ram mark/diff` over `+0x2bcf0..+0x2bd20` and
`+0x2d600..+0x2d700` across each event: only the bytes listed changed.

| frame | `est` / `etk` | `lock` / `dlg` | what |
|---|---|---|---|
| 20987 | 0 / 0 | −1 / 0 | floor load: Louie at (−40, −73); props **Alouette 30 at (−38, −46), Arma 41 at (−42, −46)** (actor slots 0 / 1, read f20988); crystals scattered around. Free walk, x clamped |
| 21047..21081 | 0 / 60..94 | −1 / 0 | `U` (+z) walk; pz −73 → −67.8 |
| **21082** | **0 → 1** / 1 | −1 / **1** | `pz` −66.95 (> −67) → **event 12/8** (`sid` 12008). Seen byte **`+0x2d678`** 0 → 1 |
| 21510 | 1 / 1 | −1 / 0 | 12/8 over; stage 1 tick runs (camera drifts back) |
| **21630** | 1 / 121 | **0** / **1** | tick 120 → **event 12/9** (`sid` 12009); `s32(0x438b4e8)` 1. Seen byte **`+0x2d679`** 0 → 1 |
| 22454 | 1 / 121 | 0 / 0 | 12/9 over |
| **22534** | **1 → 2** / 1 | 0 → **4** | tick 200 → stage 2: scripted walk (`lock` 4, no input needed), pz −66.15 → −54.85 at 0.175/tick |
| **22600** | **2 → 3** / 1 | 4 / **1** | `pz` −54.85 (> −55) → **event 12/13** (`sid` 12013). Seen byte **`+0x2d67d`** 0 → 1 |
| 24359 | 3 / 1 | 4 / 0 | 12/13 over; **f24360 `lock` 4 → 0**; stage 3 runs: camera low, orbs swirl into the crystal (pictures f24420 / 24500) |
| **24559** | **3 → 4** / 1 | 0 / **1** | tick 200 → **event 12/14** (`sid` 12014). Seen byte **`+0x2d67e`** 0 → 1 |
| 25339 | 4 / 1 | 0 / 0 | 12/14 over; stage 4 runs: fade at tick 10 (f25348) |
| **25469** | 4 / 131 | 0 / 0 | tick 130: `u32(0x9643128)` 0 → 1; **golem spawns** (73 body, 74 core, 75 / 76 hands; 4 actors), Arma prop freed (`actors(T == 41)` 1 → 0); party re-placed **(−40, −54.3) → (−40, −50)**; Alouette on Tear's spot |
| **25689** | **4 → 8** / 1 | 0 / **1** | tick 350 → **event 12/15** (`sid` 12015); bank **`+0x2bcff` 0 → 1** (dword `0x450f494` 0 → 0x01000000); seen byte **`+0x2d67f`** 0 → 1 |
| 27010 | 8 / 1 | 0 / 0 | 12/15 over; stage 8 (240 ticks, no dialogue) |
| **27250** | **8 → 10** | 0 → −1 | the fight. Slots: 0 Alouette (30) at (−38.2, −52); 1 body 73 **2940**; 2 core 74 980; 3 / 4 hands 75 / 76 980 at x −32 / −48 (all y 1.0, `unhit` 1 on the first tick) |

### Event text (speaker = nameplate in the picture; x = the text column `rd text` reports)

**12/8** (f21082..21510; Recette x 144, Tear x 200; Louie silent)

| frame | speaker | line |
|---|---|---|
| 21214 | Recette | Holy carp... |
| 21269 | Tear | So many power crystals... / and they are so massive... |
| 21324 | Recette | I wonder if someone could / use these to get more / powerful? |
| 21379 | Tear | You are, once again, / speaking utter foolishness. / DANGEROUS foolishness. |
| 21412 | Tear | No living creature could / use such crystals safely... |

**12/9** (f21630..22454; the camera cuts to Alouette and the green-haired golem pilot at the big crystal; Alouette x 218, Avall x 134)

| frame | speaker | line |
|---|---|---|
| 21762 | Alouette | Wh-what'd you just do?! |
| 21795 | Alouette | I DEMAND that you release / Prime! IMMEDIATELY! / NOW! |
| 21850 | Avall | You seem to be laboring / under the misconception / that you are in |
| 21883 | Avall | a position to demand / anything. Were I you, I'd / think before speaking. |
| 21993 | Avall | You really are quite / different compared to / your milquetoast father. |
| 22026 | Avall | I wonder if you take / after your mother... |
| 22081 | Alouette | What are you talking / about? |
| 22114 | Alouette | I... don't remember Mama... |
| 22213 | Alouette | ... wait a minute, don't / change the subject on me! / RELEASE PRIME! |
| 22268 | Avall | Hell's bells. What a / perfect little pest. / Shoo, before I... |
| 22356 | Avall | ... Ah. The rest of the / audience has arrived. |

**12/13** (f22600..24359; Recette x 160, Alouette x 144 / 286, Avall x 294 / 134, Tear x 276)

| frame | speaker | line |
|---|---|---|
| 22732 | Recette | Avall! |
| 22765 | Recette | Let Arma go and leave / us alone! |
| 22853 | Recette | Er, hold on... |
| 22985 | Recette | Alouette? But... |
| 23018 | Recette | What're you doing here?! |
| 23073 | Alouette | Recette! Run! Run away! |
| 23128 | Recette | Huh? What d'you mean? |
| 23183 | Avall | Will you kindly refrain / from unnecessary intrusion / into my business |
| 23216 | Avall | just this once, girl? |
| 23326 | Avall | Ah. Now, welcome to the / Lapis Core, Mademoiselle / Lemongrass... and Tear. |
| 23381 | Tear | Sir... |
| 23414 | Tear | A power crystal like this / is... not something that / can be used safely. |
| 23502 | Avall | I wonder. Tell me: what do / you know of power / crystals? |
| 23557 | Recette | Uhhh. They come out of / monsters when you beat / them up and they make |
| 23590 | Recette | adventurers stronger? |
| 23645 | Avall | I suppose that would be / the limit of proletarian / knowledge on the subject. |
| 23678 | Avall | In truth, these are / nothing less than / crystallized life itself. |
| 23711 | Avall | The essence left in / the trail of fairies / encourages their growth, |
| 23744 | Avall | and so to rapidly / enhance the growth of / one... |
| 23799 | Alouette | NO! RECETTE! RUN, / DARNIT! |
| 23854 | Recette | Alouette? What are you- |
| 23909 | Tear | Nnngh! Wha- |
| (23909..24261) | | no text: Tear is drawn into the crystal (picture f24050: Tear's portrait eyes closed) |
| 24261 | Recette | Eh?... Tear?... / TEAR?! |

**12/14** (f24559..25339; Avall x 134, Recette x 216, Alouette x 286; the crystal flares)

| frame | speaker | line |
|---|---|---|
| 24713 | Avall | ... You absorb a fairy / into the crystal, using / the fairy's own soul |
| 24746 | Avall | to fuel its power. |
| 24801 | Recette | TEAR! |
| 24856 | Avall | I've spent decades / learning how to best / utilize fairies. |
| 24889 | Avall | Ironic that, for all / their attempts to / integrate into our |
| 24922 | Avall | society, they are most / useful in this state of / pseudo-death, no? |
| 24977 | Recette | Wh-wh... y-you... |
| 25032 | Avall | First that fairy "Prime", / and now dear little Tear, / useful at last. |
| 25065 | Avall | With two fairies behind / this crystal, it should / become a fountain |
| 25098 | Avall | of raw power. Power I / can use. |
| 25153 | Alouette | Gh!... |
| 25208 | Avall | Yes, with the souls of / those two fairies / powering the crystal, |
| 25241 | Avall | I should be able to do / anything I desire... / even this. |

**12/15** (f25689..27010): **no dialogue box and no `rd text` rows** — centred caption lines faded in over the golem (pictures
f25850..26950; drawn by a path the text hook does not see):
"By sealing the life-force of two fairies in this gem," / "and thus merging the golem with the empowered gem," / "I can create
anything I desire... even this." (stacked, ~f25850..26500), then "Now then. I believe I previously asked you... to depart."
(~f26700..26950, fading out with the scene).

## The golem, the clear, town
Fight **f27250 → f61463** (34,213 frames; short path: 27,315). Same live numbers as `lapis-91f-100f`: body 2940, core / hands 980.
~120 sword hits (body-HP change rows: 121), body state entries hover 0 ×61, lob 1 ×31, rain 2 ×29; zero damage kept.

| frame | seg / brk | body HP | what |
|---|---|---|---|
| 27250 | 0 / 0 | 2940 | stage 10 |
| 33124 | 0 → brk 1 | 2450 | break 1 |
| 39522 | brk (seg 1) | 1960 | break 2 |
| 44686 | brk (seg 2) | 1470 | break 3 |
| 49790 | brk (seg 3) | 980 | break 4 |
| 54700 | brk (seg 4) | 490 | break 5 |
| **61463** | 5 / 1, `+0x428` 2 | **0** | kill (clamp to 0 also sets `+0xa60`, as before) |
| 61467 / 61468 | | | stage 13 (`lock` −1 → 0) |

**New trap seen (abandoned branches): the body's hover landing.** In hover (`+0xa54` 0) the body sinks to y ≈ 0.05 around its tick
≈ 220 and rises again; at tick **≈ 237..239** anything within **~7** of its centre takes **142** (224 → 82, no hand involved; f27926 →
27929 and f49208 + 79 standing still, distance 6.93..7.0). A search that ends a hit with the body at hover tick ≥ ~190 and the
player within ~7 is doomed: no action from there avoids it, and `--hold 30` passes because the hit lands just after the tail. Fix
used in the script: goal also requires `not (state 0 and 186 ≤ tick ≤ 242 and dist < 13)`, avoid `state 0 and 226 ≤ tick ≤ 242 and
dist < 8.5`; and on a failed hit search, a dodge search to the next beam window (`hand tick ≥ 290`) before rewinding. (The card's
"contact with a patrolling hand 142" may partly be this landing.)

### After the kill (same sequence as the short path)

| frame | stage | what |
|---|---|---|
| 61467..61867 | 13 → 20 | fade, re-placement |
| 61897..62612 | 20 → 21 | **event 12/16** ("The barrier's fading!" … Recette x 190 / Alouette x 144, as in `lapis-91f-100f`) |
| 62672..64588 | 21 → 22 | **event 12/17** |
| 65298..66442 | 22 → 23 | **event 12/18** |
| 66443..66500 | 23 | `lock` 4 (walk) |
| 66692..68518 | 23 | **event 12/19** |
| **68571** | 23 tick 302 | bank `+0x2bd00` 0 → 1, `+0x2bd02` 0 → 1 (dword `0x450f498` 0x00000100 → 0x00010101); seen bytes `+0x2d680..+0x2d683` (12/16..12/19) 0 → 1; **`scene` 9 "CLEAR!"** + bag list 14/20 (nothing picked up) |
| 69512 / 69529 | | the bag list ignores `A` on an item row (cursor starts on row 1, not "Rearrange"); **`B` ends it**; town map, **`stime` 1 → 2** (short path: 1 → 3) |
| **69537..73994** | | **event 12/20 on the town map** (`sid` 12020; `+0x2bd02` 1 → 0, `+0x2bc53` = 1; ends "Let's go back to Recettear!" / "R-Recette..." / "Yes. Let's... go home."); control at `wm_cur` 6 |

## The return and the day end

| frame | what |
|---|---|
| 73994..74069 | `D D D` (6 → 3 → 0) → home (`scene` 1, `stime` 2) — task step 5 |
| 74278..74547 | door → map → `R` (3) → Merchants' Guild → `B` → `D` (0) → home: **`stime` 2 → 3 on entering home** (the visit itself changes nothing) |
| 74759..74954 | the same loop again: **`stime` 3 → 4** → f75114 Daily Profits (Profits −4,550 = the guild fee only; score 8414 → 8500 (+86)) |
| 75414 | day dword 30 → 31 |
| 75628..76562 | **Day 32 morning event** (`sid` 1017: Tear "Good morning." … "The final payment is due in a week. It will be... sizable.") |
| 76586..76913 | `~` → pause, `D` ×4 → `pm_sel` 4, `A` → slots (cursor on 020), `A`, `A` → saved (SCORE 8944, TIME 5:00:34); `rd endsave` → `save:b50b09b8aa6e` |

## Long vs short path (100F)

| | short (`lapis-91f-100f`, `+0x2bcff` 1) | long (this run, `+0x2bcff` 0) |
|---|---|---|
| arrival | stage 4 tick 130: golem spawns on the first tick, stage 8 one tick later (f32398 → 32400) | stage 0 at (−40, −73), Alouette + Arma props, free walk |
| before the fight | stage 8 only (240 ticks, no dialogue) | stages 0 → 1 → 2 → 3 → 4 → 8: events **12/8, 12/9, 12/13, 12/14, 12/15**; 6,263 frames from the load to stage 10 (short: 242) |
| `+0x2bcff` | stays 1 | 0 → 1 at stage 4 tick 350 (with 12/15) |
| seen bytes | `+0x2d685` (12/21) only before the fight | `+0x2d678`, `+0x2d679`, `+0x2d67d`, `+0x2d67e`, `+0x2d67f` (12/8, 12/9, 12/13, 12/14, 12/15) |
| party at stage 10 | (−40, −50) | (−40, −50) — identical; Alouette (−38.2, −52) in Tear's place |
| golem / fight / clear / 12/16..12/20 | — | identical structure and HP; stage-13 → CLEAR timing identical (61467 + 7104 = 68571; short 59959 + 6490 = 66449: the event dialogue lengths differ with tap timing only) |
| after CLEAR | town map `stime` 3 | town map `stime` 2 (one period earlier) |

## Tool gaps

1. **12/15's captions are invisible to `rd text`** (centred subtitle lines over the scene, no dialogue box): the transcript came
   from pictures. The caption draw path needs a text hook.
2. Doomed states after a held arrival (the hover landing at tick ≈ 238) again cost ~2 h of loops: the refine keeps returning the
   same shortest path into the same doom. A `--not-path` / diversity option or a hold that must survive a short *search* would
   avoid it (as in `lapis-91f-100f`).
3. `rd play --watch ... --changes` is not accepted (only `scan` has `--changes`).
4. Speaker names are only in the nameplate sprite; `rd text` gives the column (x) only, so every speaker needs a picture.
