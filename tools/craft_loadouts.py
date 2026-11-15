"""tools/craft_loadouts.py — boss-test loadout presets for `craft_pix_save.py --loadout NAME`.

A preset writes Louie's (chara 0) equipped gear and level into the save, and puts a consumables list into the
inventory (the driver packs it into the bag at the guild). Purpose: late bosses in reasonable time (about 25..40
sword hits for the band's biggest boss) while no single hit comes near an HP-threshold arm (a break, a phase).
CRAFTED state: say so in the task (tools/drive/SAVES.md).

Where the save holds the gear (bank dwords; IDA `unk_4510648` + bank·0x2dfc8 + chara·0x6c):
  chara record k = dword 0xb3ac + 27·k (Louie k = 0):
    +0       level, 0-based (HUD / guild shows +1)
    +1..+5   equipped slots `id << 6 | 0x20 | plus` (−1 empty); slot 0 = the weapon
    +6..+10  a second copy of the five (the saves hold the same values; written identically here)
    +11..14  AT DF MA MD from the level: ftol(L0 + (L100 − L0)·level/100) (`sub_47A8C0`, rewritten at every Depart)
  0x20 = "the adventurer's own": `sub_442C86` (dungeon return) drops 0x20 items from the bag and sends equipped
  items WITHOUT 0x20 (bag gear equipped at "Equip items in bag?") back to the bag. So the bag route is per-trip;
  the 0x20 route persists — the state an adventurer reaches by buying gear from Recette's shop (the customer-
  adventurer arm, IDA ~68320, compares the offered item with the record's slots).
  plus = low 4 bits (0..15): `sub_48093F` adds it to the item's largest stat; fusion output plus = the
  ingredients' pluses, capped at 15 (merchant-guild-RE.md); bit 4 (0x10) = unappraised.
  ATK / DEF in a dungeon = record AT / DF + Σ slot stats (+ plus) = s32(0x56db0ac) / s32(0x56db0b0) (`sub_4844EF`).
  Wearable (`sub_468DDC`): the item's audience bit for the chara and its class in the chara's list
  (`dword_5C6F04[20·k]`; Louie: 1 swords | 9 clothes 11 breastplates 12 armor | 13 bracelets 14 shields |
  15 hats 16 helms | 17 scarves 18 shoes 20 charms 22 rings — no necklaces / capes); slot 0 = class list[0];
  at most one of each group {9,10,11,12} {13,14} {15,16} (`sub_49ED12`).

Damage model (findings/bosses/combat-core.md §3): v = ftol(ATK/2 − ftol(DF·pow²)/4); a front sword hit = v + rand
% (v/5) ≈ 1.1·v; worst sword hit = counter 1.2 · back 1.5 · crit 2 · rand 1.2 = 4.32·v. Spin (`B`) = 2× the sword:
it can pass the smaller intervals below — keep to the sword where the column says so.

| preset | Louie (dword → HUD) | gear (all +0) | ATK / DEF / MATK / MDEF | meant for | biggest boss: hits | smallest arm interval vs worst sword hit |
|---|---|---|---|---|---|---|
| early | 14 → L15 | Steel Sword, Iron Armor, Spiked Armband, Warrior's Helm, Leather Boots | 44 / 57 / 10 / 23 | Jade Way + Amber Garden bosses; Jade/Amber Boss Rush (d5 30..37) | crabs d2 20F 578 (state-6 DF 30): ~38 | crown slime power gate: 1 dmg at power 3.66 (held) |
| mid | 31 → L32 | Breaker Sword, Silver Armor, Spiked Armband, Silver Helm, Hard Boots | 84 / 105 / 18 / 44 | Obsidian Tower 1..50F; Obsidian Boss Rush (d5 38..45); Lapis 25F / rush 50F crown trios | Griff rush 45F 1640: ~44; Griff 50F 1320: ~36 | jellyfish 10F 460 → 153 per third vs 146 |
| late | 47 → L48 | Holy Sword, Guardian Armor, Superarm Band, Alleus, Battle Boots | 169 / 215 / 48 / 121 | Lapis Ruins 1..100F (not 25F), Crystal Nightmare 1..30F | Arma golem 100F 2940 (DF 35): ~36 | red jellyfish 40F 1110 → 370 vs 324; golem break 490 |
| rush | 47 → L48 | Holy Sword, Legendary Armor, Superarm Band, Legendary Helm, Iron Sandals | 181 / 278 / 66 / 129 | Lapis Boss Rush (d5 46..58, not 50F) | Arma golem 58F 3540 (DF 35): ~40 | red jellyfish 52F 1060 → 353 vs 349; golem break 590 |

Kept / softened gates: crown slime (DF 8·pow²) holds at early / mid only (late: 34 per hit at power 5) — use mid
for crown floors; crab DF 750 outside state 6 holds everywhere; mushroom-rat DF 42·pow² (type 59 pow 2: late
gives 42 outside the DF-0 window, mid 0); the golem's HP gate clamps at the segment floor (a hit never skips a break).
Sources: item stats `data/item.txt` (read at runtime, not committed); chara rows `data/chara.txt`; boss HP / DF
findings/bosses/ROSTER.md and the cards.
"""
from __future__ import annotations

