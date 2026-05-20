# `data/*.txt` and `idx/*.idx` — gameplay-text tables

**Status:** in progress. This file documents the per-file formats loaded
by `FUN_00475270` ("init indexfile ok") as we port each parser. See
`docs/findings/tables-loader.md` for the dispatcher-level discovery doc
(caller context, helper identities, full file list, Phase B plan).

The shared conventions across all files in this group:

- **Encoding:** Shift-JIS (cp932). Japanese characters appear in keys
  (for some files), in values, and in inline comments. ASCII keys
  cohabit with SJIS keys in the same file.
- **Line endings:** CRLF on disk. The parser treats `\r` and `\n` as
  independent line terminators, so LF-only is also handled.
- **Comments:** the engine skips any line whose first byte is `'/'`,
  `'\r'`, or `'\n'`. There is no `//`-anywhere comment — only a leading
  `/` matters. The `/key:value` form lets the developer pre-stage a
  default value as a "commented-out" line that the parser ignores
  until the leading `/` is removed.
- **Two format families:**
  - **`/key:value`** — used by `config.idx` and `buysell.txt`. Each
    real (uncommented) line is a single key:value pair. Different
    keys land in different globals, so most files have only a handful
    of keys.
  - **CSV-with-leading-comments** — used by most `data/*.txt` files
    (`item.txt`, `kyaku.txt`, etc.). The columns are documented in
    leading `//` comment lines; data rows follow. Records are
    fixed-position (atoi/atof scans from a known byte offset within
    the record), not delimited.

---

## `data/buysell.txt`

**Engine block:** `FUN_00475270` block #7
(`docs/decompiled/by-address/475270.c:1296..1377`).

**Identity:** referenced via `s_data_buysell_txt_005caf28` (size path)
and `s_data_buysell_txt_005caf3c` (read path) — same spelling both
sides, so unlike `config.idx` there is no path-mismatch quirk to
mirror.

**Port:** `src/tables_buysell.{c,h}`, parser entry point
`tables_parse_buysell(data, size, out)`. Engine-global instance
`g_buysell`. Tests in `tests/test_tables_buysell.c` (8 cases —
empty, comments-only, ok-toggle, SJIS keys, msg/rmsg arrays, no-
trailing-newline, embedded-\0 early exit, vendor-shape end-to-end).

**Purpose:** *single-customer debug override* for the buying/selling
haggling system. When `ok:` is uncommented at the top, the engine
forces the next encounter with customer `客番号` (kyaku_number) into
mode `種類` (sell/buy/about) with pre-selected dialogue branches from
the `msg%02d` / `rmsg%02d` arrays. The shipping vendor file has
`ok:` commented out, so this debug path is dormant in production.

### Keys

| key (bytes)                            | length | type | global              | C-field                  |
|----------------------------------------|--------|------|---------------------|--------------------------|
| `ok:`                                  | 3      | flag | `DAT_073dddb8`      | `debug_mode = 1`         |
| `客番号:` (`8B 71 94 D4 8D 86 3A`)      | 7      | int  | `DAT_073dddbc`      | `kyaku_number = atoi(…)` |
| `種類:` (`94 84 94 83 3A`)              | 5      | int  | `DAT_073dddc0`      | `kind = atoi(…)`         |
| `msg%02d:` (e.g. `msg00:` .. `msg19:`) | 6      | int  | `DAT_073b1a68 + 4i` | `msg[i] = atoi(…)`       |
| `rmsg%02d:` (`rmsg00:` .. `rmsg19:`)   | 7      | int  | `DAT_073b1a18 + 4i` | `rmsg[i] = atoi(…)`      |

