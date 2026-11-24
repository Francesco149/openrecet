---
name: shop-story-tielle
start: amber-26f-30f-tielle@34699
steps:
  - loaded with control: scene == 1 and dlg == 0
  - Tielle's wake-up scene 4/4: sid == 4004
  - Tielle's wake-up scene 4/5 (sets bank + 179390): sid == 4005 and u8(bank + 179390) == 1
  - Tielle joins in the shop, scene 4/6 (story probe): sid == 4006
  - the scene over, the shop running: cc08 == 4 and dlg == 0 and sid == 4006
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Tielle's arc after Amber Garden 30F → she joins** (`docs/findings/shop-story-scenes.md` — read it first: the
story probe, eligibility, closeness slots). Played lineage: `amber-26f-30f-tielle@34699` (7 ticks before 4/4 plays on arriving home; its end save
`fc2e4d81f26d` already has 4/4 played — start from fc2e only for 4/5 on). Back in town set `0x2bcbc`; arriving home plays **4/4** (sets
`0x2bcbd`), the following tod-0 (morning) wake-up plays **4/5** (sets `bank + 179390` = `0x2bcbe`). Then Tielle
(customer 5, closeness slot 3 = int16 `bank + 0x2d21c`) is status 2 in the eligible list (morning / noon / evening);
4/6 needs her closeness > 2 (≥ 3 points: +1 / +2 / +5 per deal — a sale at exactly her target price gives most) and
plays at a session's first customer with 5 eligible: sets +179391 and the roster byte `0x2bc4f` (Tielle joins).

Do: play into the next mornings (sleep / pass periods; a session opened at night ends into the next day), watch `sid`
for 4004 and 4005 (`rd tap R "A" --text` for transcripts). Then open the store each period and serve Tielle whenever she
comes (eligible list `rd ram R dwords 0x6a5d450 20`; the visit queue `0x730aca0`, 6 dwords per entry, count
`0x730ac98`; `s32(0x730b56c)` = the customer in service — 5 is Tielle). Re-time Open Store (checkpoint before it, a few
idle ticks differ → another list) when 5 is not listed. Sell her what she asks at `s32(0x730b588)` (the target). Once her
closeness reads ≥ 3, the next first customer with 5 eligible should play 4/6. Other story scenes may fire first at
first customers (probe order: Charme, Louie, Nagi, Griff, Arma, **Tielle**, Elan, Caillou, Alouette) — record and pass them.

Record: frames + transcript excerpts of 4/4, 4/5, 4/6 and any other scene; bytes changed (`rd ram R mark` / `diff --lo
<bank+0x2bc00> --hi <bank+0x2bd40>`); Tielle's closeness per deal. Then save in game and `rd endsave R` (the next arc —
Griff's 7/1 wake-up needs Tielle met — starts from it). If 4/4 or 4/5 do not come within 3 mornings, report the bytes
`0x2bcbb..0x2bcbf` each morning and stop.
