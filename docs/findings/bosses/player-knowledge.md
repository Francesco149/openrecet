# Dungeons and bosses — player-documented knowledge (web survey, 2026-11-11)

**Status: PLAYER-DOCUMENTED claims, NOT verified against the engine.** Every section below is
what wikis / guides / forum posts say. Use as hypotheses for `rd` probes and task predicates,
never as parity evidence. Summarised in own words; source tags `[..]` resolve in §Sources.
Disagreements between sources are marked **DISAGREE**.

Coverage caveats:
- The Fandom wiki is the only deep source. GameFAQs (one walkthrough read) mostly agrees and adds tells.
- No source gives numeric Side/Back/Counter multipliers, i-frame counts, or a damage formula.
- No dungeon/boss speedrun notes exist publicly: speedrun.com has 2 categories, the one guide is a
  shop-routing doc (Google Doc, login-walled). TASvideos search: no entry.
- Not read: reddit boss-rush thread (blocked; search-engine summary only), writeups.org (403),
  GameFAQs guides 61659 / 60940 and most of 61005.

---

## 1. Dungeon list (player-documented, unverified)

| # | Dungeon | Floors | Unlocked by | Clearing unlocks | Src |
|---|---------|--------|-------------|------------------|-----|
| 0 | Hall of Trials | 5 | Day 2, after meeting Louie at the Adventurer's Guild | Louie's card (chest on 5F), Jade Way (no announcement, appears on next guild visit) | [W-Dungeon] [W-Hall] [GF] |
| 1 | Jade Way | 15 | Hall of Trials cleared | Charme as shop visitor → her card; Caillou + Elan + Tielle event chains | [W-Jade] [W-Charme] |
| 2 | Amber Garden | 30 | Jade Way cleared + Town Square scene (morning/noon, girl looking for sister) + Charme's shop scene announcing it | Tielle; midnight Tear scene → Griff chain | [W-Amber] [W-Order] |
| 3 | Obsidian Tower | 50 | Amber Garden cleared → midnight Tear scene → Griff at Town Square at night ×2 → Griff visits shop (evening/night, Dark atmosphere) → Louie visits shop and announces it | Griff as customer → his card. Last dungeon reachable before the debt is paid | [W-Obsidian] [W-Griff] |
| 4 | Lapis Ruins | 100 | Endless Mode only (debt paid) + all 7 other adventurers recruited → Arma scene next morning | Arma's True Card, long story scene, Survival Mode, Crystal Nightmare | [W-Lapis] [W-Arma] [W-Endless] |
| 5 | Crystal Nightmare | 30 + boss rush 31–59 (+60F chest floor) | Lapis Ruins cleared (no announcement) | — | [W-CN] |

Structure rules:
- Standard pattern: 4 random floors, then a fixed boss floor; exit door / continue choice only after the boss. Every 5th floor is static. [W-Jade] [W-Lapis] [TVT]
- Crystal Nightmare: 3 stages (1–10, 11–20, 21–30), exits only on 10/20/30, no bosses in the normal part. 30F has no enemies; reward chests = stages cleared in a row (1/2/3). As of v1.108 stages need not be cleared in order. Nearly every floor has a negative effect, never a positive one. [W-CN]
- **Boss Rush**: each dungeon gets one once every floor of it is cleared [TVT] [W-Lapis]. Sizes per the drop-table guide: Jade 3, Amber 5, Obsidian 8, Lapis 13, Crystal Nightmare 29; internally they are all Crystal Nightmare floors (Jade = 31–33, Amber = 34–38, Obsidian = 39–46, Lapis = 47–59, CN = 31–59). [SG-Drop]
- "Gauntlet" floors (mob swarm, kill all to proceed) replace a boss: Amber 15F; Obsidian 25F, 35F; Lapis 30F, 45F, 65F, 75F, 85F, 95F. Lapis 80F = cutscene only. [W-Gauntlet] [W-Lapis]
- Obsidian 30F has no boss once Griff is beaten (or possibly once he is unlocked). [W-Obsidian]
- A dungeon trip costs 2 of the day's 4 time periods (3 if another location is visited first). Guild closed at night and every 7th day. [W-Time] [W-Guild]
- Inventory: 20 items at start; 25 at Merchant Lv 17, 30 at Lv 24, 35 at Lv 40. On defeat you bring back 2 items at Lv 16, 3 at Lv 50 (base presumably 1; not stated). Adventurer fee halves at Lv 21. [W-Jade] [GF]
- Louie works for free until Hall of Trials is cleared, and never charges more than you have. [W-Hall] [W-Louie]

**DISAGREE (unlocks):**
- Amber Garden: [W-Dungeon] / [GF] / [SG-Unlock] require Charme's card; [W-Amber] / [W-Order] only need her shop scene.
- Lapis: "every Business Card except Arma's" [W-Dungeon] vs "recruited, True Cards not needed" [W-Lapis] (same thing, differently worded).
- Griff's night scenes: 2 [W-Obsidian] [W-Griff] vs 3 [GF].
- CN boss rush length: 29 bosses 31–59 [W-CN] [SG-Drop] [SR]; 27 bosses 31–57 [GF] (omits Drisby v3 and Golem v2); "thirty" [TVT].
- Survival Mode: unlocked by clearing Lapis [W-Survival], or by finishing the story in v1.105 / later versions [W-Survival] [SG-Unlock].

### Boss floors per dungeon

Level / HP as listed on the dungeon pages. HP is per body for multi-boss fights.

**Jade Way** [W-Jade] [GF]

| Floor | Boss | Lv | HP |
|---|---|---|---|
| 5 | The Crowned Slime ×1 | 5 | 180 |
| 10 | Reginald Drisby (crowbar) | 7 **or** 8 | 330 **or** 360 |
| 15 | Charme | 10 | 280 |