import struct

CHARA_BASE_DWORD = 0xb3ac
CHARA_STRIDE = 27
OWN_FLAG = 0x20

# Louie's chara.txt rows: level-1 and level-100 AT DF MA MD (the interpolation inputs).
LOUIE_L0 = (10, 10, 4, 4)
LOUIE_L100 = (93, 95, 50, 68)

# id: (name, ATK, DEF, MATK, MDEF) — the items the presets use.
ITEMS = {
    3: ("Steel Sword", 20, 0, 0, 0),
    6: ("Breaker Sword", 46, 3, 0, -5),
    11: ("Holy Sword", 110, 0, 26, 25),
    1302: ("Iron Armor", 0, 14, 0, 2),
    1305: ("Silver Armor", 0, 32, 0, 8),
    1310: ("Guardian Armor", 0, 80, 0, 20),
    1311: ("Legendary Armor", 4, 98, 10, 24),
    1403: ("Spiked Armband", 3, 9, 0, 8),
    1408: ("Superarm Band", 8, 24, -5, 22),
    1702: ("Warrior's Helm", 0, 8, 0, 1),
    1705: ("Silver Helm", 0, 16, 0, 10),
    1710: ("Alleus", 0, 38, 0, 20),
    1711: ("Legendary Helm", 0, 47, 0, 24),
    2402: ("Leather Boots", 0, 5, 0, 0),
    2403: ("Hard Boots", 0, 9, 0, 0),
    2405: ("Battle Boots", 2, 24, 2, 0),
    2412: ("Iron Sandals", 10, 60, 10, 0),
}
# id: (name, HP, SP) — Market food (市 > 0).
FOOD = {
    4001: ("Apple", 15, 0), 4005: ("Watermelon", 60, 0), 4006: ("Melon", 250, 0), 4012: ("Beef Bowl", 50, 0),
    4014: ("Ham Sandwich", 120, 0), 4015: ("Curry", 200, 0), 4029: ("Baked Yam", 0, 50), 4034: ("Mont Blanc", 0, 150),
}

