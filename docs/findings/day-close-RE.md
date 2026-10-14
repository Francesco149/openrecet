# Shop day close — the night exchange and the day-end summary

Retail RE (IDA `all_ida.c` + objdump), measured under rhost on
`buy-counter-raise-accept` (day 5, 4th customer of the day leaves at f7156).
Port: `src/scene1_day_close.c`, `src/scene1_actor_say.c`, `src/speech_bubble.c`,
`src/scene1_postload.c` (`scene1_postload_stage_entry_init`).

## 1. The 4th period ends the day

Shop-time `DAT_0450fb88` (work dword +0x2c3f0) counts the periods spent today.
Three sites advance it and, at 4, start the night:

| Site | Trigger | Code |
|---|---|---|
| cs leave, `FUN_00462403` (all_ida 65506-65565) | dissolve done, a real sale (`f404 == 0`) | `++fb88; FUN_0045e028();` then `fb88 < 4` → the hop-down seat, else `fb88 = 4; b4e0 = 1; FUN_0048526d();` |
| free-roam, `FUN_0048670f` @0x486c87 | `cc08 == 1 && cc04 == 0 && work +0x2ce08 == 1` (a period passed in town) | clear the flag, `++fb88`, `rand() % 5 == 0 && day >= 9` → news (the rand is drawn every time), `FUN_0045e028`, at 4: `fb88 = 4; b1c0 = 1; work +0x2cdf4 = 0; b4e0 = 1; FUN_0048526d()` |
| dungeon exit, `FUN_00442cef` (all_ida 44935-44965) | leaving a dungeon: `++fb88` (+1 more if +0x2ce08), `FUN_0045e028` | `< 4` → the world map (b1c0 8), else `b4e0 = 1; b4dc = tier; FUN_0048526d(); b1c0 = 1; work +0x2cdf4 = 1` (dungeons unported) |

`FUN_0045e028` clears the 4 per-period event-checked bytes at work +0x2bca8.

## 2. `FUN_0048526d` — the in-scene re-layout

`FUN_00436f97()` (the full stage-entry init, run without a load: floor layout
`FUN_0047df4d` + actor reset `FUN_0041f252` draw rand, `FUN_0046f1e9` re-arms
the window-NPC warmup, db054 = 0, cc08 = 1, …), then `FUN_004851e2(1)` (actors
at the front door), Recette faces Tear (`tear.x <= player.x` → octant 2 / −π/2,
else 6 / +π/2), state/anim 6 (the tired pose) unless already 6, `cc08 = 1`,
`db048 = 15`, `b924 = 300`.

Render side of the re-layout: `FUN_00436f97` sets `DAT_0438cc68 = 1`, the camera
smoothing's snap flag (`FUN_00441c3e` copies the target eye/lookat instead of the
0.2 lerp), so the view cuts to the door. With shop-time 4 the map light
(`FUN_00458f67` mode 3) takes its closed-shop preset: dir (1,0,0), diffuse
(0.3,0.3,0.6), ambient (0.2,0.2,0.8) — not the night table row.

`FUN_0048670f` re-runs it while `b4e0 == 1` (asm 0x486d04), so on the frame
after the arm the floor layout and actor reset draw again (retail f7157: 56
draws, f7158 the second warmup).

## 3. The exchange — `DAT_0438b4e0` 2..160

At asm 0x486e27 (after `b924++`), while `b4e0 > 0`: `++b4e0` (held while
`b928 == 2 && b924 < 200`, the world-map return — FUN_0049e163's home arm; `v6`
keeps the un-incremented b4e0, so the `v6 == N` beats repeat while held), at 2 the variant
`DAT_074b2eec = (+1) % 3`. Lines (via the actor-say driver, §4):

| variant | b4e0 10 (Recette, bubble left) | close | reply (Tear, bubble right) |
|---|---|---|---|
| 0 | 300 "I'm tiiiired..." + 00re_sys02a | 79 | 90: 301 "Go rest, then.…" + 01ti_sys02 |
| 1 | 302 "Thanks for all your help today, Tear!" + 00re_sys02b | 79 | 90: 303 "You also did well.…" + 01ti_sys02 |
| 2 | 304 "G'night!" + 00re_sys02c | 69 | 80: 305 "Good night." + 01ti_sys03 |

