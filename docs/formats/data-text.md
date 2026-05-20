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

---

## `data/model.txt`

**Engine block:** `FUN_00475270` block #9
(`docs/decompiled/by-address/475270.c:1422..1520`).

**Identity:** referenced via `s_data_model_txt_005cafc0` (size path)
and `s_data_model_txt_005cafd0` (read path). Both interned copies
hold the same spelling `"data/model.txt"` — no path-mismatch quirk
(unlike `config.idx`).

**Port:** `src/tables_model.{c,h}`, parser entry point
`tables_parse_model(data, size, out[MODEL_DEF_COUNT])`.
Engine-global instance `g_models[20]`. Tests in
`tests/test_tables_model.c` (9 cases — empty, basic one record,
index threading, comments/blanks skipped, fname before any no:,
repeated-slot count increment, overlong fname truncation, out-of-range
no: skipped, vendor-shape end-to-end).

**Purpose:** *3D model asset registry.* Each record maps a numeric
index (0..19) to the `.x` DirectX mesh filename (`fname`) and up to
20 named bone / attachment-point identifiers. The rendering and
animation code uses these attachment-point names to position
sub-meshes, weapons, or effects relative to a parent model. Indices
9, 16, and 19 are unassigned in the vendor file and remain all-zero
after parse.

### Line shape

| line                        | role                                          |
|-----------------------------|-----------------------------------------------|
| `//…`, `/ …`, blank         | comment / skipped (first byte `/`, `\r`, `\n`) |
| `no:N`                      | sets current model index to `atoi(line+3)`    |
| `fname:…`                   | sets `.x` filename for current record         |
| `NN:…` (`00:`..`19:`)       | sets attachment-point name for slot NN        |

`no:N` only updates the current index; subsequent `fname:` and `NN:`
checks against the same line simply do not match the `no:` prefix and
fall through harmlessly. The current index defaults to 0 (engine:
`local_c` initialised to 0 at L1429), so a `fname:` or slot line
before any `no:` writes to record 0.

### Record layout (engine stride 0x2b8)

| offset | size    | C-field             | written by             |
|--------|---------|---------------------|------------------------|
| +0x000 | 0x20    | `fname[32]`         | `fname:` line          |
| +0x020 | 4       | `count` (u32)       | incremented per `NN:` match |
| +0x024 | 20×0x20 | `point[20][32]`     | `NN:` lines (slot stride 0x20) |
| +0x2a4 | 20      | `used[20]` (u8)     | set to 1 on each `NN:` match |

Total: 0x2b8 (696) bytes per record, 20 records = 0x3660 bytes. Base
address in the engine: `&DAT_073ae258`.

### Engine quirks reproduced

- **Init loop** zeros only `count` (offset 0x20) and `used[]`
  (offsets 0x2a4..0x2b7) in every record; `fname` and `point[]`
  buffers are NOT zeroed. The port uses `memset(out, 0, …)` which
  also zeroes the name fields — a harmless superset.
- **`no:` fall-through:** a `no:N` line updates `local_c` and then
  falls through to the `fname:` and `NN:` checks, which harmlessly
  fail to match.
- **Unconditional `used[slot] = 1` and `count++`** on every `NN:`
  match, regardless of whether the slot was already populated. If the
  same slot is written twice, `count` increments twice and the last
  write's value is stored. Engine does not gate on `!used[slot]`.
- **All 20 slot prefixes** (`"00:"` .. `"19:"`) are tested against
  every non-comment line; at most one matches in practice (disjoint
  prefixes). The port breaks early after a match for efficiency while
  producing identical observable results.
- **atoi for `no:` index** — standard CRT behaviour: skips leading
  whitespace, stops at first non-digit.

### Safety divergences (not in engine)

- **Overlong `fname`:** engine write cap is 0x100, but the fname field
  is only 0x20 bytes before `count` at offset 0x20. An `fname:` value
  of ≥ 0x20 bytes (incl. NUL) would corrupt `count` and the point
  slots in the engine. The port truncates fname at
  `MODEL_DEF_NAME_MAX - 1 = 31` data chars + NUL at `fname[31]`.
  Vendor data has fnames ≤ 12 chars; this divergence is dormant.
- **Overlong point names:** same issue — engine cap 0x100, slot stride
  0x20; an overlong point name would spill into the next slot. Port
  truncates at 31 data chars + NUL.
- **Out-of-range `no:N`:** `N < 0` or `N >= 20` would cause the
  engine to compute an out-of-bounds pointer and corrupt heap or stack.
  The port treats an out-of-range index as a sentinel (`current = -1`)
  and skips all subsequent `fname:` / `NN:` writes until the next
  valid `no:` line. Engine has no such guard.

### Vendor file shape

Shipping `data/model.txt` (1758 bytes, CRLF, mostly ASCII — the
`//` comment lines contain SJIS Japanese but those lines start with
`/` and are skipped by the parser):

- 17 defined models across indices 0–8, 10–15, 17–18
- Gaps at indices 9, 16, 19 (remain all-zero)
- kine models (0, 1, 2): `g_lat_0{6,7,8}.x`, 2 points each
  (`point_01`, `bone_kine`)
- golem models (3–8): `golem_g0{1,2,3}.x`, 7 points each
  (`point_01`..`point_07`)
- kani models (10, 11): `kani01.X`, 8 points each (`point_01`..`point_08`)
- kurage models (12, 13): `kurage_01.x`, 3 points each
  (`bone1_body`, `point_01`, `point_02`)
- maou (14): `maou_02.X`, 6 points (`bone03_arm_body_r`, `point_10`..`point_14`)
- cyg body (15): `cyg_body.X`, 3 points (`point_01`, `point_20`, `point_21`)
- cyg right arm (17): `cyg_r.X`, 6 points (`bone03_arm_body_r`, `point_10`..`point_14`)
- cyg left arm (18): `cyg_l.X`, 6 points (`bone03_arm_body_l`, `point_15`..`point_19`)

Note: indices 17 and 18 appear **in reverse order** in the file
(`no:15` → `no:18` → `no:17`). The parser handles this correctly
because `no:` sets the current index for subsequent writes; parse
order does not determine record order.

The boot trace confirms `(models=17 max_points=8)` matches.

---

## `data/chara.txt`

**Engine block:** `FUN_00475270` block #6 — outer block at
`docs/decompiled/by-address/475270.c:1030..1146`, with the
continuation block reached via `LAB_00477931` that the decompiler
emits non-adjacent at `docs/decompiled/all.c:76547..76593`.

**Identity:** referenced via `s_data_chara_txt_005cae6c` (size path)
and `s_data_chara_txt_005cae7c` (read path). Both interned copies
hold the same spelling `"data/chara.txt"` — no path-mismatch quirk.

**Port:** `src/tables_chara.{c,h}`, parser entry point
`tables_parse_chara(data, size, out[CHARA_COUNT])`. Engine-global
instance `g_chara[8]`. Tests in `tests/test_tables_chara.c` (9 cases
— empty, defaults bit-exact, basic record, lv100 alone, both blocks
combined, comments skipped, OOR-index guard, lv100 field
permutation, vendor-shape end-to-end).

**Purpose:** *adventurer base + endpoint stats.* Defines the eight
playable companions Recette can dispatch into dungeons (Louie,
Charme, Caillou, Tielle, Elan, Nagi, Griff, Arma). Each record holds
level-1 base stats plus level-100 endpoint stats; the engine
interpolates per-level growth between the two. The first CSV column
in the base block is the unlock level threshold — Griff (index 6)
unlocks at 30, the rest between 1 and 20.

### Line shape

The file contains **two interleaved parser sub-blocks** that target
the same 8 records:

| line                        | role                                          |
|-----------------------------|-----------------------------------------------|
| `//…`, `/…`, blank          | comment / skipped (first byte `/`, `\r`, `\n`) |
| `NNN:` where `NNN` ∈ 000–007 | base stats — 10 CSV fields (7 ints + 3 floats) |
| `NNN:` where `NNN` ∈ 100–107 | level-100 endpoint — 6 CSV ints                |

The engine fast-skips with a 1-byte pre-check that `line[0] == '0'`
(base) or `line[0] == '1'` (lv100), then exact-matches 4 bytes
against `sprintf("%03d:", idx)` to identify the record. Both
sub-blocks iterate idx 0..9 in the engine even though only 8 records
exist (see engine bug below).

### Record layout (engine stride 0x40)

| offset | size | C-field          | source                                  |
|--------|------|------------------|-----------------------------------------|
| +0x00  | 4    | `level_threshold` | `atoi(file_field_1) - 1` (base block)  |
| +0x04  | 4    | `hp_base`        | base block file field 6 (HP column)     |
| +0x08  | 4    | `sp_base`        | base block file field 7 (SP)            |
| +0x0c  | 4    | `at_base`        | base block file field 2 (AT)            |
| +0x10  | 4    | `df_base`        | base block file field 3 (DF)            |
| +0x14  | 4    | `mt_base`        | base block file field 4 (MT)            |
| +0x18  | 4    | `mf_base`        | base block file field 5 (MF)            |
| +0x1c  | 4    | `move_speed` (f) | base block file field 8                 |
| +0x20  | 4    | `dash_speed` (f) | base block file field 9                 |
| +0x24  | 4    | `crit_rate`  (f) | base block file field 10                |
| +0x28  | 4    | `hp_lv100`       | lv100 block file field 5                |
| +0x2c  | 4    | `sp_lv100`       | lv100 block file field 6                |
| +0x30  | 4    | `at_lv100`       | lv100 block file field 1                |
| +0x34  | 4    | `df_lv100`       | lv100 block file field 2                |
| +0x38  | 4    | `mt_lv100`       | lv100 block file field 3                |
| +0x3c  | 4    | `mf_lv100`       | lv100 block file field 4                |

Total: 0x40 (64) bytes × 8 records = 0x200 bytes. Base address in
the engine: `&DAT_073ae058`. The 20 `model.txt` records start
immediately after at `&DAT_073ae258` (= `0x73ae058 + 8 * 0x40`).

### Field-order permutation

**Neither** sub-block writes fields in the order they appear on
disk:

- Base block file order: `level, AT, DF, MT, MF, HP, SP, move,
  dash, crit`. In-memory: `level, HP, SP, AT, DF, MT, MF, move,
  dash, crit`. The engine reorders by writing each field to its
  named offset (e.g. `piVar13[3] = atoi(field2)` puts AT into
  `at_base` at +0x0c).
- Lv100 block file order: `AT, DF, MT, MF, HP, SP`. In-memory:
  `HP, SP, AT, DF, MT, MF`. The engine writes field5 to `[10]`
  (offset 0x28 = `hp_lv100`) and field6 to `[0xb]` (offset 0x2c =
  `sp_lv100`), with field1..4 going to the higher offsets.

