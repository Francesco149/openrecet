---
name: lapis-100f-long-intro
start: save:15b20dc3c34e
steps:
  - on Lapis 100F: floor == 99
  - event 12/8 playing: sid == 12008 and dlg == 1
  - the golem fight: s32(0x438b8a0) == 10
  - Lapis 100F cleared: s32(0x438b8a0) == 23
  - back in the shop with control: scene == 1 and dlg == 0 and stage < 4
forbid:
  - game over: scene == 2
---
Probe: **the long intro of Lapis 100F** — floor 99's stages 0..3 and events 12/8, 12/9, 12/13, 12/14, 12/15 (`FUN_0044ecc4`
/ `FUN_0044f13d`, `docs/findings/bosses/arma-golem.md` §1), which play when bank `+0x2bcff` == 0. `lapis-91f-100f` took the short
intro (the crafted lineage had `+0x2bcff` = 1). The port has these stages ported but never run against a recording.

**Start save (crafted):** `15b20dc3c34e` = `lapis-81f-90f`'s end save `6809401b79a9` (Day 31 morning, Louie L46, slot 020) with
bank dword `0xaf3f` 0x01000000 → 0 (byte `+0x2bcff` cleared). `tools/craft_pix_save.py --base 6809401b79a9 --set 0xaf3f=0`. Say so.

Read `docs/findings/probes/lapis-91f-100f.md` first (the guild, the rows — "96～100" is listed —, the 95F knight mob floor, the 99F
exit event 12/21, the golem fight recipe and its traps: the hands' lanes at x ≈ −20 / −60, breaks every 490 HP).

Do: (1) guild → hire → pack → Lapis "96～100" → 96F..99F (event 12/21 at the 99F exit) → 100F. (2) Stage 0: the corridor walk
(x clamped −41..−39); `pz > −67` → event 12/8; then 12/9 and whatever follows (12/13..12/15): every stage / tick / `lock` / `dlg`
change, every line's text and speaker, `rd ram mark/diff` across each event (bank +0x2bcf0..+0x2bd20, +0x2d600..+0x2d700). (3) Beat
the golem as before. (4) Events 12/16..12/19, CLEAR!, town (12/20 again? report), end the day, Save over slot 020, `rd endsave`.

Report: the long intro's frames / text / state changes, what differs from the short path, the end save sha12, `rd verify` + `--real`.