Durations 100. At 160: `FUN_004536b9` (= `FUN_00453384(1)`, the day-end
summary as pause action 1), `b4e0 = 0`, `cc08 = 1`, `FUN_00406212(0)`.
Every b4e0 frame leaves through LABEL_132: `FUN_004897c6`, `FUN_0048a833`
(no `FUN_0048b850` → db054 frozen; Tear takes the face-the-player branch,
`v39 = (db048 == 15)`, and emits her wing sparkle every frame since db054 = 0),
`FUN_00486435`, `FUN_00485861`.

`b4e0 != 0` also gates the window-NPC spawns (`FUN_0046f2a3`) and both their
renders (`FUN_0046f648` / `FUN_0046f737`), and blocks the ESC pause
(`FUN_00453384` with action 0 → SE 0x16a).

Measured (rhost, buy-counter-raise-accept): fb88 4 + b4e0 1 @f7156, variant 1,
b76c 302 @f7165 (b764 2), 303 @f7245, b4e0 0 @f7315, scene 9 @f7317.

## 4. The actor-say driver and the speech bubble

`FUN_00485413(msg, arg, actor, side, dur)` sets `DAT_0438b758..b76c`.
`FUN_00485861` (the FUN_0048670f tail; not a rumble): text = `FUN_0048544a()`;
on the first tick `FUN_00406159(412, 112, 0, text)`; `++b764`; at the duration
`b764 = 0; FUN_00406212(1)`; then the bubble follows the speaker: project
(actor + 1.5 up, Tear 0.5), side 1 → mirrored tail at (sx − 192, sy − 16), side 0
→ (sx + 48, sy − 48); text scale 0.4.

`FUN_00406159` splits on `<` (+3: "<BR>") into ≤ 4 lines, SE 0x174 on the first
pop since a reset else 0x172. `FUN_00406584`'s head ages it (`648258++`,
`64825c++`), the timed variant closes at 180, the close counter runs to 10.
Draw `FUN_0040c4eb` (HUD aggregator, before `FUN_00409925`): item_win.tga
balloon (0,608)-(280,703), pop scale min(pop/10, 1), text after age 10.

## 5. The day-end summary (pause action 1)

Worker case 9 with `DAT_06a4997c == 1`: `FUN_0048da71` (init: profit
`gold − day-start gold`, score total += today's, the rating stamp, b928 = 1,
b924 = 0) + `FUN_00473618` (choushuu.tga, result_board, hanko.tga). Tick
`FUN_0048dbfb` (count-ups draw 1 rand / frame; Z after 130 → day++ and the
payment check / next day), render `FUN_0048ef00`. Retail's first summary draw
is f7363 on buy-counter-raise-accept.

## 6. The next morning — the b928 beat

Z after the count (s_after > 130): day++, the payment check (work byte +0x2bc4b,
or today == the period end) → the payment screen, else the exit fade → FUN_0048d954
(day-start gold, today's score and lists reset), shop-time 0, b4e0 0, clock phase 0,
FUN_0045e67e, then FUN_004536b9 (close), FUN_0048da71 (re-init: b928 = 1, b924 = 0),
work +0x2cdf4 = 0, b1c0 1, FUN_004852fb (counter seat, pose 6 facing Tear, db048 0).
Day 8 / 36 (story mode) or a short payment take the story / game-over scene instead.

The morning is the same DAT_0438b928 beat as the tutorial's iv2_5 → iv2_6 pause:
b928 stays 1 (cleared only by a game load FUN_0049a59e or dungeon entry FUN_0045c051);
b924 counts master ticks. While b924 < 200 FUN_0048670f's cc08 1 arm takes LABEL_448
→ Recette faces Tear in pose 6 → FUN_0048a833 (v39 = b928: face branch) → LABEL_508,
no FUN_0048b850 (db054 frozen, so Tear's wing sparkle fires every frame); FUN_0044bd0d
withholds the story cascade while b924 < 0xbe; the "Day N" card draws while b924 < 0x8c;
the merchant-rank arrivals wait for 192. Greetings (DAT_074b2ee8 toggles at b924 1):
00re_sys01a @50 + 01ti_sys01a @110, or 01ti_sys01b @50 + 00re_sys01b @150.
Measured (day-end-next-morning): Z f7600, house f7625, hold ends f7824 (b924 200),
Tear's first wander f7914.
