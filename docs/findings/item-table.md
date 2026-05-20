# `data/item.txt` — the item-record table

Block #3 of `FUN_00475270` (`tables_load_all`). Loads `data/item.txt` —
the 122 KB master table of every weapon, armour, accessory, consumable,
furniture piece, etc. that the shop deals in. ~780 lines yield ~600
populated item records (the rest are blank / comment / category-header
lines). The result is the single biggest in-memory table the engine
builds at boot: ~430 KB of record data plus ~6 KB of category-name
strings.

This file is the gating dependency for the deferred item-name → item-id
resolver hook in three already-ported parsers (`oder.txt`, `enemy.txt`,
`gousei.txt`). Once `item.txt` parses, a single resolver pass can
finally populate the ID fields those parsers currently leave at -1.

Source-level reference: `docs/decompiled/by-address/475270.c` L428..L468
plus the cross-block fallback at L815..L829 (control jumps via
`goto LAB_00476d04`). The full record parser is `FUN_004912de`
(`docs/decompiled/by-address/4912de.c`, 820 bytes).

---

## Dispatcher: a 1-byte sentinel chain spanning two blocks

Per-line dispatcher (engine, `475270.c` L437..L463 + L815..L829):

```
1. Read up to 256 bytes into local_27c[0x20..] (line scratch)
2. Skip if first byte is '\r', '\n', or '/'         (comment / blank)
3. If first byte == ':'                              [CATEGORY HEADER]
     FUN_00491044(line + 1, category_index);
     category_index += 1;
   Else fall through to LAB_00476d04 (cross-block goto into kyaku.txt block):
4. If first byte != ' '                              [ITEM RECORD]
     item_id = atoi(line[0..4]);                     # 4-digit decimal
     If line[0] is not a digit: MessageBoxA "不明な行" (unknown line);
     Else if 0 <= item_id < 10000 AND line[5] not in {'\r','\n'}:
       FUN_004912de(line + 5, item_id, record_slot);
       record_slot += 1;
5. (Otherwise: line starts with space → silently dropped)
6. Loop back via `goto LAB_00476c6f` (item.txt main loop entry).
```

The two single-byte sentinels live in `.rdata`:

| VA          | Byte | Meaning                                 |
|-------------|------|-----------------------------------------|
| `0x5cacf0`  | `:`  | Category-header prefix                  |
| `0x5cacf4`  | ` `  | Indent prefix → silently skip the line  |

The "indent skip" sentinel is purely defensive — vendor `item.txt` has
no leading-space lines, but if one ever appeared (continuation of an
overlong description, say) it would be discarded rather than
misinterpreted as a record.

**Cross-block goto.** Ghidra decompiled the dispatcher tail as a
`goto LAB_00476d04` that physically lands inside the next block's
function body (the `kyaku.txt` loader at L469+). This is a real
code-layout artifact in the binary, not a decompiler artifact: both
the item.txt category-mismatch path and the kyaku.txt loader share
the same `LAB_00476d04` epilogue. The port linearises this so
item.txt's handling stays contained in one function.

---

## Two parser entry points sharing a scratch buffer

`FUN_00491044` (81 bytes) is the **category-header** parser. It writes
the line's `before-#` and `after-#` halves into two fixed scratch
buffers at `&DAT_09642bd0` (first half) and `&DAT_09640604` (second
half). Up to 32 chars total. **The scratch is never explicitly read by
this function — the next call to `FUN_004912de` (the record parser)
picks them up.**

`FUN_004912de` (820 bytes) is the **item-record** parser. On entry it
immediately does:

```c
FUN_005038ff(&DAT_0963e5f8 + (item_id/100) * 0x20, "%s", &DAT_09642bd0);
FUN_005038ff(&DAT_0963c5f8 + (item_id/100) * 0x20, "%s", &DAT_09640604);
```

I.e. it copies the *most recently parsed category header's* before-`#`
and after-`#` halves into the per-category name table, keyed by
`item_id / 100`. This means **the order of lines in `item.txt`
matters**: each category header must precede its records, and the
records must have IDs in the matching 100-block (0000-0099 for
category 0, 0100-0199 for category 1, etc.).

The vendor file respects this convention, but a faithful port must
preserve the scratch-buffer flow — a clean per-call API can't replace
it without changing observable behaviour for adversarially-ordered
files.

---

## In-memory layout

### Category-name tables

Two arrays, both indexed by `item_id / 100`, both 32 bytes per entry:

| VA           | Stride | Count | Contents                              |
|--------------|--------|-------|---------------------------------------|
| `0x963e5f8`  | 0x20   | 100   | Category singular (e.g. `"Swords"`)   |
| `0x963c5f8`  | 0x20   | 100   | Category tag (e.g. `"(Equippable)"`)  |

The singular table is consumed by `FUN_0049ed75` (equipment-class
lookup, returns 1..0x54 for known weapon/armour subclasses) and
`FUN_0049eb2a` (weapon/armour/furniture bitmask, returns
{0x1, 0x2, 0x10000}).

### Record array