# boss rows: (name, HP, DF, power, arm interval or None, note)
PRESETS = {
    "early": {
        "level": 14,
        "equip": [(3, 0), (1302, 0), (1403, 0), (1702, 0), (2402, 0)],
        "food": [(4012, 3), (4005, 3), (4001, 6), (4029, 2)],
        "expect": (44, 57, 10, 23),
        "for": "Jade Way + Amber Garden bosses; Jade/Amber Boss Rush (d5 30..37)",
        "bosses": [
            ("Charme d1 15F", 280, 20, 1.0, None, ""),
            ("Ohm d2 10F", 420, 25, 1.0, None, ""),
            ("crabs d2 20F (state 6)", 578, 30, 1.0, None, "DF 750 outside state 6"),
            ("crown slime d2 25F (each)", 380, 8, 1.0, None, "power gate"),
            ("Tielle d2 30F", 400, 30, 1.0, None, ""),
        ],
        "gate": ("crown slime at power 3.66", 8, 3.66),
    },
    "mid": {
        "level": 31,
        "equip": [(6, 0), (1305, 0), (1403, 0), (1705, 0), (2403, 0)],
        "food": [(4014, 4), (4012, 4), (4005, 4), (4029, 3)],
        "expect": (84, 105, 18, 44),
        "for": "Obsidian Tower 1..50F; Obsidian Boss Rush (d5 38..45); crown trios (Lapis 25F, rush 50F)",
        "bosses": [
            ("jellyfish d3 10F", 460, 30, 1.0, 460 // 3, "phase per third"),
            ("golem d3 20F (each part)", 750, 30, 1.0, None, ""),
            ("Griff d3 30F", 1120, 30, 1.0, None, ""),
            ("black knight d3 45F", 640, 20, 1.0, None, ""),
            ("Griff d3 50F", 1320, 30, 1.0, 1320 * 40 // 100, "70 % / 30 %"),
            ("Griff rush 45F", 1640, 30, 1.0, 1640 * 40 // 100, ""),
        ],
        "gate": ("crown slime at power 5", 8, 5.0),
    },
    "late": {
        "level": 47,
        "equip": [(11, 0), (1310, 0), (1408, 0), (1710, 0), (2405, 0)],
        "food": [(4015, 4), (4014, 4), (4006, 2), (4034, 2), (4029, 3)],
        "expect": (169, 215, 48, 121),
        "for": "Lapis Ruins 1..100F (crown 25F: mid), Crystal Nightmare 1..30F",
        "bosses": [
            ("red jellyfish d4 40F", 1110, 35, 1.0, 1110 // 3, "phase per third"),
            ("Arma d4 50F", 1950, 35, 1.0, None, ""),
            ("dark golem d4 70F (each)", 1290, 30, 1.0, None, ""),
            ("black crab d4 90F (state 6)", 1410, 30, 1.0, None, "DF 750 outside state 6"),
            ("Arma golem body d4 100F", 2940, 35, 1.0, 490, "break per sixth (clamped)"),
        ],
        "gate": ("crab outside state 6", 750, 1.0),
    },
    "rush": {
        "level": 47,
        "equip": [(11, 0), (1311, 0), (1408, 0), (1711, 0), (2412, 0)],
        "food": [(4015, 5), (4014, 4), (4006, 2), (4034, 2), (4029, 3)],
        "expect": (181, 278, 66, 129),
        "for": "Lapis Boss Rush (d5 46..58; crown trio 50F: mid)",
        "bosses": [
            ("pumpkin ghost rush 51F (each)", 1560, 20, 1.0, None, ""),
            ("red jellyfish rush 52F", 1060, 35, 1.0, 1060 // 3, "phase per third"),
            ("Arma rush 53F", 2700, 35, 1.0, None, ""),
            ("dark golem rush 56F (each)", 1710, 30, 1.0, None, ""),
            ("black crab rush 57F (state 6)", 1740, 30, 1.0, None, ""),
            ("Arma golem body rush 58F", 3540, 35, 1.0, 590, "break per sixth (clamped)"),
        ],
        "gate": ("crab outside state 6", 750, 1.0),
    },
}


def level_stats(level: int, l0=LOUIE_L0, l100=LOUIE_L100) -> tuple[int, ...]:
    """`sub_47A8C0`: ftol(L0 + (L100 − L0)·level/100) per stat (non-negative growth → integer truncation)."""
    return tuple(a + (b - a) * level // 100 for a, b in zip(l0, l100))


def slot_stats(item_id: int, plus: int) -> list[int]:
    """`sub_48093F`: the item's 4 stats, plus added to the first strict maximum (when positive)."""
    s = list(ITEMS[item_id][1:])
    best, idx = 0, -1
    for i, x in enumerate(s):
        if best < x:
            best, idx = x, i
    if idx >= 0:
        s[idx] += plus & 0xf
    return s


def expected(name: str, level: int | None = None) -> tuple[int, int, int, int]:
    p = PRESETS[name]
    tot = list(level_stats(p["level"] if level is None else level))
    for iid, plus in p["equip"]:
        for i, x in enumerate(slot_stats(iid, plus)):
            tot[i] += x
    return tuple(tot)


def sword_v(atk: int, df: int, power: float = 1.0) -> int:
    """Unlabelled sword damage before the random term (min 1)."""
    return max(int(atk / 2 - int(df * power * power) / 4), 1)


def boss_rows(name: str):
    """(boss, HP, ~hits, worst sword hit, interval) per boss row of the preset."""
    atk = expected(name)[0]
    for boss, hp, df, pw, interval, _note in PRESETS[name]["bosses"]:
        v = sword_v(atk, df, pw)
        yield boss, hp, -(-hp * 10 // (v * 11)), int(v * 4.32), interval


def slot_value(item_id: int, plus: int = 0) -> int:
    return (item_id << 6) | OWN_FLAG | (plus & 0xf)


def apply(buf: bytearray, base: int, name: str, keep_level: bool = False) -> list[str]:
    """Write Louie's level (+ the AT DF MA MD it gives) and the 5 slots (both copies) into the bank at byte `base`."""
    p = PRESETS[name]
    rec = base + CHARA_BASE_DWORD * 4
    notes = []
    if not keep_level:
        old = struct.unpack_from("<i", buf, rec)[0]
        struct.pack_into("<i", buf, rec, p["level"])
        struct.pack_into("<4i", buf, rec + 11 * 4, *level_stats(p["level"]))
        notes.append(f"Louie level dword {old} -> {p['level']}")
    for k, (iid, plus) in enumerate(p["equip"]):
        v = slot_value(iid, plus)
        struct.pack_into("<I", buf, rec + (1 + k) * 4, v)
        struct.pack_into("<I", buf, rec + (6 + k) * 4, v)
    level = struct.unpack_from("<i", buf, rec)[0]
    gear = ", ".join(ITEMS[i][0] + (f"+{n}" if n else "") for i, n in p["equip"])
    notes.append(f"loadout {name}: {gear}; expect ATK/DEF/MATK/MDEF {'/'.join(map(str, expected(name, level)))}")
    return notes


def food_items(name: str) -> list[int]:
    """Inventory values (id << 6) of the preset's consumables, one per unit."""
    return [iid << 6 for iid, n in PRESETS[name]["food"] for _ in range(n)]
