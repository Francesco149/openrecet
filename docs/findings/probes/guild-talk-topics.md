# Probe: guild-talk-topics

- **Run:** `guild-talk-topics` · **start:** save:64d43650865f · **task:** `tools/drive/tasks/guild-talk-topics.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 52 turns, $0.77, 4 min
- **Scenario:** `tests/scenarios/guild-talk-topics/` (3881 frames)
- **Verdicts (2026-11-10):** check PASS (tools/drive/tasks/guild-talk-topics.md); verify --real MATCH; retail golden made (Windows, real device); port triage: identical guild-talk-topics                    ticks 1..4181, save IDENTICAL

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

I read all six Merchants' Guild Talk topics with full transcripts below, and the Market has no Talk option. Run `guild-talk-topics` is 3881 frames: `rd check` gives PASS, `rd verify` gives MATCH, and the in-game save was filed as save:197409ade35a.

One correction to the task: "About fusion" was NOT tagged New in this save. Its seen flag was already 1 at load, so only five topics carried the tag. I read it anyway.

STEPS AND FRAMES
- Load (`.*60 A .*30 A`): in the shop with control at f137 (step 1). No morning event; the HUD says Day 16 and the day dword reads 15.
- Shop door to town map (rd search, score = minus the distance to the door): scene 8 at f403.
- Town map: `U R` puts the cursor on 3, then A. In the guild at f462 (step 2), menu mode 1. The main list is Buy, Sell, Fusion, Talk (with a New badge), Leave. The greeting bubble: "Before you stock up, make sure to read up on everything you can!"
- Talk: `D D D A` with gcur on 3. The submenu is mode 2 from f558 and has finished sliding in at f568 (checkpoint talklist). Its bubble: "If there's anything you don't know, just ask."
- Topics (each is A, which starts the dialogue 16 ticks later, then A taps every 8 ticks):
  - 0 What is the guild?: dialogue f584-f962, flag set f584
  - 1 What can I do here?: f991-f1315, flag set f991
  - 2 About merchant levels: f1344-f1560, flag set f1344
  - 3 About the town: f1589-f2417, flag set f1589
  - 4 About unknown items: f2446-f2770, flag set f2446
  - 5 About fusion: f2799-f3258, flag was already 1
- Leaving: B closes Talk (mode 1 at f3274); `D A` on Leave reaches the town map at f3311.
- Market: `L L A` (cursor 1) enters it at f3371. Its menu is Buy, Sell, Leave only, so there was nothing to transcribe. Its bubble began "We came to the market... Wh"; I left before reading the rest. Leave at f3488.
- Home: `D A` (cursor 0) puts me in the shop at f3537 (step 3), at (2, -3.24); stime went 0 -> 1.
- Save: ESC, then D x4 to the Save entry (index 4), A, A on slot 020, then "Overwriting file. Are you sure?" and A. The slot's TIME changed from 2:17:46 to 2:18:46, so it saved. Back in the shop at f3851; the movie ends at f3881.

TOPIC LIST STATE
- Mode: gmode (0x9642c00) == 2 is the Talk submenu.
- Main cursor: gcur = 3 on Talk.
- Topic cursor: I named gtalk = s32 at 0x9642c10, values 0-6 (6 = "Never mind").
- Slide and confirm: 0x9642c1c is the slide; it reaches 0x19 when the list is fully in. 0x9642c20 is the confirm slide; it counts 1 to 15, and the dialogue starts the next tick.
- End of a topic: dlg goes 1 -> 0 and the list (mode 2, slide 0x19) gives control back. I used --until "dlg == 0".
- "New" flags: one byte per topic at bank+0x2bc98+i, i.e. 0x450f430..0x450f435. Each goes 0 -> 1 on the tick the topic's dialogue starts. Marking RAM before topic 0 and diffing the bank found only this byte (plus the frame counter at bank+8).
- Badges: each topic's badge disappears when its flag is set. The "New" on the Talk header disappears once all six flags are 1 (at f2446).
- Picture: strip f560-f1000 at /mnt/ephemeral/openrecet/drive/runs/guild-talk-topics/view/strip_560_1000.png. I looked at it: the list slides in, the Guild Master speaks topic 0, and the cursor moves to topic 1 at f1000. The New sparkles are hard to see at strip scale. The game shows "ESC Key: Event Skip" during topics.

TRANSCRIPTS (the Guild Master unless marked; page breaks are " / ")

0 What is the guild?
Right, so! A "guild" is, essentially, a group of people in the same / kind of business who cooperate with one another. / Since the prices on items can vary from store to store, / it's hard to do business if there isn't a base price between them, right? / That's what the Merchant's Guild is for - we manage / wholesaling and keep prices stable throughout the city. / That's why we require that everyone who wants to do business around here / register with the guild. / Don't worry! Your store, "Recettear", is already in the books! / It ain't all just paperwork and red tape, though! Now that you're a member, / you can pick up stock for your shop here, and you get access to a few / other benefits, too! / We're here to help, so feel free to make use of us, okay?

1 What can I do here?
Here at the guild, you'll always be able to get goods for a set price. / Most of what we can get our hands on is practical stuff, though... / Weapons, armor, clothes, adventuring necessities and the like. / The town's main market, who we work with, is where you can get things like / foodstuffs, books, and other assorted knickknacks. / The really flashy and rare stuff is a bit out of our league. / If you've a mind to get your mitts on that kind of stuff, you'll have to go / dungeon-stomping yourself, or buy it off a customer. / Oh, we'll also buy off any excess stock you have. / Mind, we buy low as a rule, so if you want to make an actual profit, / you're better off selling to your customers.

2 About merchant levels
Your "Merchant Level" is, well, your ability as a merchant. / As it goes up, you'll figure out how to redecorate or even / expand your store. / Heck, you might even be able to convince me to let you have a look at some / of our more, ah, "premium" items! / So how do you raise your level? Simple! / Buy! Sell! Trade! This is an art, and you learn by doing!

3 About the town
This city's getting a bit famous for the old ruins and the like that litter / the surrounding countryside, so we've been seeing a steady stream of / adventuring-types coming in for a while now. / They're all here to try and strike it rich off of undiscovered treasures / in the wilds. "Hunting for phat lewt", they call it. / Well, I dunno what they see in chasing after over-sized lutes, but their / presence has also brought a lot of new merchants to the city! / I sure won't complain! Business has never been better! / If you're looking for one, hmm... a few might be resting in the plaza. / I bet most of 'em will take to the city's biggest pub, though... / it's never too hard to sell those wandering-types a little bit of liquid / pleasure! Even I go in there for a little nip every now and then! / The more faithful will probably hang about the city chapel. / We do get a few like that, now and then, on a pilgrimage or quest / to find old holy relics. Er, praise be to seekers and all that, of course. / You might check the Adventurer's Guild proper, too - they're in charge / of making sure the exploration efforts aren't a total bedlam. / They even succeed... sometimes! / Now, keep in mind that these places are only open at certain times. / For example, that pub only opens in the evening. / We're pretty much open all the time, though! / [Recette] Wait... / If you are always open... how do you find time to visit that pub? / For that matter, when do you go home?! / [Guild Master] Little missy, if you had the kind of wife I do, / you'd be at work or in the sauce at all hours, too. / [Recette] Merde... forget I asked...

4 About unknown items
Now, if you go dungeon-diving, you'll find a lot of stuff which you won't be / able to identify at first. / In particular, I'll bet my hat you'll find a lot of what're commonly called / "Mystery Ingredients" by the adventurers. / At first you'll probably be tearing your hair out over all the "junk" / you keep finding in those places, but as your merchant level goes up / you'll be able to sort out the stuff you find, and pick the best materials and / whatnot from the lot. / Oh, if you manage to get out of those death-traps safely, you'll have time / to identify everything safely. / That means you won't know what you're carrying 'till you leave, though!

5 About fusion
Once a few merchant levels are under your belt, you'll be able to "fuse" / items here at the guildhall. / There's quite a few items you can only get by using fusion, so I'd get / real familiar with that furnace, if I were you. / Remember: what you put in affects what you get out of fusion. / If you put in some materials of, say, +2 quality, then you'll get / an item of similar quality as the result. / Keep an eye on what you're fusing, too - you don't want to waste / good materials on some mediocre thing you can just buy somewhere! / Another thing to keep in mind: it can be real tempting to keep your / best-quality items to yourself for personal use... / ... but your customers know quality goods when they see 'em, and you'll / earn a lot of respect if you sell top-shelf merchandise. / There's a couple of stages to what you can pull off with fusion / as you pick up some merchant levels, so experiment and be sure / to come back here often to see what you can make!

GAME FACTS
- Two speaker positions show in rd text: rows at x=232 are the Guild Master, rows at x=180 are Recette.
- Each page takes 27 ticks with A every 8 ticks. Speaker changes take about 45-63 ticks.
- The pause menu's entry types at 0x74b2844 are [0, 1 Items, 6 Encyclopedia, 2 Options, 3 Save, 4]. The save picker opens on slot 020 and asks "Overwriting file. Are you sure?" with the cursor on Yes.
- Leaving the guild or Market has no farewell dialogue; the town map comes about 37 ticks after A.

TOOL GAPS
- rd eval has no list comprehensions and no hex(). ram read with 8 bytes prints dwords, which makes byte flags easy to misread.
- Worth adding to the shared vars: gtalk (0x9642c10), the guild talk-slide 0x9642c1c, the confirm-slide 0x9642c20, and the six talk-seen bytes 0x450f430..0x450f435.