The port re-implements both permutations explicitly. Consumers
always see the canonical in-memory order in `struct chara_def_t`.

### Defaults (engine init loop, L1035..L1047)

| field          | default |
|----------------|---------|
| level_threshold| 1       |
| hp_base        | 50      |
| sp_base        | 30      |
| at_base        | 10      |
| df_base        | 13      |
| mt_base        | 5       |
| mf_base        | 10      |
| move_speed     | 0.15f (0x3e19999a) |
| dash_speed     | 0.20f (0x3e4ccccd) |
| crit_rate      | 0  (not init'd by engine; port memsets to 0) |
| hp_lv100..mf_lv100 | 0  (not init'd by engine; port memsets to 0) |

The two float bit patterns in the engine's init exactly match
IEEE 754 single-precision `0.15f` and `0.20f`, so the port's `0.15f`
/ `0.20f` initializers are byte-identical to the engine's writes.

### Engine quirks reproduced

- **Two-block parse over same records:** each adventurer has both a
  base block entry (`NNN:` with NNN < 10) and an optional lv100
  block entry (`NNN:` with 100 ≤ NNN < 110); they accumulate.
- **`field1 - 1` for level_threshold:** the leading CSV column in
  the base block is the unlock level (1, 8, 10, 20, 15, 15, 30, 1
  for the vendor adventurers); the engine subtracts 1 to store a
  0-based threshold. The default of `1` is therefore *not*
  equivalent to file_field1 = 1 (which would store 0) — they
  diverge and we use this to detect parsed records.
- **All 10 keys tested per line** — engine doesn't break out of
  the inner per-record loop after a match. Port breaks early for
  efficiency (prefixes are disjoint).
- **`atoi` / `atof`** are the CRT versions: skip leading
  whitespace, stop at first non-numeric.

### Engine bug: parse loop iterates 10× but only 8 records exist

The init loop covers exactly 8 records (puVar12 from `&DAT_073ae060`
to `&DAT_073ae260`, stride 0x40). The parse loop iterates **10**
times (piVar13 from `&DAT_073ae058` to `&DAT_073ae2d8`, stride 0x40)
— so a `008:` / `009:` / `108:` / `109:` line in chara.txt would
write 64 bytes past the end of the chara array, **directly into the
adjacent `g_models[0..1]` globals** at `&DAT_073ae258`. The vendor
file ships only `000:`..`007:` and `100:`..`107:`, so the overrun is
dormant. The port caps the inner match loop at `CHARA_COUNT` (8)
and silently drops out-of-range indices.

### Vendor file shape

Shipping `data/chara.txt` (1868 bytes, CRLF, SJIS comments). The
boot trace confirms `(adventurers=8 lv100=8)` — all eight base and
all eight lv100 rows parsed.

| idx | name      | LV→stored | HP | SP | AT | DF | MT | MF | move | dash | crit | HP100 | SP100 | AT100 | DF100 | MT100 | MF100 |
|-----|-----------|-----------|----|----|----|----|----|----|------|------|------|-------|-------|-------|-------|-------|-------|
| 0   | Louie     | 1→0       | 20 | 10 | 10 | 10 | 4  | 4  | .175 | .2625| .05  | 460   | 100   | 93    | 95    | 50    | 68    |
| 1   | Charme    | 8→7       | 16 | 15 | 9  | 8  | 8  | 9  | .195 | .2925| .07  | 340   | 250   | 88    | 82    | 68    | 79    |
| 2   | Caillou   | 10→9      | 10 | 50 | 6  | 6  | 12 | 14 | .155 | .2025| .04  | 190   | 700   | 52    | 68    | 100   | 95    |
| 3   | Tielle    | 20→19     | 13 | 18 | 7  | 7  | 8  | 10 | .175 | .2625| .05  | 280   | 350   | 81    | 78    | 70    | 86    |
| 4   | Elan      | 15→14     | 22 | 16 | 13 | 11 | 8  | 9  | .185 | .2775| .06  | 600   | 150   | 99    | 104   | 52    | 65    |
| 5   | Nagi      | 15→14     | 16 | 12 | 8  | 9  | 5  | 7  | .155 | .2325| .05  | 380   | 300   | 87    | 86    | 65    | 77    |
| 6   | Griff     | 30→29     | 18 | 20 | 12 | 7  | 11 | 13 | .175 | .2625| .06  | 300   | 500   | 90    | 76    | 88    | 106   |
| 7   | Arma      | 1→0       | 25 | 30 | 11 | 13 | 9  | 11 | .175 | .2625| .00  | 500   | 990   | 94    | 96    | 80    | 90    |

The file also contains a commented-out `1.0` block at the bottom
preserving the pre-balance-patch values — those lines all start with
`/` so the parser skips them entirely.

## `data/snews.txt`

Short news / "戦闘ニュース" — the random in-dungeon status broadcasts
that appear at the start of certain floors ("SP consumption
halved!", "Adventurer movement speed increased!", and so on). The
file has two unrelated parts: a flat name table indexed by 3-digit
ID, and a per-dungeon list of weighted spawn tables keyed by SJIS
dungeon names. Parsed by `FUN_00475270` block #12
(`docs/decompiled/by-address/475270.c` L2238..L2401); port at
`src/tables_snews.{c,h}`.

### Line shape (engine: L2272..L2401)

| line pattern               | dispatch                                 |
|----------------------------|------------------------------------------|
| empty                      | skipped                                  |
| starts with `/`            | comment, skipped                         |
| `ダンジョン{1..6}` (SJIS, 12 bytes) | switch active dungeon to 0..5; reset section counter |
| `f:N-M`                    | open new floor-range section (see quirk below) |
| `NON,W`                    | append entry (id=-2, weight=W) to current section |
| `NNN,W`                    | append entry (id=atoi(NNN), weight=W)    |
| `NNN<sep><text>`           | populate names[atoi(NNN)] = text (skipping the 1-byte separator at `<sep>`, typically `:`) |
| anything else              | engine: MessageBoxA "不明なニュース"; port: skip |

The 6 dungeon-key SJIS bytes (each 12 bytes / 6 codepoints):

| dungeon | SJIS bytes                                                  | meaning |
|---------|-------------------------------------------------------------|---------|
| 1       | `83 5f 83 93 83 57 83 87 83 93 82 50`                       | ダンジョン１ |
| 2       | `83 5f 83 93 83 57 83 87 83 93 82 51`                       | ダンジョン２ |
| 3       | `83 5f 83 93 83 57 83 87 83 93 82 52`                       | ダンジョン３ |
| 4       | `83 5f 83 93 83 57 83 87 83 93 82 53`                       | ダンジョン４ |
| 5       | `83 5f 83 93 83 57 83 87 83 93 82 54`                       | ダンジョン５ |
| 6       | `83 5f 83 93 83 57 83 87 83 93 82 55`                       | ダンジョン６ |

### Record layout

The engine maintains two unrelated globals, far apart in `.bss`:

**Name table** at `&DAT_073d8ee0` (stride 0x44, 64 entries):

| offset  | bytes | field   | notes                                   |
|---------|-------|---------|-----------------------------------------|
| `+0x00` | 4     | `active`| 0 = empty, 1 = populated                |
| `+0x04` | 64    | `name`  | text (engine: may not be NUL-terminated on 64-char overflow — see below) |

Total: 0x44 (68) bytes per entry × 64 entries = 0x1100 bytes.

**Section grid** at `&DAT_073b2108` (stride 0xa8, 10 × 30 sections):

| offset  | bytes | field         | notes                                   |
|---------|-------|---------------|-----------------------------------------|
| `+0x00` | 4     | `floor_start` | inclusive lower bound; -1 if unset      |
| `+0x04` | 4     | `floor_end`   | inclusive upper bound; -1 if unset      |
| `+0x08` | 160   | `entries[20]` | id/weight pairs (8 bytes each, id at +0, weight at +4) |

Entry slots use `id == -1` to mark unwritten, `id == -2` for "NON"
sentinel, and `id == 0..63` for a name-table reference. The engine
init writes 0xffffffff to every id field but leaves weights
uninitialised; the consumer (`FUN_004364bc`) only reads `weight`
when `id != -1`, so the divergence is unobservable.

Total: 0xa8 (168) bytes per section × 30 sections × 10 outer slots =
0xc4e0 bytes (50400). Only the first six outer slots are reachable
via the SJIS dungeon keys; slots 6..9 stay all-empty.

### Engine quirks reproduced

- **Off-by-one in `f:` writes** (load-bearing for the section
  layout!): the engine's `f:` handler writes
  `(floor_start, floor_end)` to the OLD `local_c` pointer BEFORE
  advancing to the new section position. Within a single dungeon
  this is fine — the first `f:N-M` line happens to advance to the
  same position it just wrote to (because the dungeon-key handler
  resets `local_14` to 0 but does NOT touch `local_c`). But on a
  dungeon transition, the previous dungeon's last `local_c` value
  is reused, and the next dungeon's first `f:` line **OVERWRITES
  the floor info of the last section of the previous dungeon**
  with its own `(N, M)`. Entries are not touched (writes happen
  via separate sub-record offsets). Documented as
  [engine-quirks #20](../findings/engine-quirks.md).

- **`local_c` / `local_18` not reset on dungeon-key match**: the
  engine only resets `local_14` (sections-within-current-dungeon
  counter) and `local_20` (dungeon index). The off-by-one above
  is the visible consequence.

- **Name table starts at `pcVar16 + 1`**: the engine skips a 1-byte
  separator at `line[3]` (`pcVar16[0]`) and reads the name proper
  starting at `line[4]`. Vendor data uses `:` as the separator.
  Lines like `001 name` (space) or `001-name` (dash) would have
  the separator byte stripped and the rest written verbatim.

- **Name char-copy bug**: the char loop iterates exactly 0x40 = 64
  times. The post-write EOL check writes a NUL at `name[k+1]`,
  with `k` ∈ [0, 64). On the 64th iteration `name[64]` (= the next
  entry's `active` byte) gets stomped with NUL — a 1-byte overrun
  into the adjacent table entry. Vendor names are all well under
  64 chars; the port caps at 63 + always-NUL for safety.

- **`NON` without comma**: the engine takes the name-table path
  with `id = -2`, computing `&DAT_073d8ee0 + (-2)*0x44 =
  DAT_073d8ee0 - 0x88`, somewhere in the snews `.bss` region.
  Vendor data always has `NON,W` (with a comma), so this is
  dormant. Port guards with `id >= 0 && id < SNEWS_NAME_COUNT`.

- **Unknown lines**: the engine calls
  `MessageBoxA(... line, "不明なニュース", 0)` — a blocking dialog
  on each malformed line. Vendor data is well-formed. Port
  silently skips.

### Vendor file shape

`data/snews.txt` (2230 bytes, CRLF, SJIS). The boot trace confirms
`(names=25 sections=10)`.

- 25 name entries (IDs 1..25), all in English.
- 6 dungeons with the following per-dungeon section counts:

| dungeon | f: lines | floor ranges in file        | "NON" weights    |
|---------|----------|-----------------------------|------------------|
| 1       | 1        | 1-5                         | 300              |
| 2       | 3        | 1-4, 6-9, 11-14             | 300, 400, 300    |
| 3       | 3        | 1-10, 11-20, 21-30          | 200, 150, 100    |
| 4       | 2        | 1-30, 31-60                 | 50, 50           |
| 5       | 1        | 1-100                       | 20               |
| 6       | 1        | 1-30                        | 0                |

Total: 11 f: lines across 6 dungeons. Due to the dungeon-transition
off-by-one, 5 of these writes land in the previous dungeon's last
section (corrupting its `floor_end`), and the 6th (dungeon 6's last
f:) leaves dungeon 6's section [5][0] with floor_start = -1 in the
parsed state — because no subsequent dungeon's first `f:` exists to
"write" to it. Vendor data is structured such that this corruption
is benign (corrupted floor_end values still bracket the
in-game-range floors of the dungeon's main play loop).

---

## `data/enemy.txt`

**Engine block:** `FUN_00475270` block #5
(`docs/decompiled/by-address/475270.c:834..1026`).

**Identity:** referenced via `s_data_enemy_txt_005cae2c` (size path)
and `s_data_enemy_txt_005cae3c` (read path) — same spelling both
sides, so no path-mismatch quirk.

**Port:** `src/tables_enemy.{c,h}`, parser entry point
`tables_parse_enemy(data, size, records)`. Engine-global instance
`g_enemy[ENEMY_COUNT]`. Tests in `tests/test_tables_enemy.c` (10
cases — pre-baked init, basic record, longest-prefix wins, shorter
prefix when no longer match, comments/blanks skipped, per-line drop
reset, unknown-name silently skipped, placeholder records skip
match, no-trailing-newline, vendor-shape end-to-end).

**Purpose:** configures HP/EXP/AT/DF/MA/MD stats and common/rare
drop-item references for the 64 enemy templates the engine spawns
across dungeons. The 64 NAMES are pre-baked into the binary's
`.data` segment at `&DAT_005c23f0` (file offset `0x1c0bf0` in
`vendor/unpacked/recettear.unpacked.exe`); enemy.txt only updates
the stats.

### Line shape

```text
<name>:<HP># <EXP># <AT># <DF># <MA># <MD># <common_drop> # <rare_drop>
```

Field delimiters recognized: `:`, `,`, `;`, `#`. The first delimiter
terminates the name match; the next six fields are atoi'd into HP,
EXP, AT, DF, MA, MD; the last two are looked up by name in the
`item.txt` table (`&DAT_095d381a`, stride 0x2cc, count
`_DAT_005c80ac`) and stored as item ids. Blank lines and lines
whose first byte is `/` or ` ` (space) are skipped.

The rare-drop column is optional — both drop slots are reset to -1
at the start of every line, so a line that omits the rare drop ends
up with `drop_rare = -1` rather than the previous line's value.

### Record layout (stride 0x68 / 104 bytes)

| offset | C field           | type | written by | meaning                            |
|-------:|-------------------|------|------------|------------------------------------|
| +0x00  | `name[0x20]`      | SJIS | .data init | record name (pre-baked, immutable) |
| +0x20  | `flags`           | i32  | .data init | 0=normal, 1=boss; (2=sentinel)     |
| +0x24  | `unknown_24`      | i32  | runtime    | sprite/animation index? not parsed |
| +0x28  | `unknown_28`      | f32  | runtime    | runtime scale (often 1.0); not parsed |
| +0x2c  | `hp`              | i32  | enemy.txt  | file field 1 (HP)                  |
| +0x30  | `exp_reward`      | i32  | enemy.txt  | file field 2 (EXP)                 |
| +0x34  | `at`              | i32  | enemy.txt  | file field 3 (AT)                  |
| +0x38  | `df`              | i32  | enemy.txt  | file field 4 (DF)                  |
| +0x3c  | `ma`              | i32  | enemy.txt  | file field 5 (MA)                  |
| +0x40  | `md`              | i32  | enemy.txt  | file field 6 (MD)                  |
| +0x44  | `runtime_floats`  | f32×7| runtime    | collision/render data; not parsed  |
| +0x60  | `drop_common`     | i32  | enemy.txt  | item id from "drop name" lookup    |
| +0x64  | `drop_rare`       | i32  | enemy.txt  | item id from "rare drop" lookup    |

### Record-name lookup: longest common prefix

The engine walks all 64 records per data line and keeps the record
whose stored name is the longest prefix of the line. Concretely,
for each record (skipped if `strlen(name) == 0`):

1. Compare `line[0..nlen]` byte-by-byte against `record->name`.
2. If all `nlen` bytes match AND `nlen > best_len`, this record
   becomes the new best.

So:

- A line beginning with `"アーリマン緑"` (12 bytes) matches both
  record 6 (`"アーリマン"`, 10 bytes) and record 7 (`"アーリマン緑"`,
  12 bytes); record 7 wins (longest prefix).
- A line beginning with `"アーリマン "` (with a trailing space)
  matches only record 6 — records 7..9 (`緑/青/赤`) all have a
  non-space byte at position 10 that differs from the line's space.
- A line beginning with `"ダークゴーレム"` matches no record
  (record 61's `"ゴーレム"` starts with `\x83\x53`, not `\x83\x5f`).

The seven "placeholder" records (slots 29/31/32/33/56/57/58) ship
with `name = " "` (single space, length 1). Real data lines never
begin with a space (the parser filters those out earlier), so the
placeholders never match.

### Pre-baked record table (extracted from .data)

64 records × 0x68 stride at file offset `0x1c0bf0` of
`vendor/unpacked/recettear.unpacked.exe`. Bosses (`flags == 1`) at
slots 24 (`ねずみバール`), 59-60 (`ねずみハリセン/マグロ`), 61-63
(`ゴーレム/右/左`). Placeholder slots at 29, 30 (`アルエット` — name
present but no enemy.txt line), 31-33, 42 (`ヒドラ` — no line), 43
(`親父` — no line), 56-58. All bytes beyond the name+flags region
are zero-initialized in our port; the engine ships them with a
parsed snapshot (so a fresh boot has working stats even before the
parse) plus runtime floats at +0x44..+0x5f that get used elsewhere.

The port populates only `name` and `flags` from .data (via
`tables_enemy_init`); enemy.txt overwrites stats and drop ids. The
runtime floats stay zero until the relevant engine-spawn code is
ported.

### Engine quirks

- **Sentinel `flags == 2` never present.** L821 of the parser
  breaks on a record with `flags == 2`, but no shipping record uses
  that value. The port honours the sentinel for fidelity even
  though it's dead code.
- **Aliased writes via prefix matching.** Vendor overlay file
  (`bmpdata.bin` enemy.txt, 3589 bytes) has 5 lines starting with
  `"アルマ"` (`アルマ`, `アルマゴーレム`, `アルマゴーレムコア`,
  `アルマゴーレム右手`, `アルマゴーレム左手`). All match record 41
  via the prefix rule; **last line wins**, so record 41 ends up
  with the stats of `アルマゴーレム左手` (HP=100, AT=20), not the
  `アルマ` line.
- **Lines with no matching record fire MessageBoxA.** Lines like
  `ダークゴーレム…`, `オーム…`, `カニ…`, `黒カニ…`, `クラゲ…`,
  `赤クラゲ…`, `魔王の手…` have no prefix match → engine pops a
  blocking dialog. Vendor ships these lines anyway, so any factory
  boot of recettear.exe shows the dialog chain unless the overlay
  is bypassed. Port silently skips.
- **Item lookup uses prefix match too** (L975..L1008): the drop
  name is compared as a prefix of the item-table name, not a full
  equality. So a drop name of `"Slime"` would match `Slime Fluid`
  (or `Slime Stone`, whichever appears first). Vendor drop names
  are full item names, so this is unobservable.
- **Per-line drop reset (L925..L926).** Both drop slots reset to -1
  at the start of every line, before field walking. So a line that
  only specifies the common drop ends up with `drop_rare = -1`.

### Vendor file shape

| version              | bytes | data lines | matched records | unmatched lines |
|----------------------|-------|------------|-----------------|-----------------|
| lnkdatas (raw)       | 2801  | 53         | 53              | 0               |
| bmpdata overlay      | 3589  | 67         | 54 (some aliased)| 9               |

The overlay version adds late-game / unused-content entries
(`ダークゴーレム`, `カニ`, `クラゲ`, `魔王の手`,
`アルマゴーレム*`) — most of which have no matching pre-baked
record and trigger the engine's MessageBoxA chain. The post-parse
state of `g_enemy` is identical between the two for the records
both files cover.

Boot trace logs `(enemies=N bosses=M)` where `enemies` counts
records with any of `{hp, at, md}` non-zero (covering outliers like
`岩とマグロ:0#0#20#0#0#0` and `ゴーストＯ:20#25#0#16#20#10`), and
`bosses` is the count of pre-baked `flags == 1` records (always 6).


## `data/tuto1.txt` / `data/tuto2.txt` / `data/tuto3.txt`

**Parser:** `src/tables_tuto.c` (block #15 of FUN_00475270 /
`tables_load_all`). Three files, loaded in sequence via a 3-iter
hard-coded loop. All three populate the **same** record array
(`&DAT_005d1fc8`), a single 200-slot × 296-byte buffer per file region
in the consumer's view.

The three files are a small **script** for the in-shop selling /
buying / recommendation tutorials. Each non-blank, non-`/`-prefixed
line is one record: an `id` (used as a jump target by `GOTO`) and an
opcode that selects what kind of dialogue or UI cue it is.

### Line shape

Lines are Shift-JIS, CR/LF/CRLF terminated. The engine's outer
line scanner skips lines whose first byte is `\r`, `\n`, or `/`. The
parser then sees:

```text
<id-int>,<opcode-token>[,<opcode-payload...>]
```

`<id-int>` is parsed by atoi from the start of the line. Three id
ranges have special meaning:

| `id`            | meaning                                                |
|-----------------|--------------------------------------------------------|
| `0` or positive | regular record; opcode dispatch follows                |
| `-1`            | sentinel record: opcode set to `-1`, no further reads  |
| `<= -2`         | text-only record: text copied from `line+3`, **opcode untouched** (BSS-zero = `CHR0`) — used for "retry" messages picked up by negative-id dispatch on the gameplay side |

### Opcode dispatch

Opcode tokens are matched **in this fixed order** against bytes
immediately after the first comma (the engine's nested `if/else`
chain at L2977..L3067 of `docs/decompiled/by-address/475270.c`):

| Opcode | Token (bytes) | Length | Payload |
|--------|---------------|--------|---------|
| 0      | `CHR0`        | 4      | 1 int (`chr_arg`) + `text` after the next comma |
| 1      | `CHR1`        | 4      | same as `CHR0`                                  |
| 2      | `TAGD`        | 4      | none (target window: show)                      |
| 3      | `PRID`        | 4      | none (price window: show)                       |
| 4      | `PRIA`        | 4      | none (price input wait)                         |
| 5      | `BUN0`        | 4      | 7 ints                                          |
| 6      | `GOTO`        | 4      | 7 ints (`args[0]` = target id)                  |
| 8      | `TAGN`        | 4      | none (target window: hide)                      |
| 9      | `TOUT`        | 4      | none (NPC exits scene)                          |
| 10     | `アイテム`     | 8      | none (item window)                              |
| 11     | `剣選択`       | 6      | 7 ints                                          |
| 12     | `値段` or `高く` | 4    | 7 ints — **shared opcode** for the two SJIS tokens |
| 13     | `値引`        | 4      | 7 ints                                          |
| 14     | `値上`        | 4      | 7 ints                                          |
| 20     | `初期金額決定` | 12     | none (set initial offer amount)                 |

Opcode value `7` is unused — the dispatch chain has no token that
maps there. Unknown tokens trigger `MessageBoxA(... "syntax error", ...)`
in the engine; our port logs to stderr and continues.

### Record layout (stride `0x128`)

```
offset  type          field          notes
0x000   int           id             first int on the line
0x004   int           opcode         see table above; -1 = sentinel
0x008   char[256]     text           CHR0/CHR1 dialogue or -N retry text
0x108   int[7]        args           BUN0/GOTO/剣選択/値段/高く/値引/値上
0x124   int           chr_arg        single int for CHR0/CHR1 only
```

The four field offsets are pinned by `_Static_assert` in
`src/tables_tuto.h`.

### Engine quirks reproduced

- **Parser stride 50 vs consumer stride 200** — major. The parser
  computes the destination slot as `local_8 + local_c * 50`, but the
  gameplay-side reader at `FUN_00461c00` indexes with stride 200
  (`DAT_005c6bb0 * 0xe740`, `0xe740 = 200 * 0x128`). The two
  disagree by a factor of 4, so the parser **only** ever fills the
  first 200 slots of the array. Three of four call sites for the
  file-index setter `FUN_00461bf6` push the immediate `2`, so the
  consumer routinely reads from file 2's region — which the parser
  never writes — returning BSS-zero records (all `CHR0`, empty text).
  How the game tolerates this in practice is unanswered for now;
  the port preserves the behaviour and we'll revisit when the
  gameplay-side dispatcher gets ported.
- **Vendor data overflows the parser cap on every file.** tuto1.txt
  has 135 records, tuto2.txt 90, tuto3.txt 60 — all far past the
  50-slot stride. The records sequentially overwrite each other's
  ranges; tuto3.txt walks 10 slots past the 150-slot array. Our
  port sizes `g_tuto` at 600 to absorb the overflow safely.
- **`id < -1` text-only branch does NOT set `opcode`.** Lines like
  `-2,Let us try again.` write only `id` and `text`; opcode is left
  at whatever the BSS-zero default was (= `CHR0`). The gameplay
  dispatcher addresses these records by negative id, not by opcode,
  so the wrong-looking opcode is benign.
- **7-int reader walks past line-end NUL.** For short lines like
  `0,GOTO,9,\t//comment` the engine's per-arg `for`-loop scans for
  `,` `\r` `\n` — none of which `\0` matches — so it walks into
  stack garbage and atoi's whatever bytes happen to be there. Our
  port uses a zeroed-between-lines buffer, so missing args read as
  0. Benign because gameplay code uses `args[0]` only for `GOTO`.
- **No comma → "loop err 17".** A line with no comma (e.g.
  `\t//comment` with leading whitespace that parses to id 0) makes
  the engine's comma-find loop run unbounded. The engine logs
  `loop err 17` via its debug pipe and moves on; we log to stderr
  with the offending line.
- **Sentinel write at end-of-file** — after consuming the file, the
  engine writes opcode = -1 at the **next** (unwritten) slot. Our
  port mirrors this so the gameplay-side `opcode == -1` early-exit
  trigger works.

### Vendor file shape

| file       | bytes | records | slot range (parser) | overflow? |
|------------|-------|---------|---------------------|-----------|
| tuto1.txt  | 8978  | 135     | 0..134              | yes (85)  |
| tuto2.txt  | 5828  | 90      | 50..139             | yes (40)  |
| tuto3.txt  | 4064  | 60      | 100..159            | yes (10)  |

After all three loads, the **final array state** is:

| slots      | content                                                   |
|------------|-----------------------------------------------------------|
| 0..49      | tuto1[0..49] (intact)                                     |
| 50..99     | tuto2[0..49] (overwrites tuto1's overflow 50..99)         |
| 100..149   | tuto3[0..49] (overwrites tuto1's overflow 100..134 + tuto2's overflow 100..139) |
| 150..159   | tuto3[50..59] (overflow — only this region escapes overwrites) |
| 160..      | sentinel + BSS-zero                                       |

Boot trace logs `(records=N)` per file plus a one-line warning when
the count exceeds the 50-slot cap, so the engine's behaviour is
observable at startup.


## `data/gousei.txt`

**Parser:** `src/tables_gousei.c` (block #13 of FUN_00475270 /
`tables_load_all`, starting at LAB_004790cd /
`docs/decompiled/by-address/475270.c:2402..2579`). Tests in
`tests/test_tables_gousei.c` (15 cases — empty, comments-only, basic
recipe, rank header, recipe-before-any-rank, prefix-discarded,
three- and five-ingredient widths, null-resolver, unknown name,
EOL-without-trailing-':', no-trailing-newline, max-records cap,
embedded-NUL early-exit, vendor-shape end-to-end).

**Purpose:** *item synthesis (合成) recipes* — defines how the shop's
workshop UI crafts higher-tier items from lower-tier inputs. Each
recipe pairs one output item with up to 5 named ingredients, each
with a quantity. Recipes are grouped under `ランク:N` rank headers
(crafting rank gates which recipes are unlocked).

### Per-line shape

```
0004:Gilded Sword:Longsword#1:Water Crystal#1:
^^^^                                          ^
4-digit prefix — discarded                    trailing ':'
```

Field separator: `:`. Ingredient quantity: `#count` appended to the
ingredient name. The output item (column 0) has no `#count`.

### Per-record layout (12 dwords, stride 0x30)

The engine writes records into a contiguous array starting at
`&DAT_09640650`, with the populated-count counter at `&DAT_09642bf0`.
Each record:

| offset | engine symbol     | C field             | meaning             |
|--------|-------------------|---------------------|---------------------|
| +0x00  | DAT_09640650      | output_id           | item id (col 0)     |
| +0x04  | DAT_09640654      | rank                | current `ランク:N`  |
| +0x08  | DAT_09640658      | ingredient_id[0]    | ing1 item id        |
| +0x0c  | DAT_0964065c      | ingredient_id[1]    | ing2 item id        |
| +0x10  | DAT_09640660      | ingredient_id[2]    | ing3 item id        |
| +0x14  | DAT_09640664      | ingredient_id[3]    | ing4 item id        |
| +0x18  | DAT_09640668      | ingredient_id[4]    | ing5 item id        |
| +0x1c  | DAT_0964066c      | ingredient_count[0] | ing1 count          |
| +0x20  | DAT_09640670      | ingredient_count[1] | ing2 count          |
| +0x24  | DAT_09640674      | ingredient_count[2] | ing3 count          |
| +0x28  | DAT_09640678      | ingredient_count[3] | ing4 count          |
| +0x2c  | DAT_0964067c      | ingredient_count[4] | ing5 count          |

### Header dispatch

| line pattern   | action                                              |
|----------------|-----------------------------------------------------|
| `/...`         | comment — skipped                                   |
| empty          | skipped                                             |
| `ランク:N`     | `current_rank = atoi(N)` (7-byte SJIS prefix)       |
| `NNNN:...`     | recipe row (NNNN parsed but discarded)              |

Recipes encountered before any `ランク:` header get rank=0.

### Engine quirks

- **The 4-digit `NNNN:` prefix is discarded.** Engine: `pcVar16 =
  local_27c + 0x25` skips 5 bytes. The output item is keyed by NAME,
  not by the numeric prefix. The digits appear to be a sprite-slot
  hint for the data designers (first 2 digits = category, last 2 =
  sub-index) but the engine never reads them.

- **`ing1` write stamps `ing2..ing5` to -1.** When the parser writes
  the column-1 ingredient ID (engine: L2533..L2538), it also writes
  0xffffffff to the ing2..ing5 ID slots. This is the ONLY place those
  slots get pre-initialised — counts stay at BSS-zero unless written
  by `#N`. Unused ingredient IDs therefore read as -1 (assuming ing1
  was set), unused ingredient counts as 0.

- **Item lookup is exact-name, not longest-prefix.** Engine
  L2491..L2519 does length-equality + strncmp(strlen) — names must
  match exactly. (Contrast `enemy.txt`, which uses longest-prefix.)

- **Index-0 match still pops MessageBox.** Engine bug: a name that
  matches the FIRST entry in the item table (index 0) takes the
  `break` path instead of `goto skip_messagebox`, so MessageBoxA
  "{name} に不明なアイテム" fires anyway. The resolved ID *does* land
  in the record correctly. Port doesn't pop MessageBox at all, so
  this quirk is moot in the port.

- **Resolver miss = 0 in engine, -1 in port.** Engine: name-not-found
  → `piVar4 = 0` (the loop-init value) is what gets stored. Port:
  unresolved → -1 (matches the convention used in oder.txt and
  enemy.txt drops). Both are dormant until `item.txt` has been
  parsed.

- **`#0` count and "stale count" both warn (no-op).** Engine pops
  MessageBox for zero counts (`#0`) and for ingredient slots with no
  `#count` modifier. Vendor data never trips either; the port logs
  to stderr instead of pop-up.

- **Record cap at 200 with overrun.** Engine `MessageBoxA "合成
  アイテム登録オーバー"` fires when count crosses 200, but the record
  has already been written — the engine writes-then-warns. By address
  math, the array runs out of room around slot 200 (next adjacent
  symbol `&DAT_09642bf0` sits at `base + 0x25a0`), so the overrun
  bleeds into neighbouring globals. The port refuses to write past
  slot 199 (count reaches 200) and drops the remaining recipes.
  Vendor data ships 101 recipes.

### `#count` at EOL with no trailing ':'

One vendor recipe is malformed:

```
1215:Master's Plate:Barrier Plate#1:Plate of Grief#1:Wind Emblem#1
                                                                  ^ no ':'
```

Engine's `#` handler does an UNBOUNDED scan for ':' after the count
digits — on this line it walks past `\r\n` and into the rest of the
parse buffer, eventually hitting some unrelated ':' far away. The
record IS still committed (count++); all four ingredients get the
correct IDs and counts; only the parser's cursor ends up in an
undefined location, briefly. The next line's processing eventually
re-aligns (the outer loop reads `pos` from the line-collect cursor,
not the inner `pcVar16`).

Port behaviour: detects EOL/EOF inside the `:` hunt and finalises the
last ingredient cleanly (resolves the name, writes the ID, breaks out
of the inner loop). End-state of the record is identical to the
engine's; we just skip the undefined-behaviour scan.

### Vendor file shape

| stat                              | value                            |
|-----------------------------------|----------------------------------|
| Bytes via `storage_read`          | 6252                             |
| Total non-comment/blank lines     | 106 (5 rank headers + 101 recipes) |
| Recipes per rank                  | 22 / 22 / 17 / 19 / 21 (ranks 1..5) |
| Recipes with 4 ingredients (col=3 lookups) | 65                      |
| Recipes with 3 ingredients (col=2 final)   | 36                      |
| Recipes with 5 ingredients                 | 0 (max width unused)    |
| Malformed-trailing-':' recipes             | 1 (rank-4 Master's Plate) |

Boot trace logs `(recipes=N max_rank=M)`. The cross-table dependency
on `item.txt`'s name table means every ingredient and output ID
currently resolves to -1; once item.txt's parser lands, tables.c can
plumb a real resolver into `tables_parse_gousei` and the IDs will
resolve correctly without touching the parser itself.

---

## `data/item.txt`

The master item catalog: every weapon, armour, accessory, consumable,
food, book, furniture, etc. that the shop deals in. 571 records
across 33 categories. Source-of-truth for the `singular[]` /
`plural[]` / `attr_mask` / `price` / etc. fields that every other
gameplay table (oder.txt, enemy.txt, gousei.txt) ultimately resolves
through.

Parser source: `src/tables_item.c`. Engine source: FUN_00475270
block #3 (475270.c L428..L468) + cross-block record dispatch at
L815..L829, FUN_00491044 (category header), FUN_004912de (item
record). Full discovery: `docs/findings/item-table.md`.

### Per-line dispatcher

Each line is classified by its first byte:

| First byte | Meaning                          | Engine routing                       |
|------------|----------------------------------|---------------------------------------|
| `\r` `\n` `/` | Blank line or comment           | Skipped                              |
| `:`        | Category header                  | `FUN_00491044(line+1, cat_idx)`       |
| ` `        | Defensive indent-skip            | Silently dropped                     |
| `0`–`9`    | Item record (4-digit ID prefix)  | `FUN_004912de(line+5, item_id, slot)` |
| anything else | Unknown line                  | Engine pops MessageBoxA "不明な行"; port logs to stderr |

### Category header (`:Name#Tag`)

```
:Swords#(Equippable)
```

Splits on the first `#`. Up to 32 chars total. Result lands in two
scratch buffers (engine globals `&DAT_09642bd0` for singular, `&DAT_09640604`
for tag) that the NEXT item-record parse picks up. The header itself
doesn't index into the category table — the record does that based
on `item_id / 100`.

### Item record (`NNNN:R#name+plural#price#atk#def#mt#mf#attr#stock#aud##desc1#desc2`)

```
0001:1#Worn Sword+Worn Swords#               200#  8#  0#  0#  0# 金属地味         #在庫(1)ギ(1)市(0)買(0)ダ(11)        ##A worn-out, dented, chipped sword. Still better than#going into the wild bare-handed, though.
```

Fields (all `#`-separated, 10 separators total):

| # | Field        | Type              | Notes                                        |
|---|--------------|-------------------|----------------------------------------------|
| 1 | RANK         | int (atoi)        | Rank/level digit; written before name parse  |
| 2 | NAME+PLURAL  | SJIS text         | Optional `+` splits singular and plural      |
| 3 | PRICE        | int               |                                              |
| 4 | ATK          | int               |                                              |
| 5 | DEF          | int               |                                              |
| 6 | MT           | int               | "Magic attack"                               |
| 7 | MF           | int               | "Magic defense"                              |
| 8 | ATTR         | space-sep SJIS    | Up to 10 × 4-byte tags (shared with oder.txt) |
| 9 | STOCK        | tag-sequence      | `在庫(N)ギ(N)市(N)買(N)ダ(N)(N)(N)卸(N)持(N)` |
| 10| AUD          | tag-sequence      | Up to 10 × 2-byte target-audience tags       |
| 11| DESC1        | SJIS text         | Description line 1 (often empty via `##`)    |
| 12| DESC2        | SJIS text         | Description line 2; `/` truncates           |

### Name + plural

Phase 0 of `FUN_004912de` writes each name character to BOTH
`singular[]` AND `plural[]`. On encountering `+`, the column counter
resets and only `plural[]` continues to accumulate. Net effect:

- `Sword` — no `+` → singular = plural = "Sword"
- `Worn Sword+Worn Swords` — singular = "Worn Sword", plural = "Worn Swords"

Both buffers are 64 bytes (NUL-terminated).

### Attribute tags

The ATTR field uses the same 16-tag table as `oder.txt`
(`oder_attr_hash` — see oder.txt section above for the SJIS byte
values and bit assignments). Up to 10 tags per field; space-separated.
Per-record `attr_mask` is the OR of all matched tag bits.

Additionally, the parser OR's in *category bits* (`FUN_0049eb2a`)
based on the singular category name:

| Category name(s)                                          | Bit       |
|-----------------------------------------------------------|-----------|
| Swords, Daggers, Staves, Bows, Spears, Gloves, Claws, Arm Parts | `0x00001` (weapon) |
| Clothes, Robes, Breastplates, Armor, Shields, Bracelets, Helms, Hats | `0x00002` (armour) |
| Flooring, Wallpapers, Counters, Carpets                   | `0x10000` (furniture) |

### Stock-info tags

`STOCK_TAG(N)` format — 7 distinct tags scanned in 5 rounds:

| SJIS tag | Stored at | Notes                                |
|----------|-----------|--------------------------------------|
| `在庫(N)` | `stock_info[0]` | Stock count                    |
| `ギ(N)`   | `stock_info[1]` | Guild submission               |
| `市(N)`   | `stock_info[2]` | Market visibility              |
| `買(N)`   | `stock_info[3]` | Buy-back preference            |
| `ダ(N)(M)(K)` | `stock_info[4..6]` | Dungeon spawn ×3; values < 10 are stored as `value*10` |
| `卸(N)`   | `stock_info[7]` | Wholesale (default 200)        |
| `持(N)`   | `stock_info[8]` | Hold slot                      |

Multi-digit `在庫`, `ギ`, etc. values are an engine quirk: the parser
advances by a hard-coded 7 or 5 bytes (single-digit assumption), so a
two-digit value silently desyncs subsequent tag scans within the same
round. Vendor data is all single-digit for these slots. `ダ` is the
only tag with variable advance.

### Audience tags

11 × 2-byte SJIS tags, each OR'ing a bit into `aud_mask`:

| Tag (SJIS) | Hex bytes   | Bit(s)             | Maps to NPC          |
|------------|-------------|---------------------|----------------------|
| 全         | `91 53`     | `0xff` (all)        | All customers        |
| リ         | `83 8a`     | `0x01`              | Recette              |
| シ         | `83 56`     | `0x02`              |                      |
| カ         | `83 4a`     | `0x04`              | Caillou              |
| テ         | `83 65`     | `0x08`              | Tielle               |
| エ         | `83 47`     | `0x10`              | Elan                 |
| ナ         | `83 69`     | `0x20`              | Nagi                 |
| グ         | `83 4f`     | `0x40`              | Guildmaster          |
| ア         | `83 41`     | `0x80`              | Arma                 |
| 男         | `92 6a`     | `0x55` (composite)  | All male NPCs        |
| 女         | `8f 97`     | `0xaa` (composite)  | All female NPCs      |

Special case: an audience field whose *first character* is `#`
(i.e. the empty field between `##` in vendor data) triggers an
unconditional `aud_mask |= 0xff` — items default to "visible to all"
when the AUD field is omitted.

### Description lines

The `##` after AUD in vendor data is the AUD→DESC1 separator with
an empty audience field. Phase 0 consumes 9 `#` advances; after the
9th, the parser is past the SECOND `#` of `##` and phase 1 begins
reading the first DESC1 byte. Phase 1 captures bytes until the next
`#` (DESC1→DESC2 boundary), then phase 2 takes over and captures
until `/`, `\r`, or `\n`.

Engine init at `FUN_004912de:28-31`: both `desc_line1` and
`desc_line2` are seeded with `" "` (single space + NUL) before
parsing. If phase 1 or phase 2 never writes a byte, the field
remains as that single-space sentinel.

### Vendor file shape

| stat                                  | value     |
|---------------------------------------|-----------|
| Bytes via `storage_read`              | 122010    |
| Total lines (CRLF-split)              | 781       |
| Comment lines (`//`)                  | 66        |
| Blank lines                           | 111       |
| Category headers (`:`)                | 33        |
| Item records (digit-prefix)           | 571       |
| Unique category indices               | 33        |
| Category index range                  | 0..54     |
| Item-id range                         | 0..5408   |

Boot trace logs `(items=N max_id=M equippable=K cats=C)`. The
table at `&DAT_095d37d0` (stride 0x2cc) holds all 571 records; the
category-name globals at `&DAT_0963e5f8` (singular) and
`&DAT_0963c5f8` (tag) carry the per-category text.

### Resolver hook

`tables_item_resolve(state, name)` performs exact-match lookup
against record `singular[]` fields and returns the matching record's
`item_id` (NOT slot index). Mismatch / NULL inputs → -1.

A follow-up commit will thread this through `tables.c` into the
deferred resolver hooks of `tables_parse_enemy` (drop refs) and
`tables_parse_gousei` (ingredient refs), retiring the `-1`
placeholders those parsers currently produce.

## `data/kyaku.txt`

The 50-slot customer roster (kyaku = 客 = "customer"). Each record
holds a singular/plural name, a name-table index, a 2-axis SJIS
attribute pair, a list of preferred item categories (English names,
resolved through item.txt's category table), a preferred-attribute
SJIS-bitmask, a budget range, an activity-time-of-day mask, a handful
of haggle-tuning ints, and a per-character dialog-file path. Vendor
file populates 18 active slots out of 50.

The engine struct is enormous (0x2c670 bytes / 181 KB per record)
because most of it is dialog-buffer scratch for the customer's `file:`
script content. The table loader only writes the ~18 meaningful
fields below; the dialog text gets wired in by an unrelated subsystem.

### Per-line shape

```text
013:Woman#Women                   ← header: NNN:Singular[#Plural]
名前番号:13                       ← name-table index
属性:0,4                          ← attr X,Y (two signed ints)
好き属性:食品派手貴金             ← preferred-attribute SJIS tokens, 4 bytes each
好き種類:Medicines                ← preferred item category (English; repeatable, ≤ 20)
好き種類:Rings
嫌い:                             ← matched but DISCARDED — see quirks
予算:3000-300000                  ← budget range "L-H"
活動時間:夕夜                     ← activity time SJIS tokens (朝/昼/夕/夜)
疑:                               ← suspicion (atoi, empty = 0)
ランダム:3
初回:120
騙:20
上昇１:10
上昇２:10
file:kyaku/f3.txt                 ← per-customer dialog file (relative path)
```

Lines starting with `/`, `\r`, or `\n` are comments / blanks. Other
characters before the first `NNN:` header silently drop (no current
record).

### Field-key table

All key bytes are exact prefix matches against the line buffer at
offset 0 (the engine's `local_27c + 0x20`); the body starts
immediately after the matched key (no whitespace skip).

| key        | bytes | body              | engine field write                  |
|------------|-------|-------------------|-------------------------------------|
| `NNN:`     | 4     | `S[#P]`           | active=1, singular, joint           |
| `名前番号:`| 9     | atoi              | name_index                          |
| `属性:`    | 5     | `X,Y` (atoi×2)    | attr_x, attr_y                      |
| `好き属性:`| 9     | up to 10 4-byte chunks | like_attr_mask (OR-of-tag-bits)|
| `好き種類:`| 9     | English category  | append like_kinds[], like_count++   |
| `嫌い:`    | 5     | *discarded*       | — (see quirk)                       |
| `予算:`    | 5     | `L-H` (atoi×2)    | budget_low, budget_high             |
| `活動時間:`| 9     | up to 4 2-byte SJIS tokens | activity_time_mask (bits 1/2/4/8) |
| `疑:`      | 3     | atoi              | suspicion                           |
| `騙:`      | 3     | atoi              | gullibility (`騙される度`)          |
| `上昇１:`  | 7     | atoi              | rise1                               |
| `上昇２:`  | 7     | atoi              | rise2                               |
| `初回:`    | 5     | atoi              | initial                             |
| `ランダム:`| 9     | atoi              | random                              |
| `file:`    | 5     | path (≤ 0x100 bytes) | file_path                        |

### Header line: `NNN:Singular[#Plural]`

Two write cursors run in parallel as bytes are consumed from the line
tail:

- `singular[iVar6]` and `joint[iVar6]` both get each pre-'#' byte
  (the `iVar6 + 4` and `iVar6 + 0x24` writes in the decompilation).
- At `#`, `iVar6` resets to 0 and a `post_hash` flag is set. From
  then on the singular write is gated off; the joint write resumes
  at `joint[0]`, overwriting the shared prefix.

Net result: singular holds the pre-'#' text, joint holds the
post-'#' text — but joint is only physically rewritten in the bytes
the plural actually covers. If the plural is shorter than the
singular, the tail of the singular leaks into joint. Vendor data
never triggers that case (all plurals are at least as long as their
singulars: `Woman#Women`, `Man#Men`, `Old Man#Old Men`, etc.).

### Activity-time tokens

Four 2-byte SJIS tokens map to bit positions; the parser advances
the cursor by 2 bytes per iteration and stops after 4 iterations.

| token | bytes        | bit   |
|-------|--------------|-------|
| 朝    | `92 a9`      | `0x1` |
| 昼    | `92 8b`      | `0x2` |
| 夕    | `97 5b`      | `0x4` |
| 夜    | `96 e9`      | `0x8` |

Tokens not in this set (e.g. `本人` for Recette, `試` for Tear) match
nothing and leave the mask at 0. The parser is happy to step 2 bytes
into the middle of an unknown 2-byte SJIS char — vendor data avoids
that ambiguity.

### Preferred-attribute tokens

Each 4-byte cursor chunk is fed through two helpers (engine
`FUN_0049e9a7` and `FUN_0049eb2a`) and OR'd into `like_attr_mask`. The
first helper is the 16-tag SJIS table shared with `oder.txt` and
`item.txt`. The second is the English category-name helper; it
requires `name[tag_len] == '\0'` which is never true for a 4-byte
SJIS slice in vendor data, so it always returns 0 in practice.

The loop is hard-capped at 10 iterations (40 bytes). Vendor lines
use 6..10 tokens.

### Engine quirks faithfully reproduced

- **`嫌い:` is an orphan match.** The 5-byte key match against
  `嫌い:` (DAT_005caddc) has an empty body — the do-while just
  `break`s on mismatch and falls through to the next key on a hit.
  No field write, no atoi, no string copy. Almost certainly a
  dialled-back feature; the data file still ships dozens of `嫌い:`
  lines but nothing consumes them. Cost is 5 char-compares per
  non-comment line.

- **Header singular/joint write-position reset.** On `NNN:S#P`, the
  joint cursor resets to offset 0 at the `#`, so the plural
  *overwrites* joint[0..] starting from the beginning instead of
  appending. Documented in detail above.

- **Singular NUL at off-by-five.** The EOL-detect inside the header
  loop writes `puVar14[iVar17 + 5] = 0` — that's `singular[iVar17 +
  1]`, NOT `singular[iVar6 + 1]`. For lines without `#` it lands one
  past the singular's last content byte (correct). For lines with
  `#` it lands several bytes past singular's end. Harmless because
  the record was memset'd to zero at boot.

- **Header gated by leading `0`.** The line dispatcher only tries
  the 50-iteration `%03d:` match if the line's first byte is `0`.
  Records 0..49 all have IDs ≤ 49 so the leading digit is always
  `0`, but a hypothetical record `100:` would be silently ignored.

- **`属性:` and `予算:` unbounded delimiter scans.** Once the first
  numeric is parsed, the engine walks forward looking for `,` (attr)
  or `-` (budget) with NO upper bound — past the line buffer if the
  delimiter is missing. Vendor data always has both. The port stops
  at NUL too for safety.

- **`好き種類:` cap of 20.** The engine guards with `if
  (DAT_06a63c38[...] < 0x14)`; the 21st like-kind triggers
  `MessageBoxA "好き種類登録数オーバー"` and is dropped. Vendor
  customer Alouette tops out at 11.

- **`好き種類:` MessageBoxA on unknown category.** Engine pops
  `MessageBoxA "不明なアイテム1" + name`. Port logs to stderr.

- **Lines before any header are silently dropped.** The engine's
  `local_14 < 0` branch sprintf's an error to a local that's
  immediately discarded — equivalent to a no-op. Port does the same.

### Vendor file shape

| stat                                  | value     |
|---------------------------------------|-----------|
| Bytes via `storage_read`              | 7603      |
| Active customer slots                 | 18        |
| Total `好き種類:` entries (sum)       | 111       |
| Customers with non-empty `予算:`      | 15        |
| Max like-kinds in one customer        | 11 (Alouette) |
| Customers with plural form (`#`)      | 4 (`Woman`,`Man`,`Old Man`,`Girl`) |

Boot trace logs `(customers=N like_kinds=K with_budget=B)`. The
record array at `&DAT_06a5ea90` (stride 0x2c670) holds all 50 slots;
the port's flat struct lifts the ~18 meaningful fields out of that
181-KB engine record and discards the dialog-buffer tail.

### Resolver hook

`好き種類:` is the only field that needs cross-table resolution.
`tables.c` wires it through a small adapter `resolve_via_item_category`
that linearly probes `g_item.categories[].singular` against the line
tail (exact match with EOL/NUL guard). The first matching slot's
index is stored; a miss returns -1 and the line is dropped.

This is a different lookup path from `tables_parse_enemy` /
`tables_parse_gousei` (which resolve against `g_item.records[].
singular` for individual items). Kyaku resolves against the
**category-name** table — populated by `:Category#(tag)` headers in
`item.txt` — not the item table itself.

---

## `data/event.txt`

**Engine block:** `FUN_00475270` block #10
(`docs/decompiled/by-address/475270.c:1521..2235`).

**Identity:** referenced via `s_data_event_txt_005caff4` (size path)
and `s_data_event_txt_005cb004` (read path) — same spelling both
sides; no path-mismatch quirk.

**Port:** `src/tables_event.{c,h}`, parser entry point
`tables_parse_event(data, size, out)`. Engine-global instance
`g_event`. Tests in `tests/test_tables_event.c` (15 cases — empty
seed, layout, comments, basic record, hex/-1 prereqs, time-first/max
tracking, unknown time tokens, loop_min, 20-pair cap, category
dispatch, pre-header fall-through to 広場, decay_or_max sentinel,
no-trailing-newline, vendor shape).

**Purpose:** *in-town vignette trigger table* — drives the
spawn-conditions for short bystander/character cutscenes the player
sees when entering one of the four town locations 広場 (plaza),
市場 (market), 教会 (church), or 酒場 (saloon). Each record carries
a calendar window (day-range pairs + loop_min), a time-of-day band,
a "set this flag when triggered" tag, and up to four flag-state
prerequisites. The consumer `FUN_0045de68` walks the per-location
record list, picks the first that satisfies all gates, marks the
"already seen" flag, and returns the event id to the in-town
scene-runner.

### Categories

The file's only structural keys are the four 4-byte SJIS location
headers (bytes at `DAT_005cb014`..`DAT_005cb02c`):

| header (SJIS) | bytes        | parser cat | constant            |
|---------------|--------------|------------|---------------------|
| `広場`        | `8d 4c 8f ea` | 0          | `EVENT_CAT_HIROBA`  |
| `市場`        | `8e 73 8f ea` | 1          | `EVENT_CAT_ICHIBA`  |
| `教会`        | `8b b3 89 ef` | 2          | `EVENT_CAT_KYOKAI`  |
| `酒場`        | `8e f0 8f ea` | 3          | `EVENT_CAT_SAKABA`  |

Header lines must match exactly the 4 bytes — no surrounding
whitespace, no extra suffix. The engine doesn't reset `local_18`
on a non-header non-data line; data lines after a comment block
still go to the most recent matched category.

### Record layout (50 dwords = 200 bytes)

The four categories occupy contiguous 100-record regions in BSS at
`&DAT_06a49b80` (広場), `&DAT_06a4e9a0` (市場), `&DAT_06a537c0`
(教会), `&DAT_06a585e0` (酒場); record stride is `0x32 * 4 = 200`
bytes and category stride is `100 * 200 = 0x4e20` bytes. The port
mirrors the byte layout exactly via `event_record_t` (`_Static_assert`
guards on every offset):

| offset | size | field                | source              |
|--------|------|----------------------|---------------------|
| +0     | 4    | `id`                 | atoi of first int   |
| +4     | 4    | `flag_on_trigger`    | atoi after `-`      |
| +8     | 16   | `prereq[0..3]`       | 4 hex-or-FF prereqs |
| +24    | 4    | `time_first`         | first matched tag idx |
| +28    | 4    | `time_max`           | max matched tag idx |
| +32    | 160  | `day_pairs[20][2]`   | (start,end) ranges  |
| +192   | 4    | `loop_min`           | atoi (post-tags)    |
| +196   | 4    | `decay_or_max`       | 0 for parsed; 100000 for pre-baked seed |

### Data line shape

```
14- 4:  100:-1:-1:-1  :朝昼　　:  0:  3-9,36-999:   //comment tail
 ││  │   │  │  │  │   │         │  │
 ││  │   │  │  │  │   │         │  └─ day_pairs (1..20 of `start-end`, ',' sep, ':' terminator)
 ││  │   │  │  │  │   │         └─── loop_min (atoi)
 ││  │   │  │  │  │   └──────────── weekday tags (朝昼夕夜 mix, ':' terminator)
 ││  │   │  │  │  └──────────────── prereq[3]
 ││  │   │  │  └──────────────── prereq[2]
 ││  │   │  └──────────────── prereq[1]
 ││  │   └──────────────── prereq[0]   (first numeric after the leading id-flag pair)
 ││  └───────── flag_on_trigger (atoi after '-')
 └└────────── id (atoi)
```

### Prereq encoding

Each prereq field is a hex value built char-by-char (lowercase
`0`..`9`/`a`..`f` only) terminated by `:`. A `-` byte anywhere in a
field's tail sets a sticky "this field is -1" flag for the field's
commit write, so `-1`, `-2`, `0-`, `f-f` all parse as -1. The
accumulated hex value is discarded when the flag fires.

The 4 prereqs play different roles in the consumer
`FUN_0045de68`:

- `prereq[0]` — "must NOT be set" flag (the consumer tests
  `flags[prereq[0]] == 0` as part of category-eligibility, so this
  is typically a "this exact event was already seen" tag).
- `prereq[1..3]` — "must be set" flags (consumer tests
  `prereq[k] == -1 || flags[prereq[k]] != 0`).

A `:` terminator after the 4th field is required for the parser to
exit the prereq block. The cursor scan caps at 50 chars total
across all 4 fields.

### Weekday-of-day tags

A 2-byte SJIS token block, scanned left-to-right; up to 40 cursor
steps. Recognised tokens:

| token | bytes  | index |
|-------|--------|-------|
| 朝    | `92 a9` | 0     |
| 昼    | `92 8b` | 1     |
| 夕    | `97 5b` | 2     |
| 夜    | `96 e9` | 3     |

The parser records both the **first** matched index (writes to
`time_first` only when no prior match has landed) and the **max**
matched index (writes to `time_max` only when the new index is
strictly greater). On a 2-byte mismatch against all four tokens,
the cursor advances by **1 byte** (NOT 2 — quirk). Full-width
spaces `81 40` therefore advance the cursor twice each.

For the consumer, `time_first <= current_time_of_day <= time_max`
gates the event by time band. With both fields at 0 (no recognised
tokens) the gate degenerates to "morning only".

### Day-range pairs and loop_min

After the second `:` past the tag block, the line carries the
loop-count gate (`loop_min`, atoi) then another `:` then a list of
1..20 `start-end` pairs separated by `,`. The list is terminated
by `:` (or by exhaustion of the 20-pair cap). `end == 999` is a
calendar-day wildcard the consumer treats as "no upper bound".

Each pair is initialised to `(-1, -1)` before parsing; consumer
stops walking pairs at the first `start == -1`.

### Pre-baked default record

Before parsing the file, the engine hand-writes a "default" record
into slot 0 of category 0 (広場) and sets the 広場 count to 1:

```
records[広場][0]:
  id              = 0x0b
  flag_on_trigger = 1
  prereq[0]       = 0xa3       (must NOT be set — エウリア flag)
  prereq[1..3]    = -1
  time_first      = 0          (朝)
  time_max        = 1          (昼)
  day_pairs[0]    = (0, 40)
  day_pairs[1]    = (-1, -1)   (and pairs[2..19] BSS-zero, dormant)
  loop_min        = 0
  decay_or_max    = 100000
```

So the very first **parsed** 広場 line lands at slot 1, not slot 0.
The vendor file's first 広場 line `14- 4:  100:-1:-1:-1...` is the
slot-1 record.

### Engine quirks faithfully reproduced

- **Hex-only prereq accumulator with sticky `-`.** Described above.
  Side effects of the loose grammar: any non-hex/`-`/`:` byte (e.g.
  space) is silently skipped; multi-digit hex values like `1b5`,
  `0xa3` (without the `0x`) parse correctly; a stray `-` anywhere
  in a field's tail nukes the field to -1.

- **Weekday tag mismatch advances 1 byte.** Described above.
  Dormant in vendor data thanks to full-width-space padding.

- **`time_first == 0` is overloaded.** "No matched token" and
  "first matched token was 朝" both leave `time_first = 0`. Same
  for `time_max == 0`. The consumer is happy with the resulting
  "morning-only" gate when a record's tag list is empty.

- **Pre-baked record 0 of category 0.** Described above. The seed
  has `decay_or_max = 100000` while parsed records get
  `decay_or_max = 0`; the consumer tests `decay_or_max <=
  some-counter`, so the seed only fires once the counter has
  ramped past 100000 (probably a "game progression" milestone we
  haven't identified yet).

- **End-of-list sentinel.** The loader writes `id = -1` to the
  slot one past each category's `counts[cat]`, mirroring the
  engine's 4 post-loop writes. The consumer's loop terminator is
  this sentinel.

- **No bounds check on per-category capacity.** The engine doesn't
  guard `local_4c[i] < 100`; an overflow would smash the next
  category's record region (or, for category 3, run into the news.
  txt globals). Vendor data tops out at 38 parsed records in 広場
  → comfortable margin. Port hard-caps at
  `EVENT_RECORDS_PER_CATEGORY = 100` and logs to stderr.

- **Day-pair `:` terminator is required.** The pair-loop's inner
  scan for `,`/`:` is unbounded — if the line ends without either,
  the engine walks off the line buffer into stack junk. Vendor
  data always has the trailing `:`. Port stops at the line's NUL
  terminator for safety.

### Vendor file shape

| stat                                       | value |
|--------------------------------------------|-------|
| Bytes via `storage_read`                   | 8901  |
| 広場 records (incl. pre-baked seed)        | 39    |
| 市場 records                               | 9     |
| 教会 records                               | 9     |
| 酒場 records                               | 19    |
| Total                                      | 76    |
| Records with `prereq[0]` set (not -1)      | 76    |

Boot trace logs `(hiroba=H ichiba=I kyokai=K sakaba=S
with_prereqs=N)`. Every vendor record has a non-`-1` prereq[0] —
the "must not be set" flag is mandatory across the board, since
the engine uses it to ensure each event fires at most once.

### Resolver hook

None — `event.txt` has no cross-table lookups. The `flag_on_trigger`
and `prereq[]` values are bare integers that index into the engine's
global flag bitmap at `&DAT_044e3798` (per-loop) and don't need
resolver wiring against `g_item` or `g_kyaku`.

## `data/news.txt`

The random-news ticker — every line describes one in-game news
message that can fire on a given day, optionally bumping the price
or audience interest for a named item / category / attribute.

### File shape (vendor: 6342 bytes, CRLF, SJIS)

Two sticky-state header lines and three data-row shapes interleave
freely (engine parser at `docs/decompiled/by-address/475270.c`
L1583..L2236). Comment / blank lines are dispatched by
`line[0] in {'/', '\r', '\n'}`; all other lines are dispatched in
this order:

| Pattern        | Effect                                                          |
|----------------|-----------------------------------------------------------------|
| `対象者,N`      | sets sticky `target_group = N` for subsequent non-"-" data rows |
| `時期,A-B`      | sets sticky `period_start = A`, `period_end = B` for subsequent rows |
| `-,-,body`     | "generic" news row — no item-effect; body lives after 2 commas  |
| `<name>,…`     | data row — name + rate + price range [+ days range] + body      |

`対象者` is 6 SJIS bytes, `時期` is 4 SJIS bytes; the atoi for `N`
reads from `line + 7` and `line + 5` respectively.

### Data-row shape

```
<name>,<rate>,<price_lo>-<price_hi>[,<days_lo>-<days_hi>],<body>
```

The days range is **optional** — the engine detects it by peeking
the first byte after the price comma: if it's `'0'..'9'`, parse as
days; otherwise treat as the start of body text. So:

- `武器,1,3-1,The price of weapons has increased.` — no days
- `武器,0,3-1,0-1000,Men of character seek <I>.` — days 0-1000

### `<name>` field resolution

Looked up in this order, **stopping at the first hit**:

1. Special sentinel `特殊` (4 SJIS bytes) → `attr_mask = -1`; no further lookup.
2. SJIS attribute-tag hash (`oder_attr_hash`; engine `FUN_0049e9a7`)
   → `attr_mask = (1u << tag_index)`. Only fires if the field is
   ≥ 4 bytes and its first 4 bytes match one of the 16 attr tags
   listed under `oder.txt` above.
3. Category resolver — prefix-match against `g_item.categories[i].singular`
   → `category = i`.
4. Item resolver — prefix-match against `g_item.records[j].singular`
   → `item_id = records[j].item_id`.
5. Miss → engine pops `MessageBoxA("syn error")`; port logs to stderr.

All three resolvers (special, category, item) use the engine's
`memcmp(name, candidate, name_len)` — i.e. **the news.txt field is
a prefix of the candidate up to its own length**, not exact-equal.
Vendor data always fully matches so the distinction is dormant —
see engine-quirks #28.

### Record layout — `news_record_t` (0xbc bytes = 188 bytes)

| Offset | Size | Field          | Notes                                               |
|--------|------|----------------|-----------------------------------------------------|
| +0x00  | 128  | `body[128]`    | News message; CRLF source leaves trailing `\r` (#30) |
| +0x80  | 16   | `name[16]`     | Parsed name; parser can write up to 20 → overflow (#27) |
| +0x90  | 4    | `rate`         | `atoi` of field 2; +N price up, -N down, 0 customers up |
| +0x94  | 4    | `price_lo`     | `atoi` of field 3 before `-`                        |
| +0x98  | 4    | `price_hi`     | `atoi` of field 3 after `-`                         |
| +0x9c  | 4    | `attr_mask`    | `oder_attr_hash`; -1 for `特殊`; 0 for `-` / no attr |
| +0xa0  | 4    | `category`     | Category index; -100 for `-` rows; -1 if no match   |
| +0xa4  | 4    | `item_id`      | Item id (NOT slot); -1 if no match; 0 for `-` rows  |
| +0xa8  | 4    | `target_group` | Sticky `対象者:`; 0 for `-` rows (engine never writes) |
| +0xac  | 4    | `days_lo`      | `atoi` if optional days present; -1 absent; 0 for `-` |
| +0xb0  | 4    | `days_hi`      | `atoi`; same defaults as `days_lo`                  |
| +0xb4  | 4    | `period_start` | Sticky `時期:` start (default 0)                    |
| +0xb8  | 4    | `period_end`   | Sticky `時期:` end (default 100)                    |

Boot trace logs `(news=N dash=D special=S attr=A category=C item=I)`.

### Faithfully-reproduced quirks

- **Name buffer can overflow into rate (#27).** Parser writes up to
  20 bytes at +0x80, but the field is structurally 16. The NUL
  terminator at `name[name_len]` can land in `rate` / `price_lo` /
  `category` for names 16..20 bytes long. Dormant in vendor data
  (longest name = 12 bytes).

- **Prefix-by-name-length lookup (#28).** All three name resolvers
  use `memcmp(name, candidate, name_len)`; a short news.txt name
  matches any candidate it's a prefix of. Vendor names always
  exactly equal their candidate.

- **"-" data rows leave fields at BSS-zero (#29).** The "-" branch
  doesn't write `target_group` / `item_id` / `days_lo` / `days_hi`,
  so they stay at 0 (the `memset` default), not -1 like a non-"-"
  row's "no match" sentinel. Vendor data sidesteps this by always
  emitting `対象者,0` before its "-" rows.

- **Body retains trailing `\r` on CRLF (#30).** Line-collect stores
  the terminating `\r` in the line buffer; body-copy stops at `\0`
  / `\n` but not `\r`. Every CRLF-source body has a trailing `\r`
  byte before its NUL — preserved for byte-identical behaviour.

- **`時期,A` (no '-') keeps `period_end` unchanged.** Engine emits
  "loop err 6" and continues without writing `local_20`. Port
  matches via `strchr(line+5, '-')` — null pointer → skip the
  second `atoi`.

- **Empty name vacuously matches anything.** `FUN_00479f4d(name,
  candidate, 0)` returns 1 with no comparison. Dormant in vendor
  data (the parser only enters the name-write loop after confirming
  `line[0] != '-'`, and an empty name field starting with `,` would
  also have to dodge the "must have a `,`" check downstream).

- **`対象者,N` polarity.** "対象者" is 6 SJIS bytes (3 chars); the
  atoi for N reads at offset 7 (skipping the comma). `時期,A-B`
  reads A at offset 5 (4 bytes + 1 comma) and B after `strchr(line,
  '-') + 1`.

### Resolver hook

Two callbacks wired in `src/tables.c`:

- `news_resolve_category(name, name_len, g_item)` — prefix-match
  against `g_item.categories[i].singular`.
- `news_resolve_item(name, name_len, g_item)` — prefix-match
  against `g_item.records[j].singular`; returns the item's id,
  not its slot.

Both depend on `load_item_txt` having run first (which it has, by
the engine's load order). Empty-singular slots are skipped so the
unpopulated 100-slot category array doesn't generate false
positives on an all-NUL prefix.

## `data/enemylist.txt`

The per-dungeon, per-floor-range enemy spawn roster — plus a small
"wisp drop" item table at the top of the file. Vendor: **28281
bytes** (largest of the gameplay-table files), CRLF, SJIS.

### File shape

Five line kinds are dispatched in this order (engine: `FUN_00475270`
L2581..L2899):

| Pattern               | Effect                                                            |
|-----------------------|-------------------------------------------------------------------|
| `// …` / blank / `/…` | comment — skipped (`line[0] in {/, \r, \n}`)                      |
| `wispN:<item-name>`   | wisp drop table — slot N-1 (1..9 only — see quirk #32)            |
| `ダンジョン１..６`     | dungeon header — switches dungeon-slot to 0..5; resets section index |
| `f:N` or `f:N-M`      | floor header — opens the next section in the current dungeon       |
| `<enemy>[(<v>)][x<n>]:<d1>[#<d2>[#<d3>]]` | enemy line — spawns an enemy in the current section |

State carries between lines:

- `local_20` — dungeon-slot index (0..5 set via key match; 0 default).
- `local_14` — section index within current dungeon, 0..59.
  Incremented on every `f:` line, reset on every dungeon header.
- `local_18` — enemy slot index within current section, 0..30.
  Reset on every `f:` line.

### Engine globals populated

| Global               | Address              | Shape                                  |
|----------------------|----------------------|----------------------------------------|
| sections grid        | `&DAT_0053f8e8`      | `enemylist_section_t [10][60]` — 451200 bytes |
| wisp drop table      | `&DAT_073d8630`      | `int32_t [10]`                         |

Only 6 of the 10 dungeon slots are addressable (no keys exist for
`ダンジョン７..10`); the trailing 4 stay at their post-init `floor_lo
= -1` / `enemy_id = -1` sentinels (quirk #31).

### Section layout (`enemylist_section_t`, 752 bytes = 0x2f0)

| Offset | Size | Field                  | Notes                                          |
|--------|------|------------------------|------------------------------------------------|
| +0x000 | 4    | `floor_lo` (int32)     | `atoi(line+2) - 1` after `f:`                  |
| +0x004 | 4    | `floor_hi` (int32)     | `atoi(line+dash+1) - 1`; = `floor_lo` if no '-'|
| +0x008 | 12×31 | `enemies[31]`         | Per-slot: `{enemy_id, variant, count}`         |
| +0x17c | 12×31 | `drops[31]`           | Per-slot: `int32_t item_id[3]`                 |

Per-enemy slot (12 bytes):

| Field      | Init  | Set by                                  |
|------------|-------|-----------------------------------------|
| `enemy_id` | -1    | Per-line longest-prefix lookup → index into `g_enemy[]` |
| `variant`  | 0     | `atoi(line+pos+1)` after `(`            |
| `count`    | 1     | `atoi(line+pos+1)` after `x`            |

Per-drop slot (12 bytes): `item_id[0..2]` each init to -1 per-line;
populated by the `:`/`#` field walker, resolved through the
caller-supplied `tables_item_resolve` callback.

### Enemy-name lookup

Same "longest common prefix" rule as `enemy.txt`: walk the 64
pre-baked records in `g_enemy[]`, match each record name as a
prefix of the line's leading bytes, keep the record whose name is
the **longest** such prefix. Records whose name is empty (or whose
`flags == 2` sentinel fires) are skipped.

Pre-baked names live in `tables_enemy.c`; the enemylist parser
references the same table (loaded earlier via `load_enemy_txt`).
Lines whose leading bytes do not prefix any record are silently
dropped — the engine pops `MessageBoxA("無効な敵ネーム")` (quirk #21
applies here too); the port stays quiet.

### Wisp / drop item lookup

The `<item-name>` field of `wispN:` lines, and the `:`/`#`-separated
`<dN>` fields of enemy lines, are resolved to item ids by
`tables_item_resolve(g_item, name)`. The engine implements this as
a double-`FUN_00479f4d` (`memcmp` once with each side's strlen),
which behaves like an exact-string match for any well-formed input.

Unknown names yield -1; engine emits `MessageBoxA("不明なアイテム4")`
or `("不明なアイテム6")`; port silently stores -1.

### Faithfully-reproduced quirks

- **Slots 6..9 are dead storage (#31).** Init zeros all 10×60
  sections, but no SJIS dungeon key dispatches to them. Trailing 4
  dungeons stay at their post-init sentinel state forever.

- **`wisp10:` silent-drops (#32).** The name-copy loop reads from
  `line[0x26]` (`line+6`). For `wisp1..wisp9`, that's the colon
  (well, the byte after the digit) — actually, that's the digit's
  *next* byte. For `wisp10:`, byte 6 is the `:` and the name-copy
  terminates immediately. Vendor only ships wisp1..wisp6 so this
  is dormant. (Note: vendor `wisp1:` and `wisp2:` are empty-value
  lines, deliberately leaving slots 0/1 at -1.)

- **Slot-30 terminator hazard (#33).** Engine writes `enemy_id = -1`
  to slot `local_18 + 1` after each enemy line. If a section ever
  pushes `local_18` to 30 (max writable), the terminator lands at
  slot 31's `enemy_id` — which is the **first drop dword of slot 0**
  in the same section. Dormant in vendor (no `f:` block exceeds
  ~12 enemies). The port logs the overflow to stderr and skips
  the line rather than clobbering drops[0].

- **Per-line drop reset.** `drops[slot].item_id[0..2]` are set to
  -1 at the start of each line's enemy write — so a line that
  omits a drop column ends with that slot at -1 rather than the
  previous value.

- **State sticky across lines.** Dungeon slot, section index, and
  enemy slot persist until the next header overrides them. An
  enemy line before any `ダンジョン` header lands in dungeon 0; an
  enemy line before any `f:` lands in section 0. Vendor data never
  emits enemies in that position.

- **Empty `(` / `x` → atoi(0).** Variant/count parsers do not
  guard against missing digits. Vendor always supplies them.

- **`f:N` (no dash) → `floor_hi = floor_lo`.** Dash-scan terminates
  on `\r`/`\n` without finding `-`; the second atoi never runs and
  the floor-lo value is reused for floor_hi.

- **`f:` (no digits) → "loop err 16" + line skipped.** Engine
  writes `atoi("") - 1 = -1` to `floor_lo` BEFORE detecting the
  empty value, then prints loop err and bails — leaving the
  section in a half-initialised state. Port mirrors the write
  + skip.

### Resolver hook

- `resolve_via_item_state(name, &g_item)` — same adapter used by
  `enemy.txt` and `gousei.txt` (both already wired). Calls
  `tables_item_resolve(g_item, name)` which is an exact-string
  match on `g_item.records[i].singular`.

### Vendor file shape

| stat                                       | value |
|--------------------------------------------|-------|
| Bytes via `storage_read`                   | 28281 |
| `wisp` lines                               | 6 (`wisp1:`/`wisp2:` empty; 3..6 populate slots 2..5) |
| Dungeon headers                            | 6 (`ダンジョン１..６`) |
| `f:` lines                                 | 100   |
| Enemy lines parsed (matching pre-baked record) | ~696   |
| Enemy lines silently dropped (unknown name)| ~50 — same quirk #21 cohort as enemy.txt |

Boot trace logs `(sections=S enemies=E drops=D resolved=R wisps=W
wisp_resolved=WR)`. With g_item live, R should equal D and WR
should equal W (every name in vendor data is a real item).