**Amber Garden** [W-Amber] [GF]

| Floor | Boss | Lv | HP |
|---|---|---|---|
| 5 | Eyebat King ×1 | 13 **or** 8 | 510 **or** 240 |
| 10 | The Insect King ×1 | 15 | 570 |
| 15 | Gauntlet floor (bombs, eyebats, slimes, knights, ropers) | — | — |
| 20 | Volcanicrab | 17 | 630 **or** 713 |
| 25 | The Crowned Slime ×2 | 18 | 440 |
| 30 | Tielle | 16 **or** 20 | 400 **or** 720 |

**Obsidian Tower** [W-Obsidian] [GF]

| Floor | Boss | Lv | HP |
|---|---|---|---|
| 5 | Samhain ×1 | 28 | 960 |
| 10 | Electrojelly (blue) | 29 | 660 **or** 990 |
| 15 | Eyebat King ×2 | 30 | 680 |
| 20 | Terran Golem | 32 | 1080 |
| 25 | Gauntlet floor (all monster types, green variants) | 23 | 24–189 |
| 30 | Griff | 33 | 1480 |
| 35 | Gauntlet floor (slimes, all types) | — | — |
| 40 | The Insect King ×2 | 34 | 1140 |
| 45 | "The Gauntlet" (giant Dark Knight ×1 + knights) | 35 | 780 |
| 50 | Griff & The Archdevil's Arm | 37 | 1640 |

**Lapis Ruins** [W-Lapis] [GF] — the wiki notes its boss Lv/HP were read off the boss rush
("roughly 10 levels higher than standard"); [GF] lists the same numbers for the dungeon itself.

| Floor | Boss | Lv | HP |
|---|---|---|---|
| 5 | Samhain ×2 | 43 | 1410 |
| 10 | Reginald Drisby v2 (paper fan) | 44 | 1440 |
| 15 | Eyebat King ×3 | 45 | 980 |
| 20 | The Insect King ×3 | 46 | 1500 |
| 25 | The Crowned Slime ×3 | 47 | 1020 |
| 30 | Gauntlet floor (bombs) | — | — |
| 35 | Samhain ×3 | 48 | 1560 |
| 40 | Electrojelly v2 (pink/red) | 49 | 1060 **or** 1590 |
| 45 | Gauntlet floor (red enemies) | — | — |
| 50 | Arma | 50 | 2700 |
| 55 | "The Gauntlet" ×2 Dark Knights | 51 | 1100 |
| 60 | Reginald Drisby v3 (giant fish) | 52 | 1680 |
| 65 | Gauntlet floor (skeletons, ghosts) | — | — |
| 70 | Terran Golem v2 (dark) | 53 | 1710 |
| 75 | Gauntlet floor (yellow/gold enemies) | 40 | 44–158 |
| 80 | no boss, cutscene | — | — |
| 85 | Gauntlet floor (ropers) | 42 | 138–230 |
| 90 | Volcanicrab v2 (black) | 54 | 1740 |
| 95 | Gauntlet floor (knights) | 44 | 115–336 |
| 100 | The Geddon Device | 55 | torso 3540, arms 1180 each |

**Crystal Nightmare boss rush** [W-CN] (60F = 3 equipment chests; speedrun timer stops on leaving 59F [SR])

| F | Boss | Lv | HP | F | Boss | Lv | HP |
|---|---|---|---|---|---|---|---|
| 31 | Crowned Slime ×1 | 51 | 1100 | 46 | Griff & Archdevil's Arm | 76 | 3200 |
| 32 | Drisby v1 | 52 | 1680 | 47 | Samhain ×2 | 78 | 2460 |
| 33 | Charme | 54 | 1160 | 48 | Drisby v2 | 79 | 2490 |
| 34 | Eyebat King ×1 | 56 | 1200 | 49 | Eyebat King ×3 | 81 | 1700 |
| 35 | Insect King ×1 | 57 | 1830 | 50 | Insect King ×3 | 83 | 2610 |
| 36 | Volcanicrab | 59 | 2142 | 51 | Crowned Slime ×3 | 85 | 1780 |
| 37 | Crowned Slime ×2 | 61 | 1300 | 52 | Samhain ×3 | 86 | 2700 |
| 38 | Tielle | 62 | 1320 | 53 | Electrojelly v2 | 88 | 1840 (**or** 2760) |
| 39 | Samhain ×1 | 64 | 2040 | 54 | Arma | 90 | 4700 |
| 40 | Electrojelly | 66 | 1400 (**or** 2100) | 55 | The Gauntlet ×2 | 91 | 1900 |
| 41 | Eyebat King ×2 | 68 | 1440 | 56 | Drisby v3 | 93 | 2910 |
| 42 | Terran Golem | 69 | 2190 | 57 | Terran Golem v2 | 95 | 2970 |
| 43 | Griff | 71 | 3000 | 58 | Volcanicrab v2 | 96 | 3000 |
| 44 | Insect King ×2 | 73 | 2310 | 59 | The Geddon Device | 98 | 6120 / arms 2040 |
| 45 | The Gauntlet ×1 | 74–75 | 1560 | 60 | chests | — | — |

**DISAGREE (stats):**
- Eyebat King, Amber 5F: Lv 13 / 510 [W-Amber] [GF] vs Lv 8 / 240 [W-Eyebat].
- Drisby, Jade 10F: Lv 7 / 330 [W-Jade] vs Lv 8 / 360 [W-Drisby] [GF].
- Tielle, Amber 30F: Lv 16 / 400 [W-Amber] vs 720 [W-TielleB], Lv 20 / 720 [GF].
- Volcanicrab, Amber 20F: 630 [W-Amber] vs 713 [GF].
- Electrojelly: 660 / 1060 / 1400 / 1840 [W-Obsidian] [W-Lapis] [W-CN] [GF] vs 990 / 1590 / 2100 / 2760 [W-Electro] — exactly ×1.5 everywhere, so probably two different quantities.
- Insect King CN floors: 35 / 44 / 50 [W-CN] vs 34 / 44 / 50 [W-Insect].

