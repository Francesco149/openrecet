---
name: lapis-60f-event-12-4
start: save:b4766a99d74b
steps:
  - on Lapis 60F: floor == 59
  - event 12/4 playing: sid == 12004 and dlg == 1
  - back in the shop with control: scene == 1 and dlg == 0 and stage < 4
forbid:
  - game over: scene == 2
---
Probe: **event 12/4** — the one-time scene after the Lapis 60F rat (type 60) dies, gated by `bank+0x2bcf7` == 0
(`src/dungeon_flow.c` ≈2012: iv12_4 at event tick 120, then the flag is set). No recording has played it: the
`lapis-51f-60f` lineage already had the flag set.

**Start save (crafted):** `b4766a99d74b` = `lapis-51f-60f`'s end save `78c391141547` (Day 28 morning, Louie L44,
slot 020) with bank dword `0xaf3d` 0x01010101 → 0x00010101 (byte `+0x2bcf7` cleared, `+0x2bcf4..f6` kept).
`tools/craft_pix_save.py --base 78c391141547 --set 0xaf3d=0x00010101`. Say so in the report.

Read `docs/findings/probes/lapis-51f-60f.md` first: the guild hire and packing, the rows ("56～60" row is the
one), the 60F rat fight (purple-mushroom low-defence windows, damage scale 3.0) and the 60F return door (walk +x
(`L`) from the −x side at (−45.55, −39.20)).

Do: (1) guild → hire Louie → pack → Lapis "56～60" → walk 56F..59F → 60F. (2) Kill the rat. (3) Let the scene
play: `estage` / `sid` / `dlg` per change, every line's text (A taps), where the party is put, whether the
portals appear, the frame `+0x2bcf7` turns 1. (4) Return door → Yes → results → town → end the day → next
morning → pause-menu Save over slot 020, `rd endsave`.

Report: the event's frames and full text, state changes around it (`estage`, `gate`, `lock`, `+0x2bcf7`), the
end save sha12, `rd verify` + `--real` lines.
