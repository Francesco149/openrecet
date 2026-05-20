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