---

## 2. Bosses (player-documented, unverified)

General rules players state:
- **"Adventurer" bosses Charme, Griff, Arma fall down after 2 consecutive hits and are invulnerable until they get up.** Multi-hit skills waste everything after hit 2. Tielle is the exception (never knocked down). [W-ArmaB] [W-GriffB] [W-TielleB] [W-Caillou] [SD-Tielle]
- Boss behaviour is not reported to change with HP, except Griff's bat phase and Geddon's retreats.
- Boss drop rates are fixed (common 30 %, uncommon 10 %, rare 5 %); bosses drop no food, their minions can. [SG-Drop] ([TVT] quotes roughly 25 % / 5 % / under 2 % for the CN rush — **DISAGREE**.)

### The Crowned Slime — Jade 5F (×1), Amber 25F (×2), Lapis 25F (×3), CN 31/37/51 [W-Slime] [GF]
- Giant blue slime, nearly immobile. Very high defence at full size.
- Only attack: a jump/stomp with no tell, roughly every 5–10 s. Each landing shrinks it and lowers its defence.
- Blue slimes spawn; after 3 jumps they are absorbed and it returns to full size. Killing the small slimes prevents regrowth.
- Vulnerable window: after it has shrunk ([GF]: weakest after 2 jumps). Enough ATK overcomes the full-size defence.
- Rated the easiest boss; no-damage kill expected. Don't spend SP except to hit several bodies at once (Spin Slash, Hundred Thrusts). Caillou: Sparkburst from range, or point-blank before the first jump; one Ice Mine can end it.
- Arma's Punch Unit cannot hit the smallest size (v1.108).

### Reginald Drisby — Jade 10F (crowbar), Lapis 10F (fan), Lapis 60F (fish), CN 32/48/56 [W-Drisby] [GF]
- Giant rat. Alternates between chasing aggressively and wandering while ignoring the player (even when hit).
- Attacks: 1–2 weapon swings in a frontal arc; fish version adds a third overhead slam with longer reach; bubble spray in a wide frontal cone (random speeds/directions).
- Extremely high defence (damage 1–2) unless stunned.
- Arena mushrooms (static, harmless): **red heals him ~300–500, purple stuns him**. Kill the reds; he then eats a purple.
- He may never go for a mushroom unless he has taken some damage first.
- When he walks to a mushroom, get behind him: Back Attack bonus applies for the whole stun.
- Later versions spawn fewer purples. Fish version whiffs for a long time after a miss.
- Burst picks: Louie Spin Slash spam from behind; Caillou Sparkburst from behind (can kill in one stun); Arma Vector Cannon timed on the stun.

### Charme — Jade 15F, CN 33 [W-CharmeB] [W-Charme] [GF] [TVT]
- Fast melee. Throws sticky nets (Shadow Weave); stepping in one slows you and gives her a free hit.
- Tell: she pauses and says a line, then lunges straight ahead wreathed in fire (Flame Charge). Bait it, step aside, punish the recovery from behind.
- Low defence, full damage at all times. Knocked down after 2 hits.
- The wiki has no behaviour text for her; this is [GF] + [TVT] only.
- Caillou's multi-hit skills are weak here for the 2-hit reason.

### Eyebat King — Amber 5F (×1), Obsidian 15F (×2), Lapis 15F (×3), CN 34/41/49 [W-Eyebat] [GF]
- Flies around aimlessly; regular eyebats spawn continuously (stronger colours with more kings).
- Roughly every 15 s (wiki marks this uncertain) it charges a large laser. **While charging it stops tracking**, so move off its line. Recoil pushes it back; the beam has a wide area — don't approach while it fires.
- No phase change.
- Tactics: kill one king at a time; kings can be cornered. High MDEF matters (minion sonic rings; Red Eyebats hit hard). Caillou: stand near a wall/corner, Sparkburst, Warp through beams. Avoid damage-over-time weapons on Arma.

### The Insect King — Amber 10F (×1), Obsidian 40F (×2), Lapis 20F (×3), CN 35/44/50 [W-Insect] [GF]
- **Shell is immune; only the head takes damage.**
- Tell: curls up, then rolls across the arena. Run perpendicular / parallel to its facing; never get pinned on a wall.
- Emits pink-purple gas; standing at the head is safe from it.
- Bees spawn from it; bee hits slow you, which sets up the roll.
- Multi-boss: stand centre, use area skills on the bees. Charge skills pass through the boss harmlessly (Elan/Nagi Flame Charge, Griff Dark Claw/dash). Caillou: re-cast Sparkburst for its invulnerability to eat the roll. Arma: Rocket Boots.

### Volcanicrab — Amber 20F, Lapis 90F (black), CN 36/58 [W-Crab] [GF]
- Almost undamageable while upright. Attacks: claw swipes (heavy, frontal), bubble cone, eruption leaving fire patches around it.
- Bombs spawn. Kill a bomb, then knock the bomb corpse at the crab: **a direct hit explodes instantly, a timer explosion nearby also works, and either flips it**. Flipped = vulnerable.
- While flipped, **hitting its face counts as a Back Attack**.
- Don't knock the bomb when it is close to you; kill bombs regardless (their fire rings add up). The big claw blocks projectiles aimed at the bomb.
- Lapis version cycles several bomb colours with different fuse lengths.
- Caillou can kill it within one flip with point-blank Sparkburst; Tielle uses Flame Arrow (pierces) on a shielded bomb, then Cuterage + charge-3 shots.