Base `&DAT_095d37d0`, stride `0x2cc` (716 bytes). The engine never
caps the slot index in the dispatcher — `record_slot` increments
unboundedly. Vendor data ships ~600 populated slots; the port reserves
a 1000-slot cap (sufficient with headroom; engine .bss reserves enough
for similar growth without observable cap).

Total count stored at `&DAT_005c80ac` after the loop completes.

Per-record fields, offsets within the 0x2cc-byte stride:

| Offset | Size | Field                       | Source                          |
|--------|------|-----------------------------|---------------------------------|
| +0x00  | 4    | `valid`                     | Set to `1` at end of parser     |
| +0x04  | 4    | `price` (i32)               | Field 3 atoi                    |
| +0x08  | 4    | `attack` (i32)              | Field 4 atoi                    |
| +0x0c  | 4    | `defense` (i32)             | Field 5 atoi                    |
| +0x10  | 4    | `magic_attack` (i32)        | Field 6 atoi                    |
| +0x14  | 4    | `magic_defense` (i32)       | Field 7 atoi                    |
| +0x18  | 4    | `aud_mask` (u32 bitfield)   | `FUN_0049e849` (target audience)|
| +0x1c  | 4    | _padding / unused_          | Zero                            |
| +0x20  | 4    | `rank` (i32)                | Field 2 atoi (if digit)         |
| +0x24  | 4    | _padding / unused_          | Zero                            |
| +0x28  | 4    | `attr_mask` (u32 bitfield)  | `FUN_00491216` + `FUN_0049eb2a` |
| +0x2c  | 4    | `equip_class` (i32)         | `FUN_0049ed75` (category name)  |
| +0x30  | 4    | _padding / unused_          | Zero                            |
| +0x34  | 4    | `item_id` (i32)             | Dispatcher                      |
| +0x38  | 4    | `category` (i32)            | `item_id / 100`                 |
| +0x3c  | 4    | `subindex` (i32)            | `item_id % 100`                 |
| +0x40  | 9    | `stock_info[9]` (u8 × 9)    | `FUN_00491095` (stock-info tags)|
| +0x49  | 1    | _padding_                   |                                 |
| +0x4a  | 0x40 | `singular[64]` (SJIS)       | Field 2, before `+`             |
| +0x8a  | 0x40 | `plural[64]` (SJIS)         | Field 2, after `+`              |
| +0xca  | 0x100| `desc_line1[256]` (SJIS)    | Phase 1 (between `##`)          |
| +0x1ca | 0x100| `desc_line2[256]` (SJIS)    | Phase 2 (after second `#`)      |

Total: 0x2ca + 1 = 0x2cb bytes used; struct padded to 0x2cc.

### Item-record parser phase machine

`FUN_004912de` runs an outer loop bounded by `iter < 0x100` (256 outer
iterations, one per consumed-or-step character). Within the loop a
phase counter (`param_1` reused as state) gates which buffer receives
data:

