# Gameplay tables loader (`FUN_00475270`)

**Status:** discovery + skeleton only (2026-05-20). Outer dispatcher
lands as `src/tables.c:tables_load_all` with one stub per file; the
14 individual parsers will land one per commit going forward (see
`docs/PROGRESS.md` for in-flight progress).

**Engine label:** the boot trace immediately after this function
returns is `"init indexfile ok"` (string at `0x005cc170`, emitted by
`FUN_0047aa31` at `0x47bfb3:0x47bf...` — the WinMain init chain).
The engine itself calls these files "index files" even though most of
them live under `data/`. We follow the engine's term in code comments
and use `tables` only as the C-module name (because "index" collides
with the lnkdatas index in the storage layer, which is a completely
different thing).

## Caller context

Called exactly once, from `FUN_0047bfb3` (WinMain / main init), in
this slot:

```text
init strage ok        — FUN_004341fe — storage_init        (ported)
init start            — FUN_0047ac6a — ?                   (TODO)
init print ok         — FUN_00451863 — print/text setup    (TODO)
init dinput ok        — FUN_0047af52 — input_init          (ported)
init render ok        — FUN_00454e69 — layers_init         (ported)
init indexfile ok     — FUN_00475270 — tables_load_all     ← THIS
init fontsys ok       — FUN_0047c228 — font system init    (TODO)
init daoudio ok       — FUN_00498ef4 — DirectSound init    (TODO)
fontsystem ok         — FUN_0047c3a5 — ?                   (TODO)
read systemtex ok     — FUN_00472f5d — system textures     (TODO)
load savefile ok      — FUN_004902fe — save file           (TODO)
read titletex ok      — FUN_0043609b + FUN_004733d5        (TODO)
                      — FUN_0049a3a3 enters main loop      (TODO)
```

The function takes no args, returns void, and is purely a side-effect
populator of engine globals. Failure paths inside it call
`MessageBoxA(... "error" ...)` and continue (no fatal abort) — the
game will run with partial / missing tables and likely break later.

## Shape

3965 lines of decompiled C; ~19.6 KiB of x86. Internally it's
**fifteen mostly-independent file-load blocks** with the same outer
structure each time:

```c
sz  = storage_get_size("data/foo.txt");   // FUN_00434585
buf = HeapAlloc(...sz...);                // HeapAlloc on the global heap
storage_read("data/foo.txt", buf);        // FUN_004346bf
... per-file parsing into globals ...
HeapFree(g_heap, 0, buf);                 // via FUN_005036af (tiny wrapper)
```

We already have `storage_get_size` and `storage_read` ported, so the
infrastructure is free — what's left is the per-file parsing.

Helper identities (cross-checked against the CRT shapes):

| ghidra            | identity                          |
|-------------------|-----------------------------------|
| `FUN_00434585`    | `storage_get_size` (ported)       |
| `FUN_004346bf`    | `storage_read` (ported)           |
| `FUN_00503d03`    | `atoi` (thunk to `FUN_00503c78`)  |
| `FUN_00503c2b`    | `atof` (returns float10/long double) |
| `FUN_005036af`    | `free` (HeapFree on global heap)  |
| `FUN_005038ff`    | `sprintf` (5+ arg variadic)       |

## File list (in load order)

Sizes are the decompressed asset size as shipped in `vendor/original/`
(extracted via `tools/extract/data-bin.py`). "C lines" is the
decompiled-block size in `docs/decompiled/by-address/475270.c` — a
proxy for parser complexity, not asset size.

| # | path                  | bytes  | C lines | starts | format family   |
|---|-----------------------|--------|---------|--------|-----------------|
| 1 | `idx/stage.idx`       | 22434  |    275  | L55    | stage records   |
| 2 | `idx/config.idx`      |   950  |     98  | L330   | `/key:value`    |
| 3 | `data/item.txt`       | 122010 |     41  | L428   | CSV-ish         |
| 4 | `data/kyaku.txt`      |  7603  |    365  | L469   | customer recs   |
| 5 | `data/enemy.txt`      |  2801  |    196  | L834   | enemy recs      |
| 6 | `data/chara.txt`      |  1868  |    266  | L1030  | character recs  |
| 7 | `data/buysell.txt`    |   504  |     82  | L1296  | buy/sell ratios |
| 8 | `data/oder.txt`       |  1686  |     44  | L1378  | orders/quests   |
| 9 | `data/model.txt`      |  1758  |     99  | L1422  | 3D? model defs  |
|10 | `data/event.txt`      |  8901  |     62  | L1521  | event scripts   |
|11 | `data/news.txt`       |  6342  |    655  | L1583  | news entries    |
|12 | `data/snews.txt`      |  2230  |    166  | L2238  | short news      |
|13 | `data/gousei.txt`     |  6254  |    177  | L2404  | synthesis recipes |
|14 | `data/enemylist.txt`  | 28281  |    322  | L2581  | enemy roster    |
|15 | `data/tuto_N.txt`     | varies | 1062*   | L2903  | tutorial loop   |

`*` = includes the tutorial *loop* plus function epilogue / cleanup.
`tuto1.txt`, `tuto2.txt`, `tuto3.txt` are present in
`vendor/original/`.