### Tielle — Amber 30F, CN 38 [W-TielleB] [GF] [SD-Tielle]
- Three stages of box fields. Boxes take 2 hits each, one box per swing; contents: nothing, a slime, a jack-o-lantern, a gnoll, or an item (only if inventory has room).
- Gnolls spawn on top of boxes and throw chestnuts for the whole fight.
- Stage 1: she is visible behind boxes, fires a scatter shot aimed at you. Reach her → she flees.
- Stage 2: she hides inside a box; the scatter shot's angle reveals which.
- Stage 3: hidden again, now uses Starshot Arrow (marked ground spots near you). Break her box → open duel with single/triple shots.
- **Not knocked down after 2 hits**; slow attack rate; blind spot at sides and back.
- Bait: stand off the box you want, let her fire, then step in. In the duel, approach diagonally, bait the shot, pass through it with any i-frame skill, burst her down.
- Cheese: Caillou's Warp outside the first arena returns him to the first arena; while he is off-screen she does not attack, and Gemini Force still homes on her if she is on screen. [W-Caillou]

### Samhain — Obsidian 5F (×1), Lapis 5F (×2) / 35F (×3), CN 39/47/52 [W-Samhain] [GF] [W-Enemies]
- Pumpkin-headed; drifts away from the player along the arena edges, repeatedly vanishes and reappears.
- Throws "Bouncer" jack-o-lanterns that ricochet off the walls; in groups, fire rings become the real threat.
- No tactics beyond "keep attacking"; awkward for melee. Immune to Vampire Vortex. [W-Griff]

### Electrojelly — Obsidian 10F (blue), Lapis 40F (pink/red), CN 40/53 [W-Electro] [GF] [TVT]
- **Only the tentacles take damage**, and they are also its weapons. Strike the tips right after a swing.
- Attacks: single-tentacle whip toward you (v2: both directions), fan-like spin, lightning bolts; spawns a few ropers.
- **Safe spot claimed: directly under the torso** takes no hits. [W-Electro]
- Vampire Vortex pulls it but deals nothing (tentacles stay outside the centre). [W-Griff]

### Terran Golem — Obsidian 20F, Lapis 70F (dark), CN 42/57 [W-Golem] [GF]
- Slow, heavy. Tells: arm raised → slam (sidestep); charging energy → spin (retreat); jump → lands on your position after a delay (keep moving).
- Spawns Easter-Island-head minions (1, later 2). Vampire Vortex pulls them without damaging them.
- Punish after every attack. Thinnest documentation of any boss.

### The Gauntlet (Dark Knight) — Obsidian 45F (×1), Lapis 55F (×2), CN 45/55 [W-GauntletB] [GF]
- Giant knight plus coloured knights spawning. Wanders semi-aimlessly; **shield blocks everything from the front unless it is attacking**.
- Fires a wide "vacuum sword" wave whenever the player is on its row or column.
- Lightning bolt on its own position, used only when it is being hit in melee and blocking repeatedly.
- Opening: approach diagonally and cross its line so it fires at where you were, then hit its side. Or pass through the wave with a charge skill.
- Louie blocks the wave when facing it: walk in, block, attack + Spin Slash, back off before the lightning. Caillou: Sparkburst as it fires. Arma: high rate of fire stops its guard from recovering.

### Griff — Obsidian 30F, CN 43 [W-GriffB] [W-GriffArm] [GF]
- Fast basic claw with long reach.
- **Dark Claw**: only when the player is on his row/column. Stands still, invulnerable ~1 s, then dashes a short distance almost instantly. Recovery afterwards is the main opening.
- **Fire rings**: 5 rings that scatter then converge on you and erupt. He stands still until hit or until they ignite.
- **Below ~20 % HP** ([GF]: ~15 %): teleports to top or bottom edge and fires a cone of bats; 2 bats remain, attack, and later burst into flame.
- Falls after 2 hits (invulnerable), then **wake-up AoE around himself**. [W-GriffArm] adds that he may activate Demon Soul if hit too often quickly.
- Tactics: single big hits only. Position where Dark Claw will end. Flame-Charge-type skills hit and carry you out of the retaliation. Sprint/dash outruns the rings.
- Louie: Vacuum Sword from range, or sword hit cancelled into Spin Slash to dodge the counter. Charme: keep one Mirror Image up so both hits land at once. Caillou: Sparkburst + Warp over the rings, never cast while lined up with him. Griff mirror: dash i-frames through his first swing, hit twice, dash out.

### Griff & The Archdevil's Arm — Obsidian 50F, CN 46 [W-GriffB] [W-GriffArm] [GF] [TVT]
- Griff unchanged. **The Arm is invincible** and disappears when Griff dies.
- Arm loop: thin beam through the middle that widens and sweeps to the top or bottom, leaving a small safe strip; 3 sweeps alternating direction; then it rises and slams (usually left side / your position after a delay); repeat.
- The safe strip is where Griff's bat cone lands, per [TVT].

