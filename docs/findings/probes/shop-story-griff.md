# Probe: shop-story-griff

- **Run:** `ssg-1` → scenario `shop-story-griff` · **start:** save:d7c10c7e06df (`shop-story-tielle`'s end; played lineage) · **task:** `tools/drive/tasks/shop-story-griff.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/shop-story-griff/` (28,599 frames); end save `3228c6dabd3d` (day dword 28 morning, slot 022)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f28599, rng 5d753e9e); golden + triage: **identical**; gate row. No port change.

Griff's arc to Obsidian Tower opening, on the played lineage (`docs/findings/shop-story-scenes.md` 7/4, 7/5 rows; the
wake-up 7/1 and the plaza events 7-2 / 7-3).

| frame | where / when | sid | bytes |
|---|---|---|---|
| 552 | plaza, d24 evening | 14005 | `+0x2bca8` 0 → 1 (not this arc) |
| 1441 | plaza, d24 night | 16003 (filler: the plaza's daily event used) | — |
| 2210–3634 | house, d25 wake-up | **7001** | `0x2bcdd` 0 → 1 (at the scene start) |
| 4258 | market, d25 morning | 17014 (Tielle) | `0x2bca9` 0 → 1 |
| 6742–8825 | plaza, d25 night | **7002** | `0x2bcde` 0 → 1 |
| 11127–15044 | plaza, d26 night | **7003** | `0x2bcdf` 0 → 1 |
| 16992–18955 | shop, d27 evening, first customer | **7004** (Griff) | `0x2bce0` 0 → 1 |
| 23817–25549 | shop, d27 night, first customer | **7005** (Louie: "The guild's discovered a new dungeon! A BIG one!") | `0x2bce1` 0 → 1 |

- **One plaza / market event per day:** `bank + 0x2bca8` (flag 0x60, plaza) / `0x2bca9` (0x61, market), set by any event
  there, cleared at the day change; while set the plaza plays the filler 16003. A morning plaza visit spends that day's.
- Periods: each visit elsewhere raises `stime` by one on re-entering home; an empty Chapel visit (16005, ~170 ticks) is
  the cheapest; re-entering home at stime 3 ends the day (no store after a night outing).
- After 7/1 movement / ESC stay locked under a white overlay until an A.
- `wms` = 2 does not promise an event (Town Square read 1 on d24 night yet played 16003).
- Tool gaps: `rd ram diff --lo 0x…` traceback (bare hex works); `search --apply` paths are unrefined ~250-frame detours.
