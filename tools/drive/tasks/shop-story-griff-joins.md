---
name: shop-story-griff-joins
start: save:e26d0d428437
steps:
  - loaded with control: scene == 1 and dlg == 0
  - Griff's shop scene 7/9 (after Obsidian 50F; story probe): sid == 7009 and u8(bank + 0x2bcea) == 1
  - Griff joins, scene 7/10: sid == 7010 and u8(bank + 0x2bc52) == 1
  - the scene over, the shop running: cc08 == 4 and dlg == 0 and sid == 7010
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Griff's last two shop scenes, 7/9 and 7/10 (he joins)** (`docs/findings/shop-story-scenes.md` — read it first).
**Crafted start:** `e26d0d428437` = `f795f8712b3b` (the end of `shop-story-tielle-market`, played lineage, day dword 29
evening, slot 023 — `.*60 A .*30 A` loads the last-used slot) `--set 0xaf39=0x01000000`: bank byte `0x2bce7` = 1, the
byte Obsidian Tower 50F sets when all actors are gone (`sub_4427F1`). Nothing else changed — the Obsidian trip itself is
not in this lineage, so say "crafted" in the report.

With `0x2bce7` == 1 and `0x2bcea` == 0, Griff (customer 8) is forced into the eligible list (evening / night — his
time mask; verify from the lists); at a session's first customer the probe plays **7/9** (sets `0x2bcea`). Then he is
status 2; **7/10** needs `0x2bc52` == 0 (yes) and his closeness (slot 6 = int16 `bank + 0x2d228`) > 2 — a deal at his
target `s32(0x730b588)` gives +5 — and plays at a later first customer with 8 eligible (sets `0x2bc52` = he joins).

Do: open the store in the evening (now) or night; re-time Open Store when 8 is not listed (`rd ram R dwords 0x6a5d450 20`).
After 7/9, serve Griff when he visits (`cust == 8 and b534 == 1`), sell at his target; then the next first customer with
8 eligible → 7/10. Record frames + transcript excerpts, bytes changed, his closeness per deal. Save in game at the end
(a new slot), `rd endsave R`.