### Arma — Lapis 50F, CN 54 [W-ArmaB] [GF]
- Falls after 2 consecutive hits. Stays put when lined up with the player; nearly everything needs line of sight.
- **Blade launcher**: 4 discs in a wide cone, long range, often repeated. Damages only on the return trip. She always opens with it.
- Flamethrower: narrow, long, single hit, leaves burning ground. Often followed by the rocket.
- Rocket (Messiah Unit): slow, short range, hits hard.
- Gatling: very short range, very high rate; interrupted by hitting her.
- **Vector Cannon**: used from the far wall, usually after landing a hit; ~3 s charge tell; devastating.
- Standard: approach on a diagonal, 2 hits, retreat. Or circle her in one direction landing one hit per pass.
- Cheeses:
  - Caillou: chain Warp (boss-fight Warp = ~2 s hover invulnerability) to staff range, one melee hit, Warp, repeat; trivial once her back is on a wall. [W-ArmaB]
  - Griff: Dark Claw at the start, one normal hit, repeat alternating north/south. The claw's start-up resets her 2-hit counter and she stays locked in the disc attack. Alternative: Demon Soul, 2 hits, dash through her, repeat. [W-ArmaB]
  - Charme: stand just outside flamethrower range, Flame Charge through her as she starts shooting, one hit from behind. [W-ArmaB]
  - Player Arma: Hammer Arm orbs deployed, then swap to a stronger part — two heavy hits before the knockdown. Chaos Unit also rated for 2-hit bosses. [W-Arma] [W-Chaos]
- Called the hardest fight of the CN rush; Louie's shield blocks none of her projectiles.