- `kind` semantics (per the file's own comment): `0`=sell, `1`=buy,
  `2`="about" (mixed).
- `atoi` is the CRT one — it skips leading whitespace and stops at
  the first non-digit, which means trailing `\r`/`\n` in the buffer
  do not need to be stripped.
- The engine does **not** use else-if between key checks; every key
  is tested against every non-comment line independently. Distinct
  keys make this benign in practice, but the parser mirrors the
  behavior faithfully.

### Quirky in-memory layout

The `rmsg` array sits at the **lower** address (`0x073b1a18`) and
`msg` at the higher one (`0x073b1a68`), each 20 ints, contiguous —
an inversion from the source-code naming. We keep the same field
order in `struct buysell_config` (`rmsg` before `msg`) so anyone
reading runtime memory side-by-side with the port sees identical
offsets.

The zero-init loop in the engine seeds both arrays to `0` before
parsing; the port does the same via `memset(out, 0, sizeof *out)`
at the top of `tables_parse_buysell`.

### Vendor file sample

The shipping `data/buysell.txt` (504 bytes, CRLF) sets only the two
SJIS scalars to non-zero values:

```text
/ok:    //  (commented — debug mode off)
…
/客番号:kyaku.txt …
/種類:扱いの種類を固定できる、0..販売、1..買取、2..アバウト
/msg00:…
/
/
/
客番号:14
種類:2
msg00:0   …   msg12:0       (msg13..msg19 absent → stay zero)

/ リセット時の指定
rmsg00:0  …   rmsg12:0      (rmsg13..rmsg19 absent → stay zero)
```

So the vendor file is effectively inert — it expresses a debug
target (customer 14, kind 2) but with `ok:` commented and all
overrides set to 0. Removing the `/` on the `ok:` line is what
activates the override path.

---

## `data/config.idx`

**Engine block:** `FUN_00475270` block #2
(`docs/decompiled/by-address/475270.c:330..425`).

**Identity:** referenced via `s_config_idx_005cac78` (size path) and
`s_config_idx_005cac84` (read path). The two strings have **different
spellings** — `"config.idx"` (size side, no folder prefix) vs
`"data/config.idx"` (read side) — which causes a 940-byte heap overrun
in the original engine on every boot. Documented in
`docs/findings/tables-loader.md`; our port uses the read-side spelling
for both calls and sidesteps the bug.

**Port:** `src/tables_config.{c,h}`, parser entry point
`tables_parse_config(data, size, out)`. Engine-global instance
`g_config`. Tests in `tests/test_tables_config.c` (7 cases — empty,
all five live keys at once, makefont no-op, SJIS font name, font
over-length truncation, comment-only file, vendor-shape end-to-end).

**Purpose:** font + text-rendering configuration. Most keys are flags
that toggle behavior; only `edgewi` / `edgedel` carry numeric values.
The shipping vendor file enables only `edgewi=2` and `edgedel=6` —
the rest are commented out with `/` so the engine uses its built-in
defaults.

### Keys

| key (bytes)    | length | type  | global              | C-field                  |
|----------------|--------|-------|---------------------|--------------------------|
| `kanjioff:`    | 9      | flag  | `DAT_005cbc70`      | `kanjioff = 1`           |
| `edgewi:`      | 7      | int   | `_DAT_005cbc74`     | `edgewi = atoi(…)`       |
| `effectmode:`  | 11     | flag  | `DAT_073dddb4`      | `effectmode = 1`         |
| `edgedel:`     | 8      | int   | `_DAT_005cbc78`     | `edgedel = atoi(…)`      |
| `makefont`     | 8      | —     | **dead**            | match-then-no-op         |
| `font:`        | 5      | str   | `DAT_073de168[256]` + `DAT_073dfd00` | font name + `font_set = 1` |

- `kanjioff` (when set) tells the font generator to skip kanji glyph
  generation — useful as a speed-up on slow machines circa 2007.
- `edgewi` ∈ [0..4] — width in pixels of the black edge / outline.
- `edgedel` ∈ [0..15] — how fast the outline's per-pixel alpha
  decays toward the edge.
- `effectmode` switches between two text-rendering pipelines (specific
  semantics not yet investigated).
- `font:` sets the GDI font face name. The value bytes start at line
  offset 5 (right after the colon) and are copied as raw bytes (no
  encoding conversion) into a 256-byte fixed buffer. The shipping
  comment block suggests `ＭＳ 明朝`, `ＭＳ Ｐゴシック`, `Terminal`,
  or `Lucida Sans Unicode` as alternatives.

### Engine quirk: dead `makefont` check

The `makefont` string in the binary is the bare 8-letter word — no
trailing colon, no NUL counted in the match. The engine matches 8
bytes against it but assigns to no global — the matched-true branch
falls through to the next prefix check (`font:`) without setting
anything. Most likely a stub for a feature that was never implemented
(a "regenerate the font texture cache" trigger). Our port mirrors
the check for documentation; a `makefont:…` line is parsed but does
nothing.

### Engine quirk: line-terminator handling differs from buysell.txt

The config.idx parser overwrites the `\r` / `\n` at the end of each
line with `\0` in place (so that the inline-strcpy of the font name
sees a clean C string). The buysell.txt parser only writes `\0` one
byte AFTER the terminator. Both behaviors are equivalent for atoi
(which stops at non-digits anyway) but matter for the `font:` strcpy.
Our port's line reader excludes the `\r`/`\n` from the buffer
entirely, achieving the same effect for both files with less
ceremony.

---

## `data/oder.txt`

**Engine block:** `FUN_00475270` block #8 — dispatch wrapper at
`docs/decompiled/by-address/475270.c:1378..1421`, inner CSV loop
reached via `goto LAB_00477ffe` at lines 1813..1931. The two blocks
are physically non-adjacent in the decompiled output because the
inner loop is shared structurally with other CSV parsers, but only
oder.txt enters it through this particular goto.

**Identity:** referenced via `s_data_oder_txt_005caf7c` (size path)
and `s_data_oder_txt_005caf8c` (read path) — same spelling both
sides, so unlike `config.idx` there is no path-mismatch quirk to
mirror.

**Port:** `src/tables_oder.{c,h}`, parser entry point
`tables_parse_oder(data, size, out)`. Engine-global instance
`g_oder`. Tests in `tests/test_tables_oder.c` (9 cases — empty,
single record, LV threading, all 16 SJIS attribute tags, English
fallback, tab handling, 100-char line cap, no-trailing-newline,
vendor-shape end-to-end).

**Purpose:** *customer order requests.* When a customer enters Recet's
shop with an order, the engine picks an `oder.txt` entry whose
`LV` matches the current game difficulty, then displays the
singular/plural phrasing ("she wants **a bracelet** / she wants
**bracelets**") and uses the attribute tag to filter the shop
inventory for matching items.

### Line shape

The file is **not** in the `/key:value` family — it's a hybrid of
two line types intermixed:

| line                                       | role                       |
|--------------------------------------------|----------------------------|
| `//…` or blank or `/…`                     | comment / skipped          |
| `LV:N`                                     | sets pending difficulty    |
| `<singular>,<plural>,<attribute>`          | data row → one record      |

Each data row uses the most-recently-seen `LV:` value. The engine
stores `level_minus_1 = LV - 1` in the record — the `-1` shift makes
the level a 0-based array index.

### Record layout (engine stride 0x4c)

| offset | size | C-field          | written by         |
|--------|------|------------------|--------------------|
| +0x00  | 32   | `name_singular`  | phase 0 (in-place at column position) |
| +0x20  | 32   | `name_plural`    | phase 1 (sequential after first `,`)  |
| +0x40  | 4    | `attr_mask`      | `oder_attr_hash()` over field 3       |
| +0x44  | 4    | `attr_index`     | item-name lookup (suppressed — see below) |
| +0x48  | 4    | `level_minus_1`  | pending `LV` minus 1                  |

Record base in the engine: `&DAT_06a5db98` + `count * 0x4c`. The
port's `struct oder_entry` matches this byte layout, though we
present it as named fields rather than a packed buffer.

### Inner-loop quirks

- **Per-line cap:** 100 chars (`local_14 == 0x64` break at L1842).
  Anything past 100 bytes on a single data row is silently
  truncated.
- **Tab skipping:** an embedded `\t` is the only byte type that does
  *not* advance any field position. In phase 0 this leaves a hole
  at the tab's column (the byte at that record offset stays at its
  pre-parse value — usually 0). In phase 1 / phase 2 the tab is
  simply omitted from the output.
- **Column-position writes for field 1:** unlike fields 2 and 3
  which write sequentially, field 1 writes each byte at *column
  position within the line*. A comma at column N overwrites with
  `\0` at offset N in the record. With memset 0 at parse-start
  this still produces a clean C string for the common case
  (well-formed input, field width ≤ 31).
- **Over-32-char field 1:** the engine would spill bytes past
  offset 0x1F into the +0x20 plural slot, corrupting it. The
  port truncates at offset 31 to avoid the spill. The vendor file
  has no field over 16 chars so this divergence is dormant.

### 16-tag attribute table (`FUN_0049e9a7`)

The third field is hashed via a 4-byte memcmp against 16 SJIS tags
at `&DAT_005fd7fc` (stride 8: 4 chars + 4 zero-padding). The
returned mask sets bit `(1 << N)` for index N's tag, or 0 if no
match. The 16 tags in declaration order:

| bit    | SJIS bytes               | kanji  | romaji      | meaning             |
|--------|--------------------------|--------|-------------|---------------------|
| 0x0001 | `95 90 8a ed`            | 武器   | bukI        | weapon              |
| 0x0002 | `96 68 8b ef`            | 防具   | bougu       | armor               |
| 0x0004 | `92 b2 93 78`            | 調度   | choudo      | decor               |
| 0x0008 | `95 9e 8f fc`            | 服飾   | fukushoku   | clothing            |
| 0x0010 | `83 41 83 4e`            | アク   | aku         | accessory           |
| 0x0020 | `8b 4d 8b e0`            | 貴金   | kikin       | precious metal      |
| 0x0040 | `8b e0 91 ae`            | 金属   | kinzoku     | metal               |
| 0x0080 | `97 5b 94 d1`            | 夕飯   | yuuhan      | dinner              |
| 0x0100 | `8a c3 82 a2`            | 甘い   | amai        | sweet               |
| 0x0200 | `94 68 8e e8`            | 派手   | hade        | fancy               |
| 0x0400 | `92 6e 96 a1`            | 地味   | jimi        | plain               |
| 0x0800 | `92 bf 95 69`            | 珍品   | chinpin     | rare                |
| 0x1000 | `96 68 8a a6`            | 防寒   | boukan      | cold-weather        |
| 0x2000 | `90 48 95 69`            | 食品   | shokuhin    | food                |
| 0x4000 | `90 b9 91 ae`            | 聖属   | seizoku     | holy                |
| 0x8000 | `96 82 91 ae`            | 魔属   | mazoku      | sinister            |

The engine's lookup runs all 16 comparisons unconditionally and
overwrites the result with the *last* match's bit — but the 16 tags
are byte-disjoint in their first 4 bytes, so at most one match ever
fires in practice. The port mirrors "last match wins" anyway.

### Engine fallback: item-name table lookup

When `attr_mask == 0` (the attribute field didn't match any of the
16 SJIS tags), the engine linearly searches the `&DAT_0963e5f8`
item-name table (256 × 32-byte SJIS strings, populated by `item.txt`
during block #3). If a match is found, the index is stored at
`record + 0x44`. If not, two `MessageBoxA` dialogs fire with the
error string `属性不明な登録` ("unknown attribute registration").

This fallback is **intentionally suppressed in the port** for now
because:

1. `item.txt` hasn't been ported yet — the name table is empty so
   every lookup would miss and MessageBoxA on boot.
2. The fallback's natural shape is a callback / late-bound hook
   that we'll wire up cleanly when `item.txt` lands.

For now the port stores `attr_index = -1` unconditionally when the
SJIS hash misses. Vendor file behavior is preserved: SJIS-attr
rows like `…,武器` resolve to `attr_mask=1<<0` immediately;
English-name rows like `…,Treasures` get `attr_mask=0, attr_index=-1`
without a popup.

### Vendor file shape

Shipping `data/oder.txt` (1686 bytes, CRLF, SJIS):

- 5 LV groups (LV:1 through LV:5)
- 24 data rows total, distributed across groups
- A mix of English fallback rows (`Treasures`, `Bracelets`,
  `Hats`, `Scarves`, `Food`, `Books`, `Rings`, `Swords`,
  `Ingredients`) and SJIS-tag rows (the 14 attribute names)
- An ASCII comment block at the top enumerating the 16 attribute
  meanings — the same set this parser hashes against

The boot trace confirms `(orders=24 max_lv=5)` matches.

### Engine quirk: shared count global

`DAT_06a5d448` is the record-count cursor; it's set to 0 at the
top of every block (oder.txt, model.txt, event.txt, …) so each
block uses it as scratch storage. The port encapsulates this per-
parser via `struct oder_table.count`.