The "bytes" column is the lnkdatas-archive decompressed size. At
runtime, `storage_read` checks the bmpdata overlay first and may
return a different (patched) size for files the post-release update
shipped — observed via our boot logs: e.g. `enemy.txt` is 2801 bytes
in lnkdatas but 3589 bytes via storage (overlay version). Parser
ports must work against either version.

## Format observations

All files are **Shift-JIS (cp932)** encoded text. The engine reads
them as raw bytes; multi-byte Japanese characters appear in names and
descriptions but never in the parser's column-key positions.

Two format families observed in the source files:

**`/key:value` family** (`config.idx`):

```text
/font:ＭＳ Ｐゴシック
/kanjioff:
edgewi:2
edgedel:6
/effectmode:
```

The decompiled parser at L370-393 checks `local_27c[0x20..]` against
literal keys `"effectmode\0"`, `"edgedel\0"`, `"makefont\0"`,
`"font\0"` — i.e. the parser splits each line at byte 0x20 and
treats the tail as the key name. (The leading `/` is the engine's
in-file "active" marker; lines without `/` are treated as comments
for some keys.)

**CSV-with-comments family** (`item.txt` and most `data/*.txt`):

```text
// アイテム
// 名称,値段,効果量,種類,出現,説明
// 効果量は、回復量やパラ上昇の数値になります
// (... long comment block ...)
//■■■■装備品・武器■■■■
```

Lines starting `//` are comments; data rows follow the column order
declared in the column-header comment. Per-file column counts and
widths differ.

The frequently-seen idiom `FUN_00503d03(local_27c + 0xNN)` in the
decompiled parsers is `atoi` scanning from a *byte position within
the line buffer* — i.e. each record is **fixed-column**, not
delimiter-separated. atoi/atof skip leading whitespace before the
first digit, so the engine doesn't need to nail the exact starting
column, just the field start.

## Engine quirk: mismatched interned paths for `config.idx`

Every block calls `storage_get_size(path)` and then
`storage_read(path, buf)` — but the decompiled C shows the two calls
referencing **different addresses in `.data`** for the path argument
(e.g. `0x005cac78` vs `0x005cac84` for the config block). For most
files those addresses just hold two interned copies of the same
string (e.g. both spell `"idx/stage.idx"`), and the dual storage is
benign compiler bookkeeping.

For `config.idx` the developer accidentally typed two **different**
spellings:
- get_size path = `"config.idx"`  (no folder prefix)
- read path     = `"data/config.idx"`

The lnkdatas index only has `data/config.idx`, so the get_size call
returns 0 in the engine. Allocation is then `malloc(0 + 10) = 10
bytes`, into which `storage_read` happily decodes the full 950-byte
file — a quiet 940-byte heap overrun on every boot of the original.
The game survives because the next bytes on the heap evidently aren't
load-bearing for the rest of init.

Our `tables.c` uses the **read-side** spelling (`"data/config.idx"`)
for both the size and the read, sidestepping the overrun. A
faithful reproduction would require mirroring the get_size call with
the buggy path; see `docs/findings/engine-quirks.md` if/when we
formalise that catalog.

The tutorial loop also has a path-string subtlety: the format
literal at `0x005cb38c` is `"data/tuto%d.txt"` (no underscore).
Vendor ships `tuto1.txt`, `tuto2.txt`, `tuto3.txt`; the loop stops
at the first storage miss.

## Globals populated

Not yet catalogued per-file — that will happen as each loader is
ported (the global symbol names appear in the decompiled assignments,
e.g. `_DAT_005cbc74 = FUN_00503d03(local_27c + 0x27);` at L370,
which is one of the `edgewi`/`edgedel` settings from `config.idx`).
Per-file global tables will be declared in `src/tables.h` and live
in `src/tables.c` (or per-file submodules if any grow large enough
to warrant splitting).

## Phasing

- **Phase A** (this commit): findings doc + `src/tables.{c,h}`
  skeleton with `tables_load_all()` dispatcher calling 14 stub
  loaders, each of which `storage_get_size`s and `storage_read`s
  its file and logs the size. No parsing yet. Boot wire-up: replace
  the TODO at `src/main.c:158`.

- **Phase B** (one commit per file, smallest first): port the
  parsers. Per-file checklist:
  - Identify the per-file globals (struct + array dimensions)
    from the decompiled assignments.
  - Port the parser, mirroring the C as closely as readable.
  - Add a unit test in `tests/` that loads the real
    `vendor/original/` asset (via `storage_read`, gated like the
    existing vendor-dependent tests) and validates a handful of
    canonical entries.
  - Add a format-spec section in `docs/formats/data-text.md`.
  - Update `docs/PROGRESS.md`.

  Suggested order (smallest first, by raw byte size):
  `buysell.txt → config.idx → oder.txt → model.txt → chara.txt →
  snews.txt → enemy.txt → tuto_*.txt → gousei.txt → kyaku.txt →
  event.txt → news.txt → stage.idx → enemylist.txt → item.txt`.

- **Phase C**: behavioral validation — boot the exe, confirm the
  "init indexfile ok" trace position; diff a few derived gameplay
  states (e.g. item lookup by id) against the original.