### The Geddon Device — Lapis 100F, CN 59 [W-Geddon] [GF]
- Arena of floating hexagons; falling off costs HP and respawns you.
- Three parts: torso (the only part that must die) and 2 arms (do not respawn).
- Torso: multi fire-ring attack (as Griff's), multi-point lightning barrage (the real threat), backs away from the player.
- **After each 1/6 of its max HP it retreats off the map, untargetable, for ~5–10 s; damage per visit is capped at 1/6.** While away it rockets one hexagon out of the arena (same order every time).
- Arms sweep lasers; pattern changes every ~4 sweeps.
- Plan: burst the torso at the start while it is close; when it retreats kill both arms; then finish.
- Kill it over a hexagon, or the drop falls into the void.
- Caillou: hold Sparkburst from spawn, kill arms, return to the spawn hex (where Recette/Tear stand) and repeat. Tielle: Seeker Arrows from the spawn hex timed to the falling platform. Louie/Nagi: fall back to Vacuum Sword/Spear from the spawn hex. Phalanx Cannon cannot hit the arms. Immune to Vampire Vortex.

---

## 3. Combat mechanics (player-documented, unverified)

### Bonuses
- **Side / Back Attack**: hitting from the side or behind deals more damage and yields a little extra EXP. No multiplier published. [W-Combat] [R-Combat]
- **Counter**: hitting an enemy during its attack also gives a damage bonus, but may not interrupt it. [R-Combat]
- Diagonal positioning is the standard way to get side hits safely, since most enemy attacks are orthogonal. [R-Combat]
- **Chain**: consecutive kills of the same enemy family raise EXP; a different family breaks it. Colour variants usually count as the same family (gnolls, jack-o-lanterns, both mushroom kinds). The rotating "slime-go-round" trap breaks every chain. No numbers published. [W-Hall] [W-Enemies] [W-Chests]
- Criticals exist; Charme crits most (about 1 in 5–8 hits). [W-Charme]
- EXP comes as gems that home on the adventurer; gems leaving the screen are lost. [W-Combat]
- **Level-up refills HP and SP.** [W-Combat]
- Off-screen enemies are frozen (ropers, slime rings excepted); enemies only spawn off-screen except wisps and trap spawns. [W-Combat]

### Controls (keyboard defaults per [GF])
Z attack · X use selected skill · C cycle skill · W / Esc menu · arrows move · double-tap direction = dash/run (Charme, Elan, Nagi, Griff).

### Damage and equipment
- Skill damage is given as a percentage of "physical", "magical" or "physical+magical" damage; no formula published.
- 5 equipment slots: one weapon, plus at most one item per super-category (body / head / arm / accessory). Rings only via lending from inventory, which costs slots. [W-Equip]
- Adventurers upgrade their own gear by buying from the shop once their card is held. [W-Equip]
- Ferromin: permanent Max HP, capped at +200 per adventurer. Concoctions/Secret Mix: temporary stat buffs. [W-Med]

### Invincibility
Documented i-frame sources: Louie Spin Slash (whole skill); Charme run start-up, Mirror Image cast; Caillou Pyrobomb (whole), Sparkburst (first 0.5 s), boss-fight Warp (~2 s); Tielle Cuterage activation; Elan Fist Flurry; Griff dash and Dark Claw; Flame Charge (Charme, per [R-Combat]); Arma briefly after getting up. Charme can parry any attack by tapping toward it with correct timing. [W-Louie] [W-Charme] [W-Caillou] [W-Tielle] [W-Elan] [W-Griff] [W-Arma]

### Floor effects [W-Floor]
ATK ×2 / DEF ×2 / speed up, each for adventurer, enemies, or everyone; EXP ×2; SP cost halved (rounds down: 5 → 2); consumables ×2 or ×0.5; near-blind; map revealed; no automap; permanent slick floor; trap-heavy; early Will-o'-Wisps. Enemy ATK ×2 also applies to traps.

### Traps [W-Traps] [W-Chests]
Room: falling rocks (watch shadows), 5 s bomb, tuna strike, slimes, roper swarm. Chest: paralysis (slow), blue slimes (square group or rotating ring), ice floor, warp, bomb. Charme or a Thief's Ring flags trapped chests with "!".

### Food [W-Food]
Each quality point adds 20 % (max +4 found, +15 fused).

| HP | | SP | | Both | |
|---|---|---|---|---|---|
| Walnut Bread | 10 | Candy | 10 | Shrimp Doria | 50/50 |
| Apple | 15 | Chocolate Bar | 20 | Cutlet Bowl | 100/100 |
| Egg Toast | 20 | Taiyaki | 30 | Mix Pizza | 150/150 |
| Orange | 25 | Candy Apple | 40 | Seafood Bowl | 200/200 |
| Cherry / Nut Mix | 30 | Baked Yam | 50 | Lobster | 300/300 |
| Grapes | 40 | Honey | 60 | Masterful Sushi | 400/400 |
| Beef Bowl | 50 | Peach Tin | 80 | Filet Mignon | 500/500 |
| Watermelon | 60 | Shortcake | 100 | Super Crock Pot | 999/999 |
| Roast Chicken | 96 | Mango Pudding | 120 | | |
| Ham Sandwich | 120 | Mont Blanc | 150 | | |
| Curry | 200 | Apple Pie | 300 | | |
| Melon | 250 | Buche-de-noel | 999 | | |
| Ultraburger | 400 | | | | |
| Kid's Lunch | 999 | | | | |
| Durian | 999 (SP −500) | | | | |

Market tier 0: Apple, Walnut Bread, Candy, Taiyaki. Berserk-type buffs lock out items.

### Adventurers and SP skills
All skills: X fires the selected one, C cycles.

| Adventurer | Start | Basic attack | Skills (SP, learned at) | Traits |
|---|---|---|---|---|
| Louie [W-Louie] | Lv 1, HP 20, SP 10 | 100 % phys, ~270° arc; cancels into skills | **Spin Slash** 5 SP: 200 % phys, full circle, invulnerable. **Vacuum Blade** 5 SP, Lv 10: 80 % phys+mag projectile, cardinals only | Auto-blocks frontal ranged hits when not attacking; no dash; lowest SP |
| Charme [W-Charme] | Lv 8, HP 38, SP 31 | 100 % phys, fast, short | **Mirror Image** 5: shadow copies basic attacks 10 s, max 5. **Flame Charge** 5, Lv 10: 50 % phys+mag, 8 directions, ~2/3 screen. **Shadow Weave** 5, Lv 20: net, roots enemies 8 s | Run with turning; parry; trap sense |
| Caillou [W-Caillou] | Lv 10, HP 26, SP 108 | 100 % phys, slow, tiny; refunds SP = damage/10. Hold 1 s for a fireball (100 % mag) | **Pyrobomb** 5: 100 % mag sphere, invulnerable. **Ice Mine** 5: up to 10 × 50 % mag. **Sparkburst** 5 + ~3 per 0.5 s held: rapid 50 % mag shards in a V, rooted. **Gemini Force** 25, Lv 25: homing orbs, 50 % mag each. **Warp** 5, Lv 30: random teleport; in boss rooms a ~2 s invulnerable hover | Slow, lowest HP, highest SP/MAG |
| Tielle [W-Tielle] | Lv 20, HP 63, SP 81 | Arrows; charge level every 0.75 s (1 / 1 / 3 / 5 / 5+1 back / 5+3 back); can move while charging | **Flame Arrow** 4: 90 % piercing. **Seeker Arrow** 5: homing, 3 × 50 %. **Starshot Arrow** 8: rain, 60 % each. **Cuterage!** 10, Lv 25: 6 s, charge ×4 speed, no knockdown, paralyses non-boss enemies | No dash; second-lowest HP |
| Elan [W-Elan] | Lv 15, HP 102, SP 34 | 2-punch combo 70 % + 100 %, shortest reach | **Flame Charge** 5. **Fist Flurry** 20: phase in, 7 × 40 % + 100 %, invulnerable. **Berserk** 5, Lv 20: 9 s, ATK +50 %, damage taken halved, no knockdown, no items/skills | Straight-line dash (falls over on impact); highest HP |
| Nagi [W-Nagi] | Lv 15, HP 66, SP 52 | 100 % phys, long narrow thrust | **Flame Charge** 5. **Hundred Thrusts** 8: many × 10 %, uncancellable. **Spin Slash** 10, Lv 20: 120 %. **Vacuum Spear** 5, Lv 30: 75 % phys+mag wave | Run (strafe only); blocks physical projectiles |
| Griff [W-Griff] | Lv 30, HP 99, SP 159 | 100 % phys, ~270°, longer than Louie | **Dark Claw** 10: 100 % phys+mag charge, invulnerable. **Demon Soul** 20 (10 before 1.08): 8 s, ATK +50 %, damage halved, attack speed ×2, no knockdown, no items/skills. **Vampire Vortex** 30: pulls enemies, 100 % mag, heals 50 % of damage; interruptible 2 s cast | Invulnerable dash through enemies |
| Arma [W-Arma] | Lv 1, HP 25, SP 30 | Depends on Arm Part; 100 ammo, 3 s reload | **Vector Cannon**: needs full SP, uses all; 5 s charge (uninterruptible, damage halved), huge beam; knights can block it | No dash; Rocket Boots only footwear |

Which is strong where (player opinion):
- Caillou: best overall boss-rusher (Sparkburst deletes most bosses), weak vs the 2-hit bosses, slow vs Arma with Gemini Force. [W-Caillou] [R-CN]
- Single-hit damage (Louie Spin Slash, Elan's punch, Griff Dark Claw) recommended for Griff/Arma. [R-CN]
- Mirror Image trivialises mini-bosses. [W-Charme]
- Vampire Vortex + Demon Soul for groups of bosses. [W-Griff]
- Louie's block makes The Gauntlet and statue/eyebat rooms easy. [W-Louie] [W-GauntletB]

---

## 4. Hiring adventurers (player-documented, unverified)

Two different cards:
- **Business Card** = makes the adventurer hirable in the current loop.
- **True Card** = given at the debt-clear ending by the adventurer taken on the most dungeon trips (ties resolved in the order Louie, Charme, Caillou, Tielle, Elan, Nagi, Griff, Arma). Makes that adventurer hirable from the start of New Game+ / Survival and counts as "befriended". One per loop; cannot be earned in Endless or Survival. Arma's comes only from clearing Lapis 100F. [W-Card] ([GF2] gives the tie order as Louie, Charme, Caillou, Tielle, Nagi/Elan/Griff — **DISAGREE**.)

"Befriended" = a few (2–3) sales at prices they like, or owning their True Card. Visits depend on shop atmosphere and time of day.

| Adventurer | Business Card conditions | Visit conditions | Src |
|---|---|---|---|
| Louie | Day 2: meet at the guild, take him through Hall of Trials, open the 5F chest | — | [W-Louie] |
| Charme | Beat her on Jade 15F → she visits the shop (talk with Tear) → on a later visit gives the card once she likes you (≥ 2 sales per [SG-Unlock]) | Dark + Gaudy; noon/evening/night; "Booze of the World" in the window helps | [W-Charme] [W-Amber] |
| Caillou | After Jade Way: visits and orders Charred Lizard + Bat Wing + Slime Liver (all from Jade Way). Deliver (returns every 3 days until done; holding them already gives a different scene and a bigger reward) → befriend → card. The order is required even with his True Card | Dark, books; any time. Needs Merchant Lv 7 [W-Caillou] [W-Order] **or** Lv 3 [SG-Unlock] — **DISAGREE** | [W-Caillou] |
| Tielle | Town Square scene (after Jade Way unlocked, morning/noon) → beat her on Amber 30F → next morning scene → visits → card after ~2–3 visits/sales | Light; sweets in the window; morning/noon/evening | [W-Tielle] |
| Elan | After Jade Way cleared (and Charme's card per [W-Order] [SG-Unlock]): pub night scenes until the one with his tab → Merchant's Guild in the morning (orphanage delivery) → Town Square → visits → befriend → card | Not strongly Dark/Gaudy; sweets on display; morning/noon/night (not evening) | [W-Elan] [W-Order] |
| Nagi | From Day 9: random pink marker on dungeon floors (not boss rushes); touch her. Max one talk per dungeon run. After 4 talks she comes to the shop → befriend → card. Small dungeons (Hall of Trials, Jade Way) are the fastest place | Light, slightly Gaudy; morning/noon/evening | [W-Nagi] |
| Griff | Amber cleared → midnight Tear scene → Town Square at night ×2 → shop warning (Dark, evening/night) → Louie announces Obsidian Tower → beat Griff 30F and Griff & Arm 50F → he shops → befriend → card | Strongly Dark; evening/night only | [W-Griff] |
| Arma | Endless Mode, all 7 others recruited → she visits (unlocks Lapis) → beat Geddon on 100F → she brings her True Card | any | [W-Arma] |

Fastest known order (NG+ oriented, from [W-Order]): Day 2 Hall of Trials + Jade 11–15 (Charme) · Day 3 Charme ×2 visits, Caillou · Day 4 Tielle plaza scene, Charme announces Amber · Day 5 Amber 26–30 (Tielle) · Days 5–6 pub scenes · Day 7 guild delivery (Elan), night Griff 1 · Day 8 Elan card, night Griff 2 · Day 9 Griff warning (evening), Louie announces Obsidian (night) · Day 10 Obsidian final section (Griff). That page says "Obsidian Tower (55–60)", which contradicts the 50-floor count.
Dungeon entry floors persist across loops, so deep sections can be entered directly in NG+. [W-NG+]

---

## 5. Speedrun / low-level / cheese notes (player-documented, unverified)

No dedicated dungeon speedrun notes were found. What exists:
- speedrun.com categories: Any% (New Game → final "Paid" stamp) and Crystal Nightmare Boss Rush (dungeon entry → leaving 59F). [SR]
- The Any% guide is shop/menu routing with heavy RNG; its body is a login-walled Google Doc. Community advice for fast clears is to avoid dungeons and sell instead. [SR-Guide] [SG-Debt]

Collected cheeses and safe spots (all from §2):
1. Electrojelly: stand under the torso. [W-Electro]
2. Arma: Caillou Warp-and-whack; Griff Dark Claw disc-lock; Charme Flame Charge from just outside flamethrower range. [W-ArmaB]
3. Tielle: Caillou Warp out of the arena + Gemini Force while off-screen. [W-Caillou]
4. Geddon: fight from the spawn hexagon with ranged skills; 1/6-HP cap per visit means bursting more is wasted. [W-Geddon]
5. Drisby: kill reds, hit from behind during the purple-mushroom stun. [W-Drisby]
6. Volcanicrab: bomb flip, then face hits = Back Attacks. [W-Crab]
7. Gauntlet: cross its line diagonally to bait the wave; Louie just blocks it. [W-GauntletB]
8. 2-hit bosses: time two simultaneous heavy hits (Mirror Image, Hammer Arm orbs after a part swap). [W-GriffB] [W-Arma]
9. Tielle's Cuterage can be re-cast for near-permanent invulnerability with good timing. [W-Tielle]
10. Spend all SP just before a level-up (full refill). [W-Combat]
11. Gauntlet floors are the suggested place to power-level weak adventurers. [W-Gauntlet]
12. "SP consumption halved" floors: 5 SP skills cost 2. [W-Floor]
13. Elan's dash can knock walking enemies off ledge-type floors (no EXP; v1.105 behaves differently). [W-Elan]
14. Arma weapon-swap mid-attack applies the strongest part's attack bonus to any part. [W-Arma]

---

## Sources

Fandom wiki (read via its MediaWiki API, wikitext as of 2026-11-11):
- [W-Dungeon] https://recettear.fandom.com/wiki/Dungeon
- [W-Hall] https://recettear.fandom.com/wiki/The_Hall_of_Trials
- [W-Jade] https://recettear.fandom.com/wiki/Jade_Way
- [W-Amber] https://recettear.fandom.com/wiki/Amber_Garden
- [W-Obsidian] https://recettear.fandom.com/wiki/Obsidian_Tower
- [W-Lapis] https://recettear.fandom.com/wiki/Lapis_Ruins
- [W-CN] https://recettear.fandom.com/wiki/Crystal_Nightmare
- [W-Gauntlet] https://recettear.fandom.com/wiki/Gauntlet
- [W-GauntletB] https://recettear.fandom.com/wiki/The_Gauntlet
- [W-Slime] https://recettear.fandom.com/wiki/The_Crowned_Slime
- [W-Drisby] https://recettear.fandom.com/wiki/Reginald_Drisby
- [W-CharmeB] https://recettear.fandom.com/wiki/Charme_(boss)
- [W-Eyebat] https://recettear.fandom.com/wiki/Eyebat_King
- [W-Insect] https://recettear.fandom.com/wiki/The_Insect_King
- [W-Crab] https://recettear.fandom.com/wiki/Volcanicrab
- [W-TielleB] https://recettear.fandom.com/wiki/Tielle_(boss)
- [W-Samhain] https://recettear.fandom.com/wiki/Samhain
- [W-Electro] https://recettear.fandom.com/wiki/Electrojelly
- [W-Golem] https://recettear.fandom.com/wiki/Terran_Golem
- [W-GriffB] https://recettear.fandom.com/wiki/Griff_(boss)
- [W-GriffArm] https://recettear.fandom.com/wiki/Griff_%26_The_Archdevil%27s_Arm
- [W-ArmaB] https://recettear.fandom.com/wiki/Arma_(boss)
- [W-Geddon] https://recettear.fandom.com/wiki/The_Geddon_Device
- [W-Louie] https://recettear.fandom.com/wiki/Louie
- [W-Charme] https://recettear.fandom.com/wiki/Charme
- [W-Caillou] https://recettear.fandom.com/wiki/Caillou
- [W-Tielle] https://recettear.fandom.com/wiki/Tielle
- [W-Elan] https://recettear.fandom.com/wiki/Elan
- [W-Nagi] https://recettear.fandom.com/wiki/Nagi
- [W-Griff] https://recettear.fandom.com/wiki/Griff
- [W-Arma] https://recettear.fandom.com/wiki/Arma
- [W-Chaos] https://recettear.fandom.com/wiki/Chaos_Unit
- [W-Card] https://recettear.fandom.com/wiki/Business_Card
- [W-Order] https://recettear.fandom.com/wiki/Order_of_Events
- [W-Combat] https://recettear.fandom.com/wiki/General_Combat_Notes
- [W-Enemies] https://recettear.fandom.com/wiki/Enemies
- [W-Equip] https://recettear.fandom.com/wiki/Equipment
- [W-Food] https://recettear.fandom.com/wiki/Food
- [W-Med] https://recettear.fandom.com/wiki/Medicines
- [W-Floor] https://recettear.fandom.com/wiki/Floor_Effects
- [W-Traps] https://recettear.fandom.com/wiki/Traps
- [W-Chests] https://recettear.fandom.com/wiki/Chests
- [W-Time] https://recettear.fandom.com/wiki/Time_Management
- [W-Guild] https://recettear.fandom.com/wiki/Adventurer%27s_Guild
- [W-Endless] https://recettear.fandom.com/wiki/Endless_Mode
- [W-Survival] https://recettear.fandom.com/wiki/Survival_Mode
- [W-NG+] https://recettear.fandom.com/wiki/New_Game_%2B

GameFAQs (game id 604893):
- [GF] Guide and Walkthrough by InfernoCrossing, v1.2 — https://gamefaqs.gamespot.com/pc/604893-recettear-an-item-shops-tale/faqs/62717 (controls, merchant-level table, dungeon and boss sections read)
- [GF2] Strategy Guide by AdventX06 — https://gamefaqs.gamespot.com/pc/604893-recettear-an-item-shops-tale/faqs/61005 (opening only)
- Unread: https://gamefaqs.gamespot.com/pc/604893-recettear-an-item-shops-tale/faqs/61659 , https://gamefaqs.gamespot.com/pc/604893-recettear-an-item-shops-tale/faqs/60940

Steam community:
- [SG-Unlock] Unlocking New Characters / Dungeons / Game Modes — https://steamcommunity.com/sharedfiles/filedetails/?id=2991097179
- [SG-Drop] Enemy Drop Rate Mechanics — https://steamcommunity.com/sharedfiles/filedetails/?id=2966594029
- [SG-Debt] Simple Guide to pay all debts and reach endless mode — https://steamcommunity.com/sharedfiles/filedetails/?id=373947243
- [SD-Tielle] Discussion "Amber Garden Floor 30 boss fight" — https://steamcommunity.com/app/70400/discussions/0/648816742597179860

Speedrunning:
- [SR] https://www.speedrun.com/recettear_an_item_shops_tale (categories via the public API, game id `ldezl5d3`)
- [SR-Guide] All Debts any% Full Guide — https://www.speedrun.com/recettear_an_item_shops_tale/guides/epsyy (archived copy: https://web.archive.org/web/20241224042148/https://www.speedrun.com/recettear_an_item_shops_tale/guides/epsyy )
- TASvideos game search — https://tasvideos.org/Games/List?searchTerm=recettear (no entry found)

Other:
- [R-Combat] r/Recettear "Basic Combat Guide [WIP]" (2013), archived — https://web.archive.org/web/2020/https://www.reddit.com/r/Recettear/comments/1jbxko/basic_combat_guide_wip/
- [R-CN] r/Recettear "Best adventurer to grind Crystal Nightmare boss rush" (2024) — https://www.reddit.com/r/Recettear/comments/1d2zj6n/ (page blocked; only a search-engine summary was seen — lowest confidence)
- [TVT] TV Tropes, archived — https://web.archive.org/web/2022/https://tvtropes.org/pmwiki/pmwiki.php/VideoGame/Recettear