- **Phase 0** (name + numeric/text fields). Sub-steps:
  - If first char is a digit and rank not yet set: atoi → `rank` at
    `+0x20`, advance past `#`.
  - Plain text char: copy to BOTH `singular[]` AND `plural[]` (so they
    contain identical bytes up to the `+`).
  - `+`: switch `collecting_plural=true`, reset column counter — now
    only `plural[]` accumulates.
  - `#`: advance past separator, then 9 sequential sub-fields:
    `price`, `attack`, `defense`, `magic_attack`, `magic_defense`
    (5 atoi'd ints), then `attr_mask` via `FUN_00491216`,
    `stock_info` via `FUN_00491095`, `aud_mask` via `FUN_0049e849`.
    Each sub-field scans to the next `#` before yielding control.
    After the 9th `#` advance, transitions to phase 1.

- **Phase 1** (description line 1). Per-char copy to `desc_line1[]`,
  terminated by `#` (→ phase 2) or `\r`/`\n` (→ loop exit).

- **Phase 2** (description line 2). Per-char copy to `desc_line2[]`,
  terminated by `/`/`\r`/`\n` (→ loop exit). Note `/` is part of the
  terminator set — descriptions can't contain literal `/` without
  being truncated.

Sub-helper functions (each ~80–400 bytes):

| Function          | Body         | Field offsets it writes                |
|-------------------|--------------|----------------------------------------|
| `FUN_00491216`    | 85 B (24 lines)  | `attr_mask` (+0x28) — 10 × 4-byte tag scan; OR-merge with `FUN_0049eb2a(category)` |
| `FUN_00491095`    | 385 B (94 lines) | `stock_info[]` (+0x40..+0x48) — 7 tags scanned in 5 rounds |
| `FUN_0049e849`    | 350 B (84 lines) | `aud_mask` (+0x18) — 11 × 2-byte tag bitmask |
| `FUN_0049eb2a`    | 488 B (93 lines) | _returns_ category bitmask (weapon=1, armour=2, furniture=0x10000) |
| `FUN_0049ed75`    | 515 B (97 lines) | _returns_ equip-class id (1..0x54 / 84) |

### Attribute tag tables

**`FUN_00491216`** uses the same 16-tag SJIS attribute table at
`&DAT_005fd7fc` that `oder.txt` already references (`FUN_0049e9a7`).
Each tag is 2 SJIS chars padded to 4 bytes; up to 10 tags per record
attribute field, OR-merged into `attr_mask`.

**`FUN_00491095`** stock-info tags (each 3 or 5 bytes, tag + `(`):

| VA          | Tag (SJIS)   | Stored at | Notes                            |
|-------------|--------------|-----------|----------------------------------|
| `0x5cfb80`  | `在庫(` (5B) | +0x40     | stock count                      |
| `0x5cfb88`  | `ギ(` (3B)   | +0x41     | guild submission                 |
| `0x5cfb8c`  | `市(` (3B)   | +0x42     | market visibility                |
| `0x5cfb90`  | `買(` (3B)   | +0x43     | buy-back preference              |
| `0x5cfb94`  | `ダ(` (3B)   | +0x44..46 | dungeon spawn ×3, ×10 if value < 10 |
| `0x5cfb98`  | `卸(` (3B)   | +0x47     | wholesale (default 200)          |
| `0x5cfb9c`  | `持(` (3B)   | +0x48     | hold (carry slot)                |

**`FUN_0049e849`** target-audience tags (each 2-byte SJIS char):

| VA          | Tag    | Bits OR'd          | Notes                       |
|-------------|--------|--------------------|------------------------------|
| `0x5fd7d0`  | `全`   | `0xff`             | All audiences               |
| `0x5fd7d4`  | `リ`   | `0x01`             | Recette                     |
| `0x5fd7d8`  | `シ`   | `0x02`             |                              |
| `0x5fd7dc`  | `カ`   | `0x04`             | Caillou                     |
| `0x5fd7e0`  | `テ`   | `0x08`             | Tielle                      |
| `0x5fd7e4`  | `エ`   | `0x10`             | Elan                        |
| `0x5fd7e8`  | `ナ`   | `0x20`             | Nagi                        |
| `0x5fd7ec`  | `グ`   | `0x40`             | Guildmaster                 |
| `0x5fd7f0`  | `ア`   | `0x80`             | Arma                        |
| `0x5fd7f4`  | `男`   | `0x55` (males)     | カ + エ + グ + リ          |
| `0x5fd7f8`  | `女`   | `0xaa` (females)   | シ + テ + ナ + ア          |

Audience tags also accept a literal `#` as "all" (sets the byte to
`0xff` via `*audience |= 0xff`) — see `FUN_0049e849` L11..L13. The
combination tags (男/女) are pre-OR'd bit groups.

---

## Resolver implications (Phase B 11+ work)

Three already-ported parsers defer item-name → item-id lookup via a
`*_resolve_fn` callback that currently gets passed `NULL` from
`src/tables.c`:

- **`oder.txt`** (block #8): the attribute table at `&DAT_0963e5f8`
  is the item.txt category-singular global; `oder.txt`'s parser
  ALREADY references this table for its attribute lookup (engine
  `FUN_0049e9a7`), but never resolves attribute → item directly.
  No resolver wiring needed for oder.txt.
- **`enemy.txt`** (block #7): drop fields are SJIS item names that need
  resolution to integer IDs. Wiring: pass an `item_resolve_by_name`
  callback into `tables_parse_enemy`.
- **`gousei.txt`** (block #13): every recipe references 1 output + up
  to 5 ingredient item names. Wiring: pass `item_resolve_by_name`
  into `tables_parse_gousei` (signature already in place).

Resolver semantics: exact-match against `singular[]` at each record
(strncmp with NUL terminator). Returns the matching record's `item_id`
(NOT the slot index). Mismatch → -1.

Engine quirk to mirror: gousei.txt's resolver pops a MessageBoxA on
miss but still increments the record count. Port skips the MessageBox
(already documented in `tables_gousei.c`).

This wiring lands in a separate commit (Phase B 11/15) once the
parser is in. Keeping the resolver wiring as its own commit means the
item.txt parser milestone is reviewable on its own — no cross-cutting
changes to three other modules.

---

## Open questions / deferred

- **MessageBoxA "不明な行" (unknown line) for non-digit non-`:` lines.**
  Engine pops a dialog and continues. Port logs to stderr instead
  (matches existing precedent in `tables_enemy.c`'s unmatched-name
  path). No vendor data triggers this — confirmed during parser
  validation.
- **What's at record offset +0x1c, +0x24, +0x30?** Three 4-byte slots
  that the parser never writes but are within the 0x2cc stride.
  Likely runtime-mutable state (in-shop stock counter, etc.) populated
  by gameplay code — not by item.txt parsing. Initialised to zero by
  the port; behaviour is identical until/unless a gameplay subsystem
  needs them.
- **The 100-category cap.** `item_id / 100` indexes the category-name
  table. Vendor data uses categories 0..~50 (well under the cap); the
  port reserves 100 entries to match the engine's implicit bound.
