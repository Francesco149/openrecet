#!/usr/bin/env python3
"""Host tests for craft_pix_save.craft --loadout (tools/craft_loadouts.py) on a synthetic save arena."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import craft_loadouts as cl  # noqa: E402
import craft_pix_save as cps  # noqa: E402
import trace_save  # noqa: E402

BANK = 19


def arena() -> bytearray:
    buf = bytearray(trace_save.SAVE_ARENA_BYTES)
    struct.pack_into("<I", buf, cps.H_LAST_SLOT * 4, BANK)
    base = cps.HEADER_BYTES + BANK * cps.STRIDE_BYTES
    struct.pack_into("<I", buf, base + cps.F_MAGIC * 4, cps.MAGIC)
    struct.pack_into("<20000I", buf, base + 24, *([0xffffffff] * 20000))
    rec = base + cl.CHARA_BASE_DWORD * 4
    struct.pack_into("<11i", buf, rec, 47, *([(2 << 6) | 0x20, (1201 << 6) | 0x20, (1501 << 6) | 0x20,
                                              (2301 << 6) | 0x20, -1] * 2))
    struct.pack_into("<4i", buf, rec + 44, *cl.level_stats(47))
    struct.pack_into("<I", buf, base + cps.F_CHECKSUM * 4, cps.checksum(buf, base))
    return buf


def main() -> int:
    fails = 0

    def check(cond, msg):
        nonlocal fails
        if not cond:
            fails += 1
            print("FAIL:", msg)

    # the real save's Louie (dword 47) reads AT DF MA MD 49 49 25 34
    check(cl.level_stats(47) == (49, 49, 25, 34), f"level_stats(47) = {cl.level_stats(47)}")
    # bossrush-lapis: L47 + Longsword, Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots = ATK 66 / DEF 85 (live)
    atk = cl.level_stats(47)[0] + 14 + 3
    dfn = cl.level_stats(47)[1] + 14 + 9 + 8 + 5
    check((atk, dfn) == (66, 85), f"live bossrush-lapis gear model {atk}/{dfn}")
    # plus goes to the largest stat
    check(cl.slot_stats(11, 3) == [113, 0, 26, 25], "plus on the weapon's ATK")
    check(cl.slot_stats(1408, 2) == [8, 26, -5, 22], "plus on the armband's DEF")

    for name, p in cl.PRESETS.items():
        check(cl.expected(name) == p["expect"], f"{name}: expected {cl.expected(name)} != table {p['expect']}")
        check(len(p["equip"]) == 5 and p["equip"][0][0] < 100, f"{name}: slot 0 must be a sword")
        # Louie's class groups: at most one per group, no necklaces / capes
        groups = []
        for iid, _ in p["equip"][1:]:
            cat = iid // 100
            check(cat not in (21, 22), f"{name}: {iid} not wearable by Louie")
            groups.append({10: 0, 11: 0, 12: 0, 13: 0, 14: 1, 15: 1, 16: 2, 17: 2, 20: 3, 23: 4, 24: 5, 25: 6}[cat])
        check(len(set(groups)) == 4, f"{name}: two items of one group")
        for boss, hp, hits, worst, interval in cl.boss_rows(name):
            check(hits <= 45, f"{name}: {boss} needs ~{hits} hits")
            if interval:
                check(worst < interval, f"{name}: {boss} worst hit {worst} >= interval {interval}")
        gname, gdf, gpow = p["gate"]
        check(cl.sword_v(p["expect"][0], gdf, gpow) == 1, f"{name}: gate '{gname}' broken")

        buf = arena()
        bank, notes = cps.craft(buf, loadout=name)
        base = cps.HEADER_BYTES + bank * cps.STRIDE_BYTES
        rec = base + cl.CHARA_BASE_DWORD * 4
        r = struct.unpack_from("<15i", buf, rec)
        check(r[0] == p["level"], f"{name}: level dword {r[0]}")
        want = [cl.slot_value(i, n) for i, n in p["equip"]]
        check(list(r[1:6]) == want and list(r[6:11]) == want, f"{name}: slots {r[1:11]}")
        check(r[11:15] == cl.level_stats(p["level"]), f"{name}: record stats {r[11:15]}")
        inv = [v for v in struct.unpack_from("<40I", buf, base + 24) if v != 0xffffffff]
        check(inv == cl.food_items(name), f"{name}: inventory {inv}")
        check(cps.checksum(buf, base) == struct.unpack_from("<I", buf, base + cps.F_CHECKSUM * 4)[0],
              f"{name}: checksum")

        buf = arena()
        cps.craft(buf, loadout=name, keep_level=True)
        check(struct.unpack_from("<i", buf, rec)[0] == 47, f"{name}: --keep-level changed the level")

    print("ok" if not fails else f"{fails} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
