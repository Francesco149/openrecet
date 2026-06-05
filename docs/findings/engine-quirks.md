# Engine quirks — a fun tour of 2007's "Azumanga"

A curated list of weird, charming, and occasionally inexplicable things
found in EasyGameStation's engine while reverse-engineering it.  These
aren't bugs (well, mostly) and they aren't load-bearing for anyone using
the game — they're just the kind of texture that makes RE work feel less
like archaeology and more like reading someone's diary.

Each entry: what we found, where we found it, and (when it's interesting)
why.  Citations to engine functions are `FUN_XXXXXX` Ghidra addresses
from `docs/decompiled/by-address/`.

---

## 1. "CRC-16/CCITT", but with subtraction

`FUN_00474f14` (ported to `src/lnkdatas_hash.c`) is the file-integrity
hash used on `lnkdatas.bin`, `bmpdata.bin`, and the JP `lnkdata.bin`
variant.  At first glance: poly `0x1021`, init `0xFFFF`, MSB-first, final
invert — i.e. textbook CRC-16/CCITT-FALSE.

It's not.  Where the standard does `crc ^= byte`, EGS's hash does
`crc -= byte`.  Subtraction.  With borrow propagation.

The check value for `"123456789"` is `0xF5B7`.  Standard CCITT-FALSE
would give `0x29B1`.  The shape is identical, the math is foreign.

It's still a perfectly serviceable corruption detector — it just isn't
something you can implement from a reference snippet without noticing.
We learned this by trying to validate against `crc.py` in
`/opt/src/recettear-repacker` and watching every byte mismatch.

> 📍 `src/lnkdatas_hash.c`, `docs/findings/winmain-and-bootstrap.md` "hash"
> section.

---

## 2. The Japanese release "encrypts" the index by subtracting from 1

`lnkdata.bin` (JP, no trailing 's') has a 5-byte header followed by a
payload that's been XOR-… no, transformed by the absolutely-not-XOR
function `dst[i] = 0x01 - src[i]`.  Per byte.  No key.

The expected hash sentinel is different (`0xC5E1` instead of the EN
`0x8BAA`), so the engine can tell which variant it loaded — there's a
global flag (`_DAT_0438abdc`) set to 1 only when the JP path fired.

The English release does none of this — `lnkdatas.bin` is plain.
Presumably the obfuscation got dropped at localization time.

It is one of the lowest-effort obfuscation schemes possible while
still technically being "obfuscation."  We love it.

> 📍 `src/storage.c:storage_init` JP branch.  `FUN_004341fe`.

---

## 3. bmpdata.bin: the patch mechanism that solved a real problem

The engine checks `bmpdata.bin` for every asset lookup **first**, before
the much larger `bin/data*.bin` archive.  If the asset is in bmpdata,
that copy wins; otherwise fall back.

This is the dev team's patching scheme: ship a small `bmpdata.bin`
update instead of re-pressing the 120 MB of striped data archives.
Beautifully practical.  Carpe Fulgur's English translation ships a
`bmpdata.bin` that's mostly the localized UI text-textures — 22 entries
on the current Steam build, against 1188 entries in the main archive.

The catch: the two indexes have **different name-comparison semantics**
(see quirk #4).

> 📍 `docs/formats/data-bin.md`, `docs/formats/bmpdata.md`.

---

## 4. Case-sensitivity flips between the two indexes

In `FUN_00434585` (asset-lookup) and `FUN_004346bf` (asset-read), there
are two name-matching loops, one per index, in the same function:

| index      | bound      | case-sensitive? |
|------------|-----------:|-----------------|
| bmpdata    |  88 bytes  | **no** (folds A..Z → a..z) |
| lnkdatas   | 128 bytes  | **yes** (straight byte compare) |

So `"BMP/window.tga"` hits via bmpdata but misses via lnkdatas.  Names
in both indexes are stored lowercase, so this only matters when a
caller passes a non-canonical-case path — but at least one engine
caller seems to rely on the fold (we haven't tracked them all down).

Best guess for why: the bmpdata case-fold loop was added when the
patch overlay landed, possibly to be defensive about hand-typed paths
in the patch index.  The lnkdatas loop is older and predates the
caution.

> 📍 `src/storage.c:bmpdata_name_eq` vs `lnkdatas_name_eq`.

---

## 5. The 500-millisecond, 3-times retry around `fopen`

When loading an asset from the lnkdatas / `bin/data*.bin` stream
(`FUN_004346bf`), if the `fopen("bin/data%03d.bin", "rb")` fails, the
engine sleeps **500 ms** and tries again.  Up to **three times**.

This is so clearly an artifact of writing for 2007-era spinning hard
drives that you can almost hear the original disk thrash.  We dropped
the retry in our port — on a Steam install in 2026 it's not earning
its keep.

> 📍 `FUN_004346bf` `LAB_004349bb` retry loop.

---

## 6. The LZSS decoder unrolls the all-literal case by hand

`FUN_004349e5`, the LZSS decompressor, has a fast path for when the
8-bit control byte is `0x00` (i.e. all 8 flag bits are literals).  It
copies 8 bytes one statement at a time, no loop:

```c
*param_2 = *pbVar4;
param_2[1] = param_1[2];
param_2[2] = param_1[3];
param_2[3] = param_1[4];
param_2[4] = param_1[5];
param_2[5] = param_1[6];
param_2[6] = param_1[7];
param_2[7] = param_1[8];
```

A 2007-era manual `memcpy(dst, src, 8)`.  The compiler probably would
have done the same with a loop, but presumably someone profiled this
and decided not to leave it to chance.

We ported it as a plain loop because we trust GCC 15 to spot the
pattern.  If a pixel diff ever comes back odd, suspect here first.

> 📍 `src/lnk_lzss.c`, `docs/decompiled/by-address/4349e5.c`.

---

## 7. The LZW bit reader keeps reading past EOF — by accident

`FUN_00434c2c` (the bmpdata LZW 12-bit MSB-first bit reader) doesn't
range-check the input buffer.  Once past EOF, it returns the value
`0x0c` repeatedly — which happens to be the bit count it was requested
to read.  That's because it's reading from an uninitialized stack
variable that the caller initialized to `0x0c` (the bit count) earlier
in the frame.

It doesn't matter, because the main loop exits on the next iteration
anyway.  But it's the kind of thing that's load-bearing-by-accident:
if the compiler ever decided to put a different value at that stack
slot, the engine would suddenly start decoding garbage past EOF.

> 📍 `docs/formats/bmpdata.md` "The bit reader" section.

---

## 8. The LZW dictionary freezes at 3839 entries and never resets

Mid-stream LZW resets are a standard feature of the format.  This
engine's bmpdata decoder honors code 256 (the reset marker) **only if
it's the very first code in the stream**.  After that the marker is
ignored mid-stream — the dictionary just keeps filling until it hits
4095 entries (256 literal codes + 3839 dictionary codes), then freezes.

In practice every shipping bmpdata slice puts a code-256 sentinel at
the very end of the stream as a kind of polite "I'm done now" gesture
that the decoder doesn't actually need.

> 📍 `docs/formats/bmpdata.md`.

---

## 9. `bin/data*.bin` is striped at exactly 10 MiB per chunk

The "archive" is logically one giant compressed-asset stream that the
build process slices into 10,485,760-byte (`0xa00000`) files:
`data000.bin`, `data001.bin`, …  An entry whose compressed payload
straddles a 10 MiB boundary just keeps reading into the next file.

10 MiB is the exact size of a 700 MiB CD divided by 70, or a single
floppy-disk … no it isn't.  It's just a round number in bytes.  We
think the splitting was originally for CD media where individual
files over some size limit caused authoring tools to balk, but by
2007 that constraint had long since evaporated and the engine just
kept the format.

> 📍 `src/storage.c:LNKDATAS_CHUNK_SIZE`.

---

## 10. Fullscreen uses `D3DSWAPEFFECT_COPY_VSYNC` instead of `DISCARD`

`COPY_VSYNC` was a perfectly reasonable choice circa 2002 — it keeps
the back-buffer contents valid across frames, which simplifies
incremental rendering.  By 2007 (when this engine shipped) and
certainly by 2026 (when we're porting it), `DISCARD` is the obvious
pick for fullscreen — let the driver throw away the contents and
pick the fastest swap path.

But the engine knows what it wants.  We mirror it, because changing
swap effects can sometimes shake loose pixel diffs (the back-buffer
being readable post-Present is something gameplay code can in
principle rely on).

> 📍 `docs/findings/winmain-and-bootstrap.md` "Direct3D init" section.

---

## 11. Every UI string lives inline in `.rdata`

No string table.  No resource file.  No `.txt` lookup.  When the
engine wants to show "RECETTEAR Ver 1.108", that literal byte sequence
is sitting in `.rdata` at a fixed RVA, and the `WndProc` `SetWindowTextA`
call has a direct pointer to it.

The practical consequence: **translating Recettear means rebuilding
the binary**.  Carpe Fulgur's localization team did exactly that.
There's no "drop in a translation .csv" workflow — you patch the PE.

We're not going to translate anything, but it's a hilarious window
into how casual 2007 game engineering was about i18n: just put the
string where the function needs it, ship it.

> 📍 Anywhere the engine talks to the user.  `docs/findings/imports-and-layout.md`.

---

## 12. The 8× over-allocated working buffer in the texture loader

`FUN_0047193c` allocates a working buffer **8× larger** than the asset
file when loading a sprite (compared to e.g. `dsize` from the index).
Then it passes `MipLevels=1` to `D3DXCreateTextureFromFileInMemoryEx`.

We think the 8× comes from D3DX implicitly generating mips in some
configurations, even when `MipLevels=1`.  Or it could be a
"definitely enough" cushion picked by hand.  Either way it's
generous, and we kept the 8× factor in our port until we can confirm
which D3DX revision actually expanded the buffer.

> 📍 `docs/findings/texture-loader.md`.

---

## 13. The opening movie plays via `mciSendString`

`recet_op.wmv` is the EGS logo intro.  The engine opens it with
**`mciSendString("open … type mpegvideo")`**, an MCI command-string
API that was already feeling vintage in 2007 and now triggers a
warm wave of nostalgia in anyone who wrote multimedia code in the
'90s.

It still works on Windows 11.  Microsoft never breaks MCI.

> 📍 Engine reference: the WINMM import + the `recet_op.wmv` string in
> `.rdata`.

---

## 14. The asset paths reference files that don't exist on disk

Strings like `"bmp/item/item%02d.bmp"` and `"data/item.txt"` appear in
`.rdata` and get passed to `fopen` (which fails) before falling
through to `storage_read`.  The disk-first path is real — drop a file
at `vendor/original/data/item.txt` and the engine will use it.

This is almost certainly a developer's mod / debugging convenience:
during development, paths point to files on disk; for release, the
files get baked into `bin/data*.bin` but the disk paths stay in the
code so you can override them by dropping a file alongside the exe.

Modders have used this exact mechanism for English patches before
Carpe Fulgur's official release.  The engine wasn't designed for
modding, but it's friendly to it anyway, by accident.

> 📍 `src/sprite.c:read_asset` mirrors this disk-first / storage-fallback
> pattern (originally `FUN_0047193c`).

---

## 15. The hash sentinel for bmpdata is the only positive one

The integrity-check sentinels are sign-extended `int16_t` comparisons:

- lnkdatas EN: `(int16_t)0x8BAA == -0x7456`  (negative)
- lnkdatas JP: `(int16_t)0xC5E1 == -0x3A1F`  (negative)
- bmpdata:     `(int16_t)0x21DC ==  0x21DC`  (**positive**)

This isn't load-bearing — they're all just 16-bit values being
compared — but it's the kind of thing that catches your eye when
you're translating the decompilation and suddenly the "expected
hash" constant changes sign.  The bmpdata format presumably got
its sentinel rolled later and the developer picked one that didn't
need the sign-extension trick.

> 📍 `src/storage.c` `LNKDATAS_HASH_*` / `BMPDATA_HASH` macros.

---

## 16. `xfile/*.x` are text-format DirectX 3D files (still!)

DirectX `.x` supports both text and binary encodings; the engine
ships the text form.  Open `xfile/city/dun_city00.x` in a text editor
and you get readable `Mesh { … MeshNormals { … }` blocks.

This is enormously friendly to modding.  It also means the engine
parses these files at load time with a relatively slow text parser
through `DirectXFileCreate` — perfectly fine for a 2007 game whose
scenes have low polygon counts but a surprising choice if you came
to it expecting binary `.x`.

Only standard DirectX retained-mode templates are used.  No custom
template extensions.  Stock everything.

> 📍 `docs/formats/xfile.md`.

---

## 17. SteamStub DRM disguising a 2007 binary in a 2010s shell

`recettear.exe` ships with **SteamStub** packing (VLV signature at
offset `0x40` in the PE header).  Steamless unpacks it cleanly to a
5.0 MB native PE with 6 sections and 6 static DLL imports
(`KERNEL32`, `USER32`, `SHELL32`, `WINMM`, `ole32`, `ADVAPI32`).

That import list is *tiny*.  No CRT DLL.  No `MSVCR*.dll`.  No
`d3d8.dll` (loaded dynamically via `LoadLibraryA`).  This is a binary
that's almost entirely standalone — exactly the kind of self-contained
2007-era exe that makes RE feasible in the first place.

> 📍 `docs/findings/imports-and-layout.md`.

---

## 18. Sound effects live in a 3-level directory tree

`bin/se/<group>/<sub>/<name>.bin` is the SFX layout.  Three levels
deep.  Inside the archives.  For sounds.

Compare to most contemporary engines that flatten audio to
`audio/sfx_*.wav`.  EGS's hierarchy presumably groups by event
category (combat / UI / ambient) then by subcategory then by
specific sound, which is genuinely a useful authoring structure
even if it's deeper than fashion would dictate.

> 📍 Strings in `.rdata`; not yet wired into our port.

---

## 19. `chara.txt` parses two records into the next array

`FUN_00475270` block #6 (the `chara.txt` loader, ported to
`src/tables_chara.c`) sets up an 8-record array and then runs a
parse loop that iterates *ten* times.  The init loop walks
`&DAT_073ae060 → &DAT_073ae260` in 0x40-byte strides — 8 iterations.
The parse loop walks `&DAT_073ae058 → &DAT_073ae2d8` in 0x40-byte
strides — 10 iterations.  Same stride, different end pointer, off by
exactly two records.

The catch: `g_chara` is **byte-adjacent** to `g_models`.

```text
&DAT_073ae058    g_chara[0]  ── start of 8-record adventurer array
…
&DAT_073ae058 + 8 * 0x40
&DAT_073ae258    g_models[0]  ── start of 20-record 3D-model array
```

The parse loop's end pointer `0x73ae2d8 = 0x73ae258 + 2 * 0x40`
lands two model records *into* `g_models[]`.  Each iteration calls
`sprintf("%03d:", iVar1)` against the current line — so any
`008:` or `009:` line in chara.txt silently overwrites
`g_models[0]` (which holds the first kine model's `.x` filename and
bone-attachment names), and any `108:` / `109:` line writes six ints
into the second kine model's name field starting at offset 0x28
within the 0x2b8-byte record.

There's no `MessageBoxA`, no error path, no length check — the
parser is happily walking off the end into a different global.  In a
language with bounds checks this would be a tidy little overflow CVE.
In raw x86 it's just two extra `cmp`/`jne` instructions away from
correctness.

Vendor `data/chara.txt` ships only indices `000`-`007` and
`100`-`107`, so the bug is dormant in production.  Our port caps the
inner match loop at `CHARA_COUNT` (8) and silently drops out-of-range
lines, which is what the engine *would have* done if the loop bound
had matched the init.

The init loop and the parse loop are both about ten lines apart in
the decompiled output — likely the developer extended the parse
range (perhaps planning to add Recette herself as record `008`, or
to support a future expansion) and forgot to extend the init loop in
lockstep.  We'll never know.  But it's a great illustration of why
"the array has N entries" is not a property the engine knows about
itself: there's no `count` field, no header, no `#define` — just two
hardcoded address constants that have to agree by hand.

> 📍 `src/tables_chara.c`, `docs/formats/data-text.md` "chara.txt"
> section.

## 20. `snews.txt` floor-range writes land on the previous dungeon

Tied with quirk #19 for "best argument for not writing pointer-juggling
parsers in C," this one's a classic write-then-advance mistake.  The
snews.txt parser maintains four locals: `local_20` (current dungeon
index, 0-5), `local_14` (sections-into-current-dungeon counter),
`local_18` (entries-into-current-section), and `local_c` (a raw pointer
into the `&DAT_073b2108` section grid where the next floor-range +
entries are about to land).

When a `ダンジョン{1..6}` line matches, the engine resets `local_14`
and `local_20` — but **not `local_c`**.  When the next `f:N-M` line
arrives, the handler:

1. Writes `(N, M)` to `local_c[+0..+7]` (the floor_start / floor_end
   pair) — using the **old** value of `local_c`, which is still
   pointing at the last section the previous dungeon wrote into.
2. Recomputes `local_c = base + (local_14 + local_20 * 30) * 0xa8`.
3. Increments `local_14`.

So on a dungeon transition, the very first `f:` line of the new
dungeon **overwrites the floor info of the LAST section of the old
dungeon**.  Specifically, every dungeon except the last (dungeon 6,
which has no successor) loses its trailing section's `floor_end` to
whatever the next dungeon's first `f:` line happens to specify.

In production, vendor `data/snews.txt` happens to dodge this neatly:
the corrupted `floor_end` values are still close enough to the
dungeon's actual floor count that the in-game floor-range probe
(`FUN_004364bc`, looking for "first section whose floor_start ≤
current_floor+1 ≤ floor_end") still matches sensibly.  Dungeon 1's
floor 5 — the boss floor — falls past the corrupted `(1, 4)` range
and so silently gets no news-event roll, which is plausibly intended
behavior anyway.

The port reproduces the bug faithfully (writes to old `local_c`
before advancing) so that any consumer that's been written against
the corrupted layout keeps working.  The fix-instead-of-reproduce
choice would have been to reset `local_c` on dungeon match, but the
engine ships with this and we can't tell whether downstream code
depends on the shift.

> 📍 `src/tables_snews.c`, `docs/formats/data-text.md` "snews.txt"
> section, and the `test_tables_snews_dungeon_transition_corrupts_prev`
> unit test which is dedicated entirely to pinning this behavior down.

## 21. `enemy.txt` lines without a matching record pop a MessageBoxA

The shipping `bmpdata.bin` overlay version of `data/enemy.txt`
(3589 bytes, replacing the lnkdatas 2801-byte version) contains 67
data lines, but only 54 unique pre-baked records can absorb them.
Lines like `ダークゴーレム`, `オーム`, `カニ`, `黒カニ`, `クラゲ`,
`赤クラゲ`, `魔王の手`, plus three `ダークゴーレム…` variants, have
no record name that prefix-matches them — and the engine's
fall-through path at L920 of `FUN_00475270` is:

```c
if (pcVar16 == (char *)0xffffffff) {
  MessageBoxA((HWND)0x0, line, &DAT_005cae4c, 0);  // "no_match"
}
```

So a factory boot of the original `recettear.exe` should pop ~9
modal dialogs in sequence before the title screen appears — each
listing one of these orphaned enemy names. Either nobody noticed
in 2007, the QA pass missed it, or there's a path that suppresses
MessageBoxA in release builds that we haven't tracked down yet.

The matching path also has a separate "aliased writes" issue: the
five lines starting with `"アルマ"` (`アルマ`, `アルマゴーレム`,
`アルマゴーレムコア`, `アルマゴーレム右手`, `アルマゴーレム左手`) all
prefix-match the single `"アルマ"` record (slot 41). Last line
wins, so record 41 ends up with the stats of `アルマゴーレム左手`
(HP=100, AT=20), not `アルマ` itself. The intermediate writes are
silently lost.

Port silently skips unmatched lines and accepts the last-write-wins
behavior for aliased lines — both because the engine's MessageBoxA
isn't useful here and because reproducing the alias is byte-faithful
to what actually runs.

> 📍 `src/tables_enemy.c` (port), `docs/formats/data-text.md`
> "enemy.txt" section (alias and miss tables).

## 22. The tutorial loader's parser stride is 1/4 of what the consumer reads

`FUN_00475270` loads three tutorial scripts (`data/tuto1.txt`,
`tuto2.txt`, `tuto3.txt`) into a shared 296-byte-per-record array at
`&DAT_005d1fc8`. The parser writes each record at slot

```c
local_8 + local_c * 0x32      // record index + (file index × 50)
```

— i.e. it reserves a **50-record stride per file**. The gameplay-
side dispatcher (`FUN_00461c00`, L59759 of `all.c`) reads with stride
**200**:

```c
iVar9 = (DAT_005c6bb0 * 200 + _DAT_0730b604) * 0x128;
```

The two strides disagree by a factor of 4. So tuto2/tuto3 data
*never lands at the address the consumer reads from*; the consumer
sees BSS-zero records (everything looks like a `CHR0` opcode with id
0 and empty text).

That alone would be merely awkward. The kicker is that **every
shipping tuto file overflows the parser's 50-record cap**:

| file       | records | spills to slots |
|------------|---------|-----------------|
| tuto1.txt  | 135     | overwrites tuto2's parser region (50..99) and most of tuto3's (100..134) |
| tuto2.txt  | 90      | overwrites tuto3's parser region (100..139)             |
| tuto3.txt  | 60      | walks 10 slots past the 150-slot array entirely         |

So after all three files load, the parser has filled slots 0..159
with a *cascade* of overwrites — and the consumer, indexing with the
×200 stride, has to choose between reading file 0's region (slots
0..199, partially populated) or files 1/2 (slots 200..599, entirely
empty).

Three of the four call sites for the file-index setter
`FUN_00461bf6` push the immediate `2` (the new
`tools/analyze/pe.py callers` subcommand confirms this), so the
consumer is routinely reading from a never-written region. How the
tutorial visibly plays at all is not yet answered — possibly the
dispatcher short-circuits on the BSS-zero `opcode == 0 && text[0] ==
0` combination, or there's a parallel state machine we haven't
traced yet.

Whichever it is, our port faithfully reproduces the parser side:
50-stride writes, overflow into adjacent regions, and a final
sentinel that lands wherever it lands. The 600-slot array is sized
generously so the writes are well-defined.

> 📍 `src/tables_tuto.c` (port), `docs/formats/data-text.md`
> "tuto1/2/3.txt" section, `docs/decompiled/by-address/475270.c`
> L2898..L3123 (parser), L59759 of `all.c` (consumer).

## 23. The Master's Plate recipe trips an unbounded ':' scan

`data/gousei.txt` has one line that's missing the trailing `:` that
every other recipe carries:

```
1215:Master's Plate:Barrier Plate#1:Plate of Grief#1:Wind Emblem#1
                                                                  ^
                                              (every other line: ':')
```

`FUN_00475270`'s gousei parser handles `#N` as "atoi the count, then
advance forward until you find a ':'". The advance is implemented as

```c
do { pcVar16 = pcVar16 + 1; } while (*pcVar16 != ':');
```

— **unbounded**. On this one line the loop walks past `\r\n`, past
the line buffer's NUL terminator (the engine pre-padded it with four
extra zero bytes after the file content), and into whatever comes
next in process memory. It scans until it stumbles into some byte
that happens to be `:` — which it always will, eventually.

The record IS still committed (the `DAT_09642bf0++` happens after the
inner field walker exits). All four ingredients of Master's Plate
get their correct IDs and counts. Only the parser's cursor is
briefly off in the weeds; the outer loop's `pcVar16` gets reset on
the next iteration by `pcVar16 = local_14`, which was last anchored
to the runaway `:`. Whatever bytes lived between line-end and the
runaway colon get fed to the next field accumulator — but since the
outer line-collect loop reads `data[pos]` from the buffer base (not
the inner `pcVar16`), the next line's processing recovers cleanly.

The port detects EOL/EOF inside the `:` hunt and finalises the
last ingredient there: resolves the accumulated name, writes the ID,
breaks out of the inner loop. End-state of the record is identical
to the engine's; we just skip the undefined-behaviour scan.

That this misformed line ships in the retail game is itself a small
delight — it survived QA, it survived the EN localization pass that
rewrote every name, and it survived all the patches. The vendor file
literally cannot be regenerated from the recipe schema as the
designers presumably wrote it without removing that trailing colon
somewhere; whether by accident or by a one-time hand-edit, we'll
never know.

> 📍 `src/tables_gousei.c` (port), `docs/formats/data-text.md`
> "gousei.txt" section (`#count` at EOL with no trailing ':'),
> `docs/decompiled/by-address/475270.c` L2447..L2477.

## 24. `data/kyaku.txt` matches `嫌い:` then immediately discards it

The customer table's per-line parser has 14 field-key blocks; one of
them matches `嫌い:` (dislike: 5 SJIS bytes plus `:`) — and does
absolutely nothing with the body. The do-while just `break`s on a
mismatch and falls through to the next key on a hit:

```c
LAB_004771ee:
  iVar1 = 0;
  do {
    if (local_27c[iVar1 + 0x20] != (&DAT_005caddc)[iVar1]) break;
    iVar1 = iVar1 + 1;
  } while (iVar1 != 5);
  /* (no field write here — fall straight through to 活動時間:) */
```

No atoi, no string copy, no flag set, no MessageBoxA. The empty body
is almost certainly the residue of a dialled-back feature: design
note `kyaku.txt` opens with a long Japanese comment describing all
the per-customer parameters and explicitly calls out `嫌いなもの`
("dislikes"): *"地雷、コレを薦めるとお得意様度大幅ダウン、虫が苦手
な人に虫系アイテム薦めたりするとヤバイ"* — a landmine; recommending
a disliked item to a customer tanks their loyalty. The data file
still ships dozens of `嫌い:` lines (all with empty bodies in the
shipping version, but the field's existence implies it once held
content) — but the engine ignores them, so a disliked-item recommend
never gets penalised.

Cost: 5 char-compares per non-comment line. We faithfully reproduce
the orphan match in the port as `apply_dislikes_noop` so the dispatch
chain stays semantically identical.

> 📍 `src/tables_kyaku.c` (port — `apply_dislikes_noop`),
> `docs/formats/data-text.md` "kyaku.txt" section,
> `docs/decompiled/by-address/475270.c` L713..L718.

## 25. `kyaku.txt` header writes singular's NUL into the wrong slot when '#' is present

The `NNN:Singular#Plural` header parse uses two cursors:
- `iVar6` — write position into both singular[] and joint[], resets
  to 0 at `#`.
- `iVar17` — total chars consumed from the line tail (does NOT reset
  at `#`).

When the loop detects an EOL on the next byte, it writes a NUL
terminator — but at `puVar14[iVar17 + 5] = 0`, which is
`singular[iVar17 + 1]`, NOT `singular[iVar6 + 1]`. For lines without
`#` the two counters are equal and the NUL lands one past the
singular's last content byte (correct). For lines with `#`, iVar17 has
been ticking past the `#` while iVar6 reset to 0 — so the NUL lands
several bytes past singular's actual end, sometimes well into joint
territory.

The bug doesn't manifest because the record was memset'd to 0 at boot
(it's BSS-resident), so the bytes between the singular's content end
and the misplaced NUL are already 0; the misplaced NUL is just
overwriting a NUL that was already there. If the engine ever parsed
kyaku.txt twice (it doesn't), or if it were called on non-zero
memory, the singular field would be properly NUL-terminated only by
luck.

For `013:Woman#Women\r\n` specifically: iVar17 = 10 at EOL detect, so
the NUL lands at singular[11]. Singular content ends at singular[4]
('n' of "Woman"); singular[5..10] are already 0 from BSS, and
singular[11] gets re-NUL'd. Net: harmless.

> 📍 `src/tables_kyaku.c` (port — `parse_header` mirrors the
> off-by-five with a bound check), `docs/formats/data-text.md`
> "kyaku.txt" section ("Singular NUL at off-by-five"),
> `docs/decompiled/by-address/475270.c` L545..L548.

## 26. `event.txt`'s weekday-tag mismatch advances 1 byte, not 2

The in-town vignette table at `data/event.txt` carries a weekday-of-
day tag block per line, e.g. `朝昼　　` for "morning or noon". The
parser scans this block 2 bytes at a time, comparing the current
cursor against the 4 known 2-byte SJIS tokens 朝/昼/夕/夜 in turn.
On a successful match it advances the cursor by 2. On a 4-way
mismatch — the obvious thing to do is advance by 2 (one SJIS char
worth) and try again. The engine advances by **1**.

Concretely (from `docs/decompiled/by-address/475270.c` L2076..L2175):

```c
LAB_004786bc:
  // try 朝 (offsets piVar4 then local_10/local_28/local_1c each step)
  // ... all four matches use a 2-byte memcmp, but on miss:
LAB_004787a4:
  pcVar18 = pcVar18 + 1;     // cursor += 1, not 2
  iVar6 = -1;
  local_10 = (int *)((int)local_10 + -1);
  local_28 = (int *)((int)local_28 + -1);
  local_1c = (int *)((int)local_1c + -1);
  local_14 = (char *)((int)local_14 + -1);
  goto LAB_004786f9;
```

The four `local_NN` offsets are pcVar18-relative pointers to the
four token literals; the parallel `-1` decrement keeps them
pointing at the right literals as the cursor advances 1 byte at a
time.

The consequence: an unrecognised 2-byte SJIS char (e.g. the full-
width space `0x81 0x40` that vendor lines use for padding) gets
scanned **twice** — once starting at its first byte (`0x81`), once
starting at its second (`0x40`). If, hypothetically, the second
byte of one ignorable char and the first byte of the next happened
to spell `0x92 0xa9` (朝), the parser would record a false match.

In vendor data it's dormant: the unrecognised tokens are full-width
spaces, and `0x40` followed by anything doesn't spell any of the
4 known tokens. But it's a real bug in the same family as the
"unbounded `:` hunt" in `gousei.txt` (quirk #23) — a parser
optimised for the happy-path with no robustness against ill-formed
inputs.

The port reproduces it byte-for-byte: a 4-way mismatch advances
the cursor by 1; a successful match advances by 2.

> 📍 `src/tables_event.c` (port — `parse_time_tags`),
> `docs/formats/data-text.md` "event.txt" section,
> `docs/decompiled/by-address/475270.c` L2076..L2175.

---

## 27. `news.txt`'s name buffer can overflow into `rate`

The news.txt data-row parser writes the `<name>,` field into the
record's name buffer at +0x80. The structural name field is **16
bytes** (the next field, `rate`, sits at +0x90), but the parser caps
its char-copy loop at **20** iterations and writes a NUL terminator
at `name[name_len]` afterwards:

```c
local_24 = 0;
do {
  if (*pcVar16 == ',') break;
  (&DAT_056e0e80)[(int)local_24 + iVar17] = *pcVar16;
  local_24++;
  pcVar16++;
} while (local_24 != 0x14);
(&DAT_056e0e80)[(int)local_24 + iVar17] = 0;
```

So a 16-char name would put its NUL terminator on byte 0 of `rate`,
and a 20-char name would put both 4 content bytes AND its NUL well
into `price_lo` / `category`. The engine then overwrites `rate` /
`price_lo` / `price_hi` from the subsequent CSV fields anyway, so
the overflow self-cleans — but only because vendor data is
well-formed.

Vendor news.txt names are all ≤ 12 bytes (`アクセサリー` is the
longest at 12). The overflow is dormant but real.

The port reproduces it via a `uint8_t *` write into the record,
keeping the engine's stride exact:

```c
uint8_t *rec_bytes = (uint8_t *)rec;
for (i = 0; i < NEWS_NAME_PARSE_CAP && *p != ','; i++, p++) {
    rec_bytes[0x80 + i] = (uint8_t)*p;
}
rec_bytes[0x80 + i] = 0;
```

> 📍 `src/tables_news.c` (port),
> `docs/decompiled/by-address/475270.c` L1625..L1640.

---

## 28. `news.txt` name lookup is "prefix-by-name-length", not exact-match

All three name lookups in news.txt — special-sentinel check, SJIS
attribute-tag match (`FUN_0049e9a7`), and the category / item
fallback resolvers — call `FUN_00479f4d(name, candidate, name_len)`,
which is `memcmp(name, candidate, name_len) == 0`. **`name_len` is
the byte count of the news.txt field, not the candidate.**

This means a short name is a prefix-of-candidate match. A
hypothetical news.txt row with `武,1,3-1,...` (just the first SJIS
char of `武器`) would match:

- The `武器` attr tag (since `武` is 2 bytes = first 2 bytes of `武器`).
- The `武器屋` item category (if it existed) and any item whose
  singular starts with `武`.

Vendor names always exactly equal their candidate (e.g. `武器,…`,
`Daggers,…`, `Candy,…`), so the prefix vs exact distinction is
unobservable. But it's a real precedence trap if you ever ship a
short-prefix name that's a substring of multiple candidates — the
first match in lookup order (attr → category → item) wins, even if
a longer match exists deeper in the chain.

The port mirrors the engine: each resolver does `memcmp(name,
candidate, name_len) == 0 && strlen(candidate) >= name_len` (the
`>=` guard avoids reading past candidate's NUL the way the engine
does, while preserving the match semantics — over-read would
typically mismatch anyway).

> 📍 `src/tables_news.c` (port — `resolve_name` + `prefix_match`),
> `src/tables.c` (port — `news_resolve_category` / `news_resolve_item`),
> `docs/decompiled/by-address/475270.c` L1616, L1640, L1648, L1661.

---

## 29. `news.txt` "-" data rows leave per-row fields at BSS-zero

The `-,-,body` data-row shape ("generic news; no item effect") skips
the entire name-resolution and price-parsing pipeline. Only two
fields are explicitly written by the "-" branch:

```c
(&DAT_056e0ea0)[iVar1 * 0x2f] = 0xffffff9c;  // +0xa0 category = -100
(&DAT_056e0e9c)[iVar1 * 0x2f] = 0;           // +0x9c attr_mask = 0
```

Then body text + `period_start` / `period_end`. Notably **not** set:

- `+0xa8 target_group` — the sticky "対象者:" value (set only by the
  non-"-" branch at LAB_00478d0a, via `local_14`).
- `+0xa4 item_id` — defaults to BSS-zero, NOT the -1 sentinel that
  the non-"-" branch writes at the top.
- `+0xac days_lo` / `+0xb0 days_hi` — same as above: BSS-zero, NOT
  -1.

Consumers that distinguish "no item" (`item_id == -1`) from "item id
0" therefore see "-" rows as referencing item 0. Vendor data avoids
the confusion because:

- Vendor "-" rows always appear under `対象者,0` headers, so the
  unset `target_group` happens to equal the most-recent `local_14`.
- Vendor "-" rows are emitted only for purely flavour-text news that
  never references an item.

The port reproduces all four omissions: the "-" branch skips the
`target_group` / `item_id` / `days_lo` / `days_hi` writes, leaving
them at the `memset(out, 0, ...)` zero state.

> 📍 `src/tables_news.c` (port — `if (line[0] == '-')` branch),
> `docs/decompiled/by-address/475270.c` L1759..L1795.

---

## 30. `news.txt` body text retains the trailing `\r` of CRLF lines

The line-collect loop in the news.txt parser stores the terminating
`\r` or `\n` in the line buffer along with the line's content:

```c
do {
  // ...
  local_27c[iVar6 + 0x20] = cVar11;   // store char, including '\r' / '\n'
  if (cVar11 == '\r' || cVar11 == '\n') goto LAB_00478975;
  // ...
} while (cVar11 != '\0');
LAB_00478975:
  local_27c[iVar1 + 0x21] = '\0';     // NUL right after the terminator
```

The body-copy loop, by contrast, only stops at `\0` or `\n` — **not**
`\r`:

```c
do {
  cVar11 = *pcVar18;
  if (cVar11 == '\0' || cVar11 == '\n') break;
  (&DAT_056e0e00)[iVar6 + iVar17] = cVar11;
  iVar6++;
  pcVar18++;
} while (iVar6 != 0x80);
```

For a CRLF-terminated line, the line buffer ends with `…<text>\r\0`,
and the body-copy reads the `\r` as content. So every body in a
CRLF-source vendor file ends with a trailing `\r` byte before its
NUL.

This is faithful behaviour, not a bug per se — the consumer's
rendering code happily ignores the trailing `\r` (it's a no-op in
both the engine's font renderer and `MessageBoxA`). But the bytes
ARE there, and a byte-identical port must include them.

The port copies up to `NEWS_BODY_LEN - 1` bytes and stops on
`\0` or `\n`, preserving any embedded `\r`.

> 📍 `src/tables_news.c` (port — `copy_body`),
> `docs/decompiled/by-address/475270.c` L1606, L1719..L1726, L1779..L1786.

---

## 31. `enemylist.txt` reserves 10 dungeon slots, only 6 are keyed

The init loop at L2592..L2607 walks the engine's 10×60 floor-section
grid (at `&DAT_0053f8e8`, 451200 bytes total) and stamps every section
with the `floor_lo = -1` / `enemy_id[k] = -1` sentinel. That's 10
dungeons' worth of storage scrubbed at every boot.

But the parser's dungeon-key chain at L2690..L2702 only matches **6**
SJIS keys:

```text
ダンジョン１ → local_20 = 0
ダンジョン２ → local_20 = 1
ダンジョン３ → local_20 = 2
ダンジョン４ → local_20 = 3
ダンジョン５ → local_20 = 4
ダンジョン６ → local_20 = 5
```

There is no SJIS string for `ダンジョン７..0` in `.data`. `local_20`
can ONLY take values 0..5. The trailing 4 dungeon slots (indices 6..9)
are pre-initialised storage with no possible writer.

Either the developer planned 10 dungeons and shipped 6 (Pensee shipped
the same 6 in DLC) or they over-allocated and never trimmed the init.
Vendor data uses dungeons 1..6 (Slime Hutch / Jade Way / Amber Garden
/ Crystal Nightmare / Tempest Plain / Pygmy Hugs), so it's the 6 that
made the release.

> 📍 `src/tables_enemylist.c`, `docs/decompiled/by-address/475270.c`
> L2592..L2607, L2690..L2702.

---

## 32. `wisp10:` lands on `:` and silent-drops

The init loop at L2608..L2612 reserves **10** dwords at `&DAT_073d8630`
for the wisp drop table:

```c
puVar12 = &DAT_073d8630;
for (iVar1 = 10; iVar1 != 0; iVar1 = iVar1 + -1) {
  *puVar12 = 0xffffffff;
  puVar12 = puVar12 + 1;
}
```

Storage for slots 0..9. So far so good. The parse path is:

```c
iVar1 = FUN_00479f4d(line + 0x20, "wisp", 4);  // prefix match "wisp"
if (iVar1 != 0) {
  iVar1 = FUN_00503d03(local_27c + 0x24);  // atoi(line + 4)
  local_24 = (int *)(iVar1 + -1);           // slot index = N - 1
  // copy item name from line + 0x26 (= line[6]):
  iVar1 = 0;
  do {
    cVar11 = local_27c[iVar1 + 0x26];
    if (cVar11 == '\r' || cVar11 == '\n' || cVar11 == ':' ||
        cVar11 == '#' || cVar11 == ';') break;
    local_27c[iVar1] = cVar11;
    iVar1++;
  } while (iVar1 != 0x100);
```

The item-name copy starts at **`line[6]`**. That's:

- `wisp1:` → byte 6 is `\r` (terminator) — but byte 5 was `:`, so
  actually `wisp1:item-text` has byte 6 = `i` of `item-text`. ✓
- `wisp9:item-text` → byte 6 = `i`. ✓
- `wisp10:item-text` → byte 5 is `0`, byte 6 is **`:`** — the name
  copy terminates immediately. iVar1 = 0 → the `if (0 < iVar1)` gate
  at L2648 short-circuits → the lookup never runs.

So `wispN:` only works for N ∈ {1..9}. A `wisp10:` line would
correctly compute slot index 9, but never actually populate it. The
slot 9 storage is therefore dead storage — the engine reserves it
but can't write to it.

Dormant in vendor data — only `wisp1:..wisp6:` ship.

> 📍 `src/tables_enemylist.c`, `docs/decompiled/by-address/475270.c`
> L2608..L2612 (init), L2634..L2688 (parse).

---

## 33. `enemylist.txt` slot-30 terminator can clobber slot-0's drops

Each floor-section is 752 bytes, laid out as:

```
+0x000  floor_lo (int32)
+0x004  floor_hi (int32)
+0x008  enemies[0..30]  — 31 × 12 bytes  (slot k: enemy_id, variant, count)
+0x17c  drops[0..30]    — 31 × 12 bytes  (slot k: item_id[0..2])
```

Engine init writes `enemy_id = -1` to all 31 slots. The per-line
parser, after writing slot `local_18`, advances `local_18 += 1` and
writes:

```c
piVar4[(int)pvVar2 * 3 + 2] = -1;  // terminator at slot local_18's enemy_id
```

Where `pvVar2 = local_18 + 1` (i.e. the OLD value plus one — but
`local_18 = pvVar2` ran just before this, so `pvVar2` is now equal
to the new `local_18`). At L2842, an overflow check fires when
`local_3c > 30`:

```c
if (0x1e < (int)local_3c) {
  FUN_0048a348(&DAT_005cb374, local_3c);  // MessageBoxA "敵リスト登録オーバー"
}
piVar4[(int)pvVar2 * 3 + 2] = -1;  // terminator STILL fires
```

The overflow MessageBoxA pops up, but the terminator write **still
happens**. For `local_18 == 30` (i.e. just wrote into the 31st slot
— the absolute last enemy_id storage), the terminator targets slot
31's enemy_id field. But slot 31 doesn't exist — its address is the
**first byte of `drops[0].item_id[0]`** (offset 0x17c).

So a 31st enemy on a single `f:` block would silently overwrite the
first drop ID of the first enemy on that block. The dropped item
would change. Dormant in vendor data (no `f:` block has more than
about a dozen enemies — far from the cap).

The port logs the overflow to stderr and skips the line rather than
performing the writes, since the player-visible misbehaviour
(wrong drops) is the kind of subtle thing the game would never
notice in QA.

> 📍 `src/tables_enemylist.c` (port — `write_terminator`),
> `docs/decompiled/by-address/475270.c` L2840..L2845.

---

## 34. `stage.idx` unknown ID silently aliases to `1-16`

The `stage:X-Y` header dispatcher at L93..L240 is a 21-entry chain
that maps each known ID literal to a small-integer dungeon code
(`uVar5`):

```c
uVar5 = 0x14;             // chain-default — pre-loaded BEFORE "0-1" probe
// 21 sequential prefix-compares, each overwriting uVar5 on match:
//   "0-1" → 0, "0-2" → 1, ..., "1-15" → 0x13, "1-16" → 0x14
```

The default `0x14` (= 20) is the same value the last entry (`1-16`)
writes on match. So a `stage:foo` header for any ID the table doesn't
recognise opens a record with `dungeon_id = 0x14`, indistinguishable
from a legitimate `stage:1-16`. There's no diagnostic.

Dormant in vendor data: every `stage:` header in the shipping
`idx/stage.idx` matches a known ID. But if someone hand-edited the
file with a typo (e.g. `stage:1-7G`), the engine would silently
duplicate the `1-16` record's purpose with the typo'd record's
content — or, if `1-16` was defined too, both definitions overwrite
the *same* dungeon code over consecutive header opens (since the
parser allocates a new record per `stage:` header regardless of
dungeon code).

The port reproduces the fallback exactly; the boot trace logs total
`stages=N` without distinguishing matched vs fallback IDs.

> 📍 `src/tables_stage.c:dispatch_stage_id`,
> `docs/decompiled/by-address/475270.c` L93..L240.

---

## 35. `moonpos:` shares X/Y/Z with `sunpos:`/`sunset:` but not the mode flag

A stage record has exactly **one** trio of `sun_pos[3]` floats at
+0x1a7c — and three different keys write to those same three floats:

- `sunpos:X:Y:Z` → writes coords, sets `sunpos_mode = 1`.
- `sunset:X:Y:Z` → writes coords, sets `sunpos_mode = 2`.
- `moonpos:X:Y:Z` → writes coords, sets `moonpos_set = 1` (at
  +0x1a8c, a distinct flag), **does NOT touch** `sunpos_mode`.

So a stage with both `sunpos:` and `moonpos:` lines ends up with
the moonpos coords (whichever fired last) and `sunpos_mode = 1`
(from the earlier `sunpos:`) — the engine then renders the sun at
the moon's coordinates. The intent was almost certainly that
moonpos is a per-stage moon location for night scenes, but the
storage was crammed into the existing sun fields rather than given
its own. Vendor never defines `moonpos:` so this is dormant.

> 📍 `src/tables_stage.c:dispatch_field_line` (`moonpos:` block),
> `docs/decompiled/by-address/475270.c` L3838..L3865.

---

## 36. `sunset:off` is broken — the engine checks for `"sunpos:off"` instead

The `sunpos:` and `sunset:` parsers both have a short-circuit for an
`off` sentinel that bypasses the numeric parse and writes the mode
flag directly (mode=0 for sunpos:off; mode=0 was likely intended for
sunset:off too).

The implementation, at L3811..L3814 inside the `sunset:` branch:

```c
iVar1 = 0;
while (local_47c[iVar1] == s_sunpos_off_005cab80[iVar1]) {
    iVar1 = iVar1 + 1;
    if (iVar1 == 10) goto code_r0x00476682;  // mode = 0
}
```

`s_sunpos_off_005cab80` is the *literal string `"sunpos:off"`*, NOT
`"sunset:off"`. The binary has two interned copies of `"sunpos:off"`
(at `0x005cab4c` and `0x005cab80`); there is no `"sunset:off"` string
anywhere. So the comparison against `local_47c` (which holds the
line bytes — beginning with `"sunset:"` for any line that reached
this branch) fails at byte 3 (`s` vs `p`), the while loop exits
immediately, and execution falls through to the numeric path.

The numeric path then calls `atof("off")` → 0.0f for the first
component, then scans forward for the next `:` separator — which it
never finds in `"sunset:off\0"`. In the engine, the scan walks past
the buffer terminator into adjacent stack bytes from the previous
line; the subsequent atofs read whatever happens to be there. In
our port, the scan respects the line NUL and leaves Y/Z at their
defaults.

End state: `sunset:off` produces `sunpos_mode = 2` (sunset, NOT off),
`sun_pos[0] = 0`, and whatever was in `sun_pos[1..2]` before. The
copy-paste hazard probably went undetected because no vendor file
uses `sunset:off` — every `sunset:` line we'd expect to see uses
numeric coords or simply isn't written at all.

> 📍 `src/tables_stage.c:dispatch_field_line` (`sunset:` block),
> `docs/decompiled/by-address/475270.c` L3811..L3814.

---

## 37. `recet.ini` `bgnodisp` key is dead text — overwritten by `easydisp`

The shipping `recet.ini` includes `bgnodisp=0` under `[setup]`, but
`FUN_0047a474` (the loader) doesn't read it. Instead, after the read
loop finishes, the engine unconditionally does:

```c
DAT_0438b18c = 0;                   // earlier in the loop
...                                 // (other keys read in between)
DAT_0438b18c = DAT_0438b19c;        // post-loop: bgnodisp = easydisp
```

So whatever the player puts in `recet.ini` for `bgnodisp` is silently
ignored — only `easydisp` matters. Port mirrors this: `struct
recet_ini` has a `bgnodisp` field, but `recet_ini_parse` overwrites
it from `easydisp` after the iteration. A direct `bgnodisp=42` in
the test fixture is dropped on the floor (see
`test_recet_ini_bgnodisp_mirrors_easydisp`).

The probable history: an earlier engine revision exposed both as
independent switches; the simplification kept `easydisp` and folded
`bgnodisp` to mirror it, but nobody removed the (now-redundant) key
from the shipped ini.

> 📍 `src/recet_ini.c:recet_ini_parse` (post-loop fixup),
> `docs/decompiled/by-address/47a474.c` L77714 + L77722.

---

## 38. `recet.ini` `[debug] camfree` is read twice (same key, same section)

```c
_DAT_0438cd5c = GetPrivateProfileIntA(s_debug_005cba2c, s_camfree_005cba24, 0, ...);
_DAT_0438cd5c = GetPrivateProfileIntA(s_debug_005cba3c, s_camfree_005cba34, 0, ...);
```

Both calls use distinct string addresses but those addresses point
to byte-identical content (`"debug"` and `"camfree"`). The two calls
write to the same target global. The second value sticks, but it's
always the same value since both calls hit the same ini entry. Just
dead duplicate code from a refactor — the port reads `camfree`
once.

> 📍 `src/recet_ini.c:g_field_rows` (single `camfree` row),
> `docs/decompiled/by-address/47a474.c` L77700-77701.

---

## 39. `recet.ini` ships three more keys the engine never reads anywhere

In addition to `bgnodisp` (#37, which gets overwritten), the shipping
`recet.ini` carries three keys that don't appear in *any*
`GetPrivateProfile*` call in the binary:

| key       | section  | value in vendor ini |
|-----------|----------|--------------------:|
| pfnouse   | [setup]  | 0 |
| fontmode1 | [setup]  | 0 |
| fontmode2 | [setup]  | 0 |

Greppable: only `FUN_0047a474` (read) and `FUN_0047a444`/`FUN_0047a804`
(write-back of `se`/`mu`/`winx`/`winy`) ever touch
`GetPrivateProfile`/`WritePrivateProfile` in the binary — 33 +
4 calls total — and the keys above aren't in either set. So they're
truly dead text in the ini.

Likely vestigial: earlier engine revisions probably exposed these
as toggles for the print/font subsystems (`pfnouse` parallels the
live `sfnouse`; `fontmode1`/`2` parallel the `[config] font:` key
read by `tables_config.c`). Our `recet_ini_parse` silently ignores
them, matching `GetPrivateProfileIntA`'s missing-key behaviour.

> 📍 Counterpart for the dead read is `src/recet_ini.c:visit`
> "Unknown key/section: silently ignored" branch.

---

## 40. The input poll funnels both controllers into a single player slot

The output target address in `FUN_0047b73c` is computed as
`(&DAT_073dddd0)[(local_8 / 2) * 0x2a]`, where `local_8` is the
outer-loop binding-block index (4 iterations for joysticks, plus an
inner loop that reuses `iVar3` for keyboard). `local_8 / 2` is an
*integer* divide:

| local_8 | controller | output slot |
|--------:|:-----------|:-----------:|
|       0 | controller 0 joystick bindings | 0 (`DAT_073dddd0`)   |
|       1 | controller 1 joystick bindings | 0 (`DAT_073dddd0`)   |
|       2 | (dead block 2 — see quirk #41) | `0x2a` (`DAT_073dddfa`) |
|       3 | (dead block 3 — see quirk #41) | `0x2a`               |

And in the keyboard pass, `iVar3` is the inner-loop counter that walks
the same binding-block array, but only over the first two blocks
(stop condition `psVar8 != &DAT_0438cd22`, distance from base
= 56 bytes = 2 × 28-byte blocks). The two keyboard iterations give
`iVar3 / 2 == 0` for both — same slot 0 for both controllers.

End state: regardless of which controller's bindings match (kbd or
joystick), every press OR's into the *one* `DAT_073dddd0` slot. The
`DAT_073dddfa` slot only ever receives writes from the dead blocks
(quirk #41), which read all-zero bindings and never match anything,
so it stays at BSS-zero unless some other module writes it directly
(line 78916 in `FUN_0047be92` clears both at frame end — that's the
only other writer found).

Likely a vestige of an earlier two-player splitscreen layout that
got folded down to single-player without anyone removing the second
slot. The port preserves the funnelling exactly: both populated
binding blocks OR into `g_input_state[0].buttons`.

> 📍 `src/input.c:input_poll`,
> `docs/decompiled/by-address/47b73c.c` lines 105..158.

---

## 41. The joystick poll iterates 4 binding blocks but only 2 exist

`FUN_0047a474` populates exactly 2 controller blocks at
`0x0438cce8` (pad+skill per controller, 28 bytes × 2 = 56 bytes,
ending at `0x0438cd20`). But the poll's outer loop runs `psVar8`
from `0x0438ccea` to `0x0438cd5a` in 28-byte strides — that's 4
outer iterations, reading 56 bytes *past* the end of the populated
bindings.

The trailing 56 bytes (`0x0438cd20`..`0x0438cd5a`) sit in BSS and
contain unrelated globals (`DAT_0438cce0`, ...) that the ini loader
never touches with binding values, plus a stretch of all-zero
padding. Since every binding-match condition is
`binding[k] - 1 == iVar6` with `iVar6 ≥ 0x27`, an all-zero binding
gives `-1 == 0x27` → never matches.

So the dead iterations are functionally no-ops. The port still
iterates `INPUT_BINDINGS_BLOCKS = 4` so the player-slot indexing
matches the original (and so quirk #40's slot-1 path is reachable
under instrumentation), but blocks 2..3 always hold zero.

The keyboard scan correctly walks only 2 blocks
(`psVar8 != &DAT_0438cd22`); the joystick scan didn't get the same
upper bound, hence this quirk.

> 📍 `src/input.c:input_poll` (`for b in 0..INPUT_BINDINGS_BLOCKS`),
> `docs/decompiled/by-address/47b73c.c` L26-30 and L166-167.

---

## 42. Poll-failure retry checks Acquire's return for a code Acquire never sets

The joystick-poll error path is:

```c
iVar3 = device->Poll();                     // vtable +100
if (iVar3 < 0) {
    do {
        piVar2 = *piVar4;
        iVar3 = (**(code **)(*piVar2 + 0x1c))(piVar2);    // Acquire (vtable +0x1c)
        if (DAT_005cbc24 == 0) break;
    } while (iVar3 == -0x7ff8ffe2);                       // == 0x8007001E (DIERR_NOTACQUIRED)
}
```

`-0x7ff8ffe2` is `DIERR_NOTACQUIRED` (`HRESULT_FROM_WIN32(ERROR_NOT_READY)`)
— but that's a code returned by `GetDeviceState`/`GetDeviceData`,
*not* by `Acquire`. `Acquire` returns `DI_OK`, `S_FALSE`
(already-acquired), `DIERR_INPUTLOST`, or `DIERR_OTHERAPPHASPRIO`.

So the loop's continuation condition is never true in practice — it
exits after a single Acquire attempt. The intended semantics were
probably "retry Acquire until the device is back" but the wrong
error code was used. The port mirrors the (broken) loop structure
exactly; it's effectively dead code that runs at most once.

The same code-shape appears in the keyboard path (`local_178` /
`GetDeviceData` block) where it's *correct* — `GetDeviceState` does
return DIERR_NOTACQUIRED, and that branch does fall through to
`Acquire` and re-try. So this looks like a copy-paste of the kbd
path into the joystick path without adjusting which call's error
code drives the retry.

> 📍 `src/input.c:poll_one_joystick`,
> `docs/decompiled/by-address/47b73c.c` L34-43.

---

## 43. Every joystick gets Poll'd four times per frame

The poll's outer loop is over binding blocks (4 iterations) and its
inner loop over joysticks (`g_joy_count` iterations). The
`device->Poll()` + `GetDeviceState` calls sit *inside* both loops,
so each joystick is queried four times per frame — once per binding
block. The decoded data is identical across all four calls (the
real-time delta is microseconds, far below joystick refresh
granularity), so the only effect is wasted DInput round-trips.

Port reorganises the loops to query each joystick exactly once and
then apply all four binding blocks against the cached pressed-bit
array. Bit-for-bit identical output, fewer DI calls.

> 📍 `src/input.c:input_poll` (separate poll / apply phases),
> `docs/decompiled/by-address/47b73c.c` L34-46 (poll inside both loops).

---

## 44. Button auto-repeat double-fires across each reload

The button-state ring at the top of `FUN_004536cb` keeps a 16-element
`short` array at `DAT_073dddda` — one counter per bit of the input
mask. Every frame, after deriving `pressed = ~prev & cur` and the
initial `held = cur`, the ring walks all 16 bits and decides whether
to gate each one out of `held` so it doesn't auto-fire on every frame
of a hold:

```c
for (int i = 0; i < 16; i++) {
    if (((cur ^ prev) >> i) & 1) {
        repeat[i] = 0xc;                 /* edge: arm 12-frame settle */
    } else {
        if (repeat[i] > 0xc) repeat[i] = 0xc;
        if (repeat[i] < 1) {
            repeat[i] = 4;               /* reload — no decrement, no gate */
        } else {
            repeat[i]--;
            if (repeat[i] > 0) held &= ~(1 << i);   /* gate while > 0 */
        }
    }
}
```

The `< 1 → 4` reload and the `else { decrement; if > 0 gate }` are
written as mutually exclusive branches, but they each fail to gate
the bit:

- On the frame the counter hits `1`, the decrement lands on `0`, and
  the `repeat[i] > 0` test fails — so the bit is *not* masked out.
  The bit fires.
- On the *next* frame the counter is `0` going in, the `< 1` branch
  reloads to `4`, and the entire `else { decrement; gate }` block is
  skipped. The bit fires again.

So every time the counter wraps, the bit fires twice back-to-back
before the next three frames of gating. After the initial 12-frame
settle, the steady-state auto-repeat pattern is:

```
frame:  1   2..12   13 14   15 16 17   18 19   20 21 22 …
fire?:  F   . . .   F  F    .  .  .    F  F    .  .  .  …
        ^   gated    ^^     gated     ^^     gated
        rising       reload pair       reload pair
        edge
```

That's a 5-frame period with **two** firing frames per period
(40% duty cycle), not the 1-in-5 a normal `if (counter == 0)`
construction would produce. The double-fire is almost certainly
unintentional — the author probably wanted the `< 1` reload to
also set the gate — but it doesn't visibly misbehave: cursor
moves under a held UP/DOWN look "fast" but not runaway, and the
title menu's pulse animation samples at 60 Hz so any 1-frame
quirk in the input ring washes out long before it reaches the
draw call.

Port reproduces the pattern exactly — the gate condition is left
inside the `else` branch on purpose, not lifted out. The test
`test_sim_button_ring_repeat_pulses_after_settle` walks 19 frames
and asserts the fire/fire/gate/gate/gate cadence.

> 📍 `src/sim.c:sim_button_ring_update`,
> `docs/decompiled/by-address/4536cb.c` L42-70 (the ring loop),
> `tests/test_sim.c:test_sim_button_ring_repeat_pulses_after_settle`.

---

## 45. The music selector's title BGM lookup masks -1 to 0

The sim_b music selector (`FUN_0049966a`) calls `FUN_0049a558` once per
frame to look up the title-screen track. That helper is gated:

```c
if (DAT_09643520 == 10 && DAT_09643524 == 4) {
    return title_bgm_table[language * 2];   // .rdata 0x5d1be0
}
return -1;
```

Both flags stay at zero throughout the title's bare path (`cursor_anim`
sits at 0 with `menu_folding_out=1`, no submenu open → `submenu_state`
stays at 0). So the helper returns `-1` on every frame of a fresh boot.

The caller in `FUN_0049966a` then masks that `-1` to `0`:

```c
uVar5 = FUN_0049a558();
uVar5 = -(uint)(uVar5 != 0xffffffff) & uVar5;
```

When `uVar5 == 0xffffffff`, `(uVar5 != -1)` evaluates to `0`,
`-(uint)0 == 0`, and `0 & 0xffffffff == 0`. So `-1 → 0`. The selector
then hands `0` to the swap dispatcher, which queues
`bgm/retitle2010.wma` — the title music. The lookup table is never
consulted at all in this boot path.

The fall-back-to-zero is deliberate: it guarantees the title screen
always has music even when the gate doesn't fire. The actual table at
`0x5d1be0` only gets read when the player opens a submenu (cursor
folds out fully → `cursor_anim == 10`; submenu enters state `4`).

The `language` global (`DAT_005d1bd8`) is also init'd to `-1`, so even
*if* the gate ever passed before `recet.ini` set the language, the
indexing arithmetic `lang * 8 + 0x5d1be0` would walk 8 bytes *before*
the table — landing on `language` itself + a few bytes of garbage.
Our port adds a defensive `[0, 8)` guard; the engine would happily
read the OOB value but the gate almost always blocks the call first.

> 📍 `src/music.c:title_bgm_select`,
> `docs/decompiled/by-address/49966a.c` L60-62 (caller mask),
> `docs/decompiled/by-address/49a558.c` (the gate + lookup).

---

## 46. The SE table's "channel flag" column is all-zero in vendor data — voice-stealing and SE path B are dead code

The 110-entry SE table at `&DAT_005d1584` is laid out as 8-byte rows of
`(u32 resource_id, u32 channel_flag)`. The `audio_play_se` dispatch
(`FUN_00499c63`) treats the +4 column as a routing/voice-group selector:

```c
piVar1 = &DAT_005d1588 + (int)slot * 2;     /* &row.channel_flag */
if ((&DAT_005d1588)[(int)slot * 2] != 0) {
    /* Cross-slot voice stealing: scan all 110 entries, Stop every
     * currently-playing SE whose channel_flag matches this slot's. */
    for (i = 0; i < 110; i++)
        if (table[i].channel_flag == this.channel_flag)
            performance->Stop(se_segments[i], 0, 0, 0);
}

if (*piVar1 == 0) play_on_path = path_se_a;   /* DAT_0964310c */
else              play_on_path = path_se_b;   /* DAT_09643110 */
```

Two independent fade counters at `DAT_056e5774` (path A) and
`DAT_056e577c` (path B) feed the same cos-curve we already ported
(`audio_fade_compute`). The `PlaySegmentEx` call uses
`DMUS_SEGF_QUEUE = 0x80` rather than BGM's default-flags (`0`), so
same-path SE plays queue behind the prior segment unless voice-stealing
fires.

The reader does all the right things. The problem is the *data* —
every one of the 110 `channel_flag` cells in vendor data is `0`:

```
$ python3 -c '...read SE table from .data...'
nonzero col2 entries: 0/110
flag distribution: {0: 110}
```

Net effect at runtime:

- **Voice-stealing never fires** (gated on `flag != 0`).
- **SE path B is created by `audio_init` but never used as a playback
  target** — every `PlaySegmentEx` picks path A.
- **The path-B fade counter at `DAT_056e577c` never decays** — the
  branch reading it is dead.

The dormant path could have been the engine's voice-grouping plan for,
e.g., a "footsteps" group and a "UI clicks" group sharing path A, with
"alarm sirens" preempting on path B. Someone presumably planned it,
wired the dispatch, and then never populated the second column. The
queue-flag (`DMUS_SEGF_QUEUE`) only matters when two same-path SEs
collide without voice-stealing — which the data prevents.

The port keeps a single-column resource-ID table (`audio_se_names.c`)
and routes every SE to path A. The path-B path + voice-stealing scan
will land if we ever observe a modded or alternate-release binary
where the second column is non-zero.

> 📍 `docs/decompiled/by-address/499c63.c`,
> `src/audio_se_names.h` (header comment),
> `docs/findings/audio-backend.md` ("SE resource layout").

## 47. Per-tick fade phase 1/2 produce the opposite of their intuitive labels

**Severity:** documentation footgun (not a runtime bug).

The fade-animation tail at `FUN_0049966a` LAB_00499a00 selects between
two cosine schedules based on `DAT_09643114` ("fade_phase"):

- **phase 1** (assembly at 0x499a2b): `angle_progress = progress * π/2
  / duration`. As `progress` advances 0 → duration, `cos(angle_progress)`
  goes **1.0 → 0.0**. The final centibel goes from `~slider_target`
  down to `-9600` (math-floor silence) — i.e. audible **fade-OUT**.
- **phase 2** (the `else` branch at 0x499a9e): `angle_progress =
  (duration - progress) * π/2 / duration`. `cos(angle_progress)`
  goes **0.0 → 1.0** — audible **fade-IN**.

The setters line up with this reading: `FUN_00499538(duration)` takes
an explicit duration and sets phase 1 ("start fading out, here's how
long"); `FUN_0049954c()` takes no arg and sets phase 2 (re-using the
duration the prior phase-1 left in `DAT_005d1964` — "now fade back
in over the same window").

The first version of `src/music.h` labeled the field `1 = in, 2 = out`,
guessed from the setter names alone. The math says the opposite. The
port's comment + the audio-backend doc now match the assembly.

Easy to get wrong because:

- "Fade in" and "fade out" don't have a universal canonical numeric
  ordering across engines.
- The decompilation shows the FPU multiplications hidden inside
  `__ftol()` — you only see the last cos() result in Ghidra's variable
  output, so the `cos(progress) * cos(slider)` product isn't visible
  without reading the raw assembly.

> 📍 `docs/decompiled/by-address/49966a.c` (LAB_00499a00),
> `src/audio_fade.h::audio_fade_progress_centibel`,
> `src/music.h::music_state.fade_phase`.

---

## 48. Particle 0x68's "match the n-th candidate" walks the people table from two different angles

**Severity:** decomp footgun + vestigial sentinel.

FUN_0044376a's type-0x68 spawn body scans the 128-entry people table
looking for an entry that satisfies four gates:

1. `alive == 1`         (not 0, not 2)
2. `sister_720 == 0`    (engine `piVar13[-1]` = byte +0x720)
3. `sister_724 == 0`    (engine `*piVar13`   = byte +0x724)
4. `sqrt(dx² + dz²) < 16.0` — horizontal distance from spawn owner to
   the candidate's `target` field, Y excluded

It then picks the n-th entry that passes all four, where n is
`owner.field_ea0`.  Two surprises:

**(a)** The match-counter (Ghidra `iVar8`) and the people-iteration
index (Ghidra `local_10`) are independently maintained.  `iVar8` only
increments when an entry passes the gates; `local_10` increments every
loop iteration unconditionally.  When the engine finally matches
(`owner.field_ea0 == iVar8`), it uses `people[local_10].target` as the
alt-target — i.e. the people slot of the most recently passing entry,
NOT the gate-pass count.  Easy to get wrong if you collapse them into
one counter.

**(b)** The decomp shows `if (local_10 != -NAN) {...} else break;`
between the match-check and the alt-target assignment.  Raw asm at
0x444194 reveals the real test: `cmp eax, 0xffffffff; je fallback`.
That's a sentinel for "have we ever incremented" — but `local_10`
starts at 0 and only counts up, so the -1 branch is unreachable.
Probably an unfinished optimization from an earlier version where
local_10 could be uninitialized; vestigial in shipping code.

**(c)** And the FUN_005031e4 call's argument was dropped by Ghidra
entirely (same family of "argless trig" dropouts as PHC #7).  Raw asm
at 0x444070..0x44409c shows the engine builds `dx² + dz²` on the FPU
and pushes it as a double — Y is genuinely excluded, so a candidate
sitting 99 999 units above the owner still passes the gate as long as
the X/Z plane is within 16.0.

> 📍 `docs/decompiled/by-address/44376a.c` (L291-343),
> `src/scene1_records_b_spawn.c::init_entity_68`,
> `docs/findings/scene1-table-b-allocators.md` (C8j.9a entry + Q6
> resolution).

---

## Font atlas is shipped, not regenerated (engine quirk #49)

FUN_0047c474 (the GDI atlas builder — "fontsystem ok") is a fully
functional ~1.4 KB of code that the shipped EN retail build never
actually runs.

The engine's normal text-rendering pipeline needs fontdata.bin +
fontidx.bin (a pre-rasterized SJIS glyph atlas with 5×5 edge
dilation) loaded into RAM by FUN_0047c3a5. That loader's path is:

1. `fopen("fontdata.bin", "rb")` — cwd file, used during the original
   dev cycle so iterating on font generation didn't require repacking
   lnkdatas every time
2. `storage_read("fontdata.bin")` — fallback to the lnkdatas archive
3. (same for `fontidx.bin`)

The atlas regen (FUN_0047c474) only fires when `DAT_073dfd00 != 0`,
which is raised by an active `font:` line in `data/config.idx`.
**Vendor config has `/font:` commented out** — the regen never runs
under normal play. The shipped game uses the atlas baked into
lnkdatas during the original 2007 dev cycle.

That shipped atlas was built on the original dev's Japanese-locale
Windows machine. Trying to regenerate it on an English-locale Windows
host produces visually-mangled output: GDI's font substitution
resolves the engine's `face="ＭＳ Ｐゴシック" SJIS +
lfCharSet=SHIFTJIS_CHARSET` request to a *different* MS Gothic
variant (TrueType vs. legacy vector, SHIFTJIS vs. ANSI charset), and
that variant's glyph metrics don't compose cleanly with the engine's
hardcoded `dst_h = 42 * scale`, src-rect `[1, 1, 41, 41]` draw_text
math. Text renders as unrecognizable vertical stripes.

We tried hard to reproduce the dev's exact GDI environment.
`SetThreadLocale(en-US)`, `setlocale`, `SetThreadUILanguage`, ASCII
vs SJIS face names, ANSI vs SHIFTJIS_CHARSET, OUT_TT_ONLY_PRECIS vs
OUT_DEFAULT_PRECIS — none of them got GDI to give us a "right" font
variant. Retail's `recettear.unpacked.exe` running on the same EN
machine *also* generates an atlas that visually works (MS Gothic +
tmCharSet=0, non-TrueType, all-tofu kanji), but byte-different from
the shipped lnkdatas atlas and from anything our process produces.
The exact resolution mechanism is opaque from outside.

**Practical takeaway:** our port skips FUN_0047c474 entirely in
main.c and lets FUN_0047c3a5's storage_read fallback
(`src/font_atlas.c::font_atlas_load`) pull the canonical shipped
atlas out of lnkdatas. The atlas-builder code stays in
`src/font_atlas.c` for fidelity + future use; runtime never calls
it. Worth revisiting if/when we port the JP version of the game —
that build may actively regen, and the JP dev's machine config might
be more reproducible.

The whole investigation is captured in `tools/diagnostics/font/` —
probes that didn't land the fix but mapped the entire problem space.
The fix in the end was a single one-line addition to
`font_atlas.c`'s loader: `slurp_storage(...)` as a third
search-chain pass mirroring the engine's own fallback.

> 📍 `docs/decompiled/by-address/47c474.c`,
> `docs/decompiled/by-address/47c3a5.c`,
> `src/font_atlas.c::font_atlas_load`,
> `src/font_atlas.c::font_atlas_build_win32` (dead code),
> `src/main.c` (font_atlas_build_win32 call removed),
> `tools/diagnostics/font/README.md`.

---

## Frame counter pauses on scene transition (Phase B)

Symptom found 2026-05-22 while landing the Phase B input-injection
harness. The retail `title-options` scenario navigates from the
title menu → settings submenu (DOWN, DOWN, A); injection works, the
engine plays all three menu SE's (slot 10, 10, 7) on the correct
frames, and the user can visually confirm the scene transitions.
But the Frida `frame_counter` reader on `DAT_073dfcfc` stops
advancing after the transition — the harness's 40 s wall-clock
ceiling expires before the counter reaches frame 39 (the "panel
fully slid in" target), even though the engine is clearly still
rendering at 60 Hz.

The Phase A `openrecet` port doesn't show this — it drives its own
sim counter directly from `src/main.c`'s replay loop and reaches
frame 39 without issue. So the divergence is purely in *which*
counter we trust on retail.

**Confirmed 2026-05-22**: `DAT_073dfcfc` is the title-scene's
**BG-scroll tick**, not a global frame counter. User confirmed
visually: the parallax clouds behind the menu freeze in lockstep with
the counter stalling once the settings panel slides in. So the
variable is read only by the title-scene render code as a phase
input, and presumably written by the same code path; once the title
scene is no longer the active dispatch target, nothing bumps it.

The previous capture-pipeline writeup (`docs/harness-roadmap.md`
§"Phase B" and the `var_frame_counter` comment in
`tools/frida/openrecet-agent.js`) called it "engine global frame
counter" — which it is *only* during the title scene. The Phase A
input-trace replay loop also happens to use a per-sim counter that
*does* tick globally, which is why the two pipelines drifted in
behavior the moment a scenario crossed a scene boundary.

Practical impact today: scenarios that want to capture frames
*after* a scene transition need a different frame source on Phase B.

**Resolved 2026-05-22** by adding `g_manual_frame_counter` to
`tools/frida/openrecet-agent.js`. The counter starts at 0 and is
bumped on every `Present` onEnter (after the capture-decision read,
so audio/input events that fired during the cycle leading to Present
N read `frame == N`). `frameNo()` now returns this manual counter
instead of `DAT_073dfcfc`; the engine address is preserved in
`ADDR.var_frame_counter` for diagnostics and state-forcing tests
only.

Counter alignment vs Phase A's per-sim counter:

- At `speed == 0` (always the case in current scenarios), the
  scheduler dispatches input_poll + one sim + render + Present per
  iteration, so manual `frame N` ≡ Phase A `sim-tick N`.
- At `speed > 0` Phase A advances multiple sim ticks per render;
  manual frame numbers would still count Presents, so a sparse trace
  authored against sim-tick frame numbers needs the per-sim counter
  on retail too. None of our scenarios run at speed > 0 yet, so
  defer that to when it's needed.

Goldens authored against the old engine counter may need re-blessing
once on each scenario the first time the manual counter lands. For
title-options this is the unlock that lets the retail golden cover
frames 39 and 60 too.

> 📍 `tools/frida/openrecet-agent.js`
> (`g_manual_frame_counter`, `frameNo()`, the Present.onEnter bump).

---

## 51. Per-pass texture filtering is hand-set per draw group (not a global)

The scene-1 mesh render doesn't pick one filter mode — each walker
group sets its own min/mag/mip filter, and they disagree.  In
`scene1_render_meshes` (FUN_00459dfd) the alpha pass sets
`MIPFILTER=NONE` just before dispatching the records/people pre-pass;
the pre-pass (`FUN_0045672a`) then overrides:

- records sections A/B (engine `FUN_00456c4f`): `MAG/MINFILTER=LINEAR`
- people billboard section C (@ 0x456a76):     `MAG/MINFILTER=POINT`

So world-space record meshes get bilinear smoothing while NPC sprite
billboards get nearest-neighbour (crisp pixels), within one frame, one
draw call apart.  This matters for the texture-filtering 1:1-retail
parity work: there is no single "correct" filter to match — faithful
output requires reproducing each group's filter state in the right
order.  Found while porting Cchr.2e (2026-05-29), objdump @ 0x456c4f /
0x456a76.

> 📍 `docs/findings/scene1-char-sprite-render.md` "Cchr.2e",
> `src/scene1_chr_prepass.c` (the two `chr_prepass_*_setup` envelopes).

**2026-05-30 extension (Cchr.2h).** The same POINT-for-character rule holds
in the *wide-frustum shop-walker* player/companion draw `FUN_004552d0`: at
`0x456055`/`0x456067` it does `SetTextureStageState(0, MAG/MINFILTER, POINT)`
just before the actor-draw loop (objdump-confirmed; `ebx=1`).  A retail
d3d-trace of a HOUSE frame at 1024×768 shows the prim-12 character leaf
draws at `(MAG,MIN,MIP)=(POINT,POINT,NONE)` while every `DrawIndexedPrimitive`
3D mesh is `(LINEAR,LINEAR,LINEAR)` — both sides identical for the meshes.
The port's `sw_pass_light` inherited the pass-top LINEAR and drew the
billboard bilinear (visibly soft vs retail's crisp pixels at high res); fixed
by mirroring the POINT set + restoring LINEAR after the loop.  Validated by a
matched-resolution port-vs-retail pixel diff (`runs/cchr2h-retail`): player
world matrix is float-identical and dx=dy=0, so the only divergence was the
sampler state.  (Methodology note: the perceived "port 3D slightly sharper"
is **not** a filter-state difference — both are trilinear `(2,2,2)` — so it's
a deeper mipmap/LOD/gamma question, separate from this fix.)

> 📍 `src/scene1_shop_walker.c` `sw_pass_light` (the POINT set/restore around
> the actor loop), retail ground truth in `runs/cchr2h-retail-d3d`.

## 52. The sprite pre-pass does two matrix multiplies it didn't need to

`FUN_0045672a` sections A and B each build a world matrix as
`Scaling × Translation` (and B adds a `RotationY`), then multiply the
result by an explicitly-constructed `Scaling(1,1,1)` — a mathematical
no-op.  Likewise the people pass computes its diffuse alpha as
`(int)((float)alpha_byte · 255.0 / 255.0)` before the real per-entry
multiply: the `·255/255` cancels exactly.  Both are almost certainly
artifacts of a shared codegen macro / inlined helper that always emits
the extra op.  The port keeps them verbatim (commented as no-ops) so
the body stays a faithful mirror; a future "cleanup" that drops them is
harmless here but is the kind of edit that silently diverges elsewhere.
Found while porting Cchr.2e (2026-05-29), objdump @ 0x456838 / 0x456b3f.

## 50. Overlay-particle type 4 walks to a fixed point off-screen and goes "thunk"

In the overlay-particle integrator (`FUN_00414929`, L12684-12734),
slots with integrator-TYPE 4 and a non-zero "extra force" field
(slot dw 18) get a body that no other type runs.  Each tick:

1. Gate: `30 + (slot_idx % 4) < age`.  Per-slot stagger 0..3 ticks
   so four type-4 particles spawned the same frame don't all start
   moving in lockstep.
2. Aim toward the fixed world point `(11.0 * factor, -9.0 * factor,
   -520.0)`.  `factor` is computed as `(age - 30) * 0.4 + 1.2`, then
   clamped at `1.2` from above.  Because the AGE gate already
   guarantees `age > 30`, the formula always produces ≥ 1.6, so the
   clamp ALWAYS fires — `factor` is structurally constant at 1.2 for
   the entire branch.  Target is therefore the fixed point
   `(13.2, -10.8, -520)`.  The post-clamp `factor == 1.2` test
   later in the body (the kill gate) is technically always true; we
   preserve the structure verbatim since the .rdata 1.2 constant
   gives bit-exact equality.
3. Apply 10% step toward the aimed velocity, decay the extra-force
   field (`*= 0.8`), and clamp speed to 1.0.
4. Once `factor == 1.2` ("arrived"): roll `rng_next_unit() < 0.5` OR
   already-past-y-target.  If either: set sentinel to -1 (kill the
   slot) AND call `FUN_0040656e`, which plays SE id `0x29d`
   ("thunk") and bumps `DAT_00648280 = 4`.

So this particle "species" is an off-screen exit point.  Spawn one
and watch it walk toward `(13.2, -10.8, -520)` over ~30 ticks, then
once-per-tick coin-flip for whether this is the frame it pops with a
thunk.

The `-520` z component is suggestive — it's the same magic Z used by
the per-frame open's Table A inner spawn when `DAT_074b2ee4 != 0`
(see PHC #17), suggesting `-520` is the engine's "way off-screen
behind the back wall" constant for this scene's projection.  But
nothing else in the function reads it back, so the particle just
heads there as a hidden vanishing destination.

We haven't identified an in-game thing this matches yet (HOUSE doesn't
appear to spawn type-4 overlay records by default), so the actual
visual is unobserved.  Best guess: a shop NPC "leaving" animation
that walks out a door and triggers a chime — the SE 0x29d slot is
in the small-loop UI/footstep range, consistent with that read.

> 📍 `docs/decompiled/all.c::FUN_00414929` L12684-12734,
> `docs/findings/scene1-per-frame-open.md` §"FUN_00414929 — per-type
> dispatch summary" step 4.

## 53. The player-controller's dash-trail angle field is a float Ghidra reads as an int

In the `0x56dab6c` trail/after-image fill (`FUN_0048b850` tail, decomp
L89906+), Ghidra types the per-record `+0x3c` field as `int` and renders
the angle accumulation as `… + (float)piVar9[-1]` — an int→float
*conversion*.  The objdump (`0x48c9d3`) is `fadd DWORD PTR [ebx-4]`: a
raw **float** add, no `fild`/conversion.  So `+0x3c` is a float and the
`(float)` cast is a decompiler artifact; porting it as `(float)(int)x`
would corrupt every trail record's angle.  The angle is then
`2·table[anim_idx] + stored` (the table value is added to itself via
`fadd st,st` before the field — *not* `table + table` from two memory
reads, though the result is identical).  Ported faithfully as
`player_ctrl_trail_orbit_pos` (Cpop.2, 2026-05-30).

A sibling trap in the same function: every `FUN_004856d7` /
`FUN_0043647f` call in `FUN_0048b850` is shown **argless** by Ghidra,
but both are `cdecl(int key)` input queries and the asm always pushes a
literal key id (the dropped-arg pattern of quirk-class
[[feedback_argless_trig_decomp]]).  Recovered from objdump 2026-05-30 —
anyone porting these blocks must use these, not the Ghidra signature:

- **`FUN_004856d7(key)`** (`0x4856d7`) — "is binding `key` currently
  **held**?"  Walks the 5-slot per-state input-binding table
  (`0x4510648 + state*0x2dfc8 + joy*0x6c`), `>>6` each entry, returns 1
  on the first match else 0.
- **`FUN_0043647f(key)`** (`0x43647f`) — "is `key` in this frame's
  **edge/event** list?"  Scans `DAT_0438b93c[0..DAT_0438b938)` (gated on
  `DAT_068dd2f8[state] > 0`), returns 1 on match.

Resolved call sites:
- **shake-damp-factor selector** (decomp L90160+, objdump `0x48c5a0`):
  `FUN_004856d7(0x96b)` (×2, push `esi=0x96b` @ `0x48c5b2`/`0x48c5f1`),
  `FUN_0043647f(0x9)` (@ `0x48c5dd`).
- **`local_8` zoom-shake target chain** (decomp L89963+, objdump
  `0x48be6d`+): `FUN_004856d7(0x968)` → `+0.02`, `FUN_004856d7(0x969)` →
  `+0.08`, `FUN_0043647f(0xb)` / `FUN_0043647f(0xc)` → `×1.3`.

(The earlier "`push 0x96b` / `push 9`" note conflated the two blocks;
the local_8 chain uses different ids.)

## 54. Mesh textures get a full mip chain; 2D UI textures do not — two different loaders

The engine has **two** texture loaders that both wrap
`D3DXCreateTextureFromFileInMemoryEx` (statically-linked d3dx8), and
they pass **different `MipLevels`**:

- **`FUN_0047193c`** (the documented `texture-loader.md` loader) — loads
  **2D UI** assets (`bmp/system.bmp`, `fps2.tga`, `nowloading.tga`,
  `savewindow.tga`, `ive_window.tga`, …) with **`MipLevels=1`** (objdump
  `0x471a68`/`0x471ab2` push `$0x1`). No mip chain — correct, since these
  draw 1:1 in screen space where only level 0 is ever sampled.
- **`FUN_00471b24`** — the **mesh/3D texture** loader (called from the
  mesh-texture-cache miss path `FUN_00472836` @ `0x472ca6`, keyed on the
  cache count `DAT_073cb108`).  It passes **`MipLevels=0`** → D3DX builds
  a **complete mip chain**, with `MipFilter=D3DX_DEFAULT` (= box filter).
  objdump `0x471ce6`: the `MipLevels` arg reads the zero register (the
  same `edi=0` the adjacent `Usage`/`Format`/`pSrcInfo`/`pPalette` NULL
  args use); the forced `0xff00ff00` green colour-key confirms the BMP
  path.

**Why it matters (the texture-filtering parity bug, 2026-05-30):** the
port's `sprite_create` created every texture with `Levels=1`, so minified
3D meshes (the back-room shelf trim, the green star book, the window
blinds) sampled the **sharp base level** while retail sampled a smoothed
mip — the port read visibly *crisper* than retail even though the sampler
filter STATE matched (both trilinear `2,2,2`; engine-quirks §51/§53).
**Fix:** `sprite_load_mipped` (used only by the mesh loader
`mesh_load_finalize_win32`) creates `Levels=0` and box-filter-generates
each level (straight 2×2 average, matching `D3DX_FILTER_BOX`); 2D UI
sprites keep `Levels=1`.  Validated by a matched-1024×768 retail pixel
diff of the static back-wall shelf trim: **OLD port differed from retail
on 14862 px, NEW port on 282 px (mean 0.00/ch)** — bit-identical where
the camera aligns.  The old `texture-loader.md` "MipLevels 1" note was
correct *for `FUN_0047193c`* but was wrongly assumed global.  (`MIPMAPLODBIAS`
is still stubbed to 0 in the port — `scene1_device_lodbias()`; if a future
state-aligned free-roam diff shows a uniform over/under-blur, that bias is
the next suspect.)

> 📍 `src/sprite.c` (`sprite_create_impl` / `box_downsample` /
> `sprite_load_mipped`), `src/mesh_load.c:mesh_load_finalize_win32`,
> `docs/findings/texture-loader.md`, `tools/pixel_diff.py`.

## 55. The new-game intro plays a whole second load — so `HOUSE_FREEROAM` fires twice on retail

The TAS anchor `HOUSE_FREEROAM` (scene-state `DAT_0438b1c0 == INGAME` &&
`!loading`) is **not** a one-shot on retail: it rises **twice** during a
fresh new-game.  Observed (retail, `--auto-z-spam --capture-at-anchor`,
2026-05-30):

| event           | retail frame | what's on screen |
|-----------------|-------------:|------------------|
| LOADING_END / HOUSE_FREEROAM (1st) | ~3041–3156 | the **bedroom intro event** (Recette's room, "ESC Key: Event Skip") |
| LOADING_START / END (2nd load)     | between     | the intro→shop transition loads its own assets |
| HOUSE_FREEROAM (2nd)               | ~4588       | the scripted **shop tutorial** (dialog boxes) |
| (dialog clears, A-spam) | ~6088 (2nd+1500) | the **top-down free-roam shop** — the playable state |

So `INGAME && !loading` becomes true the instant the *intro event* starts,
long before the playable shop.  The intro is itself a sequence of scenes
**with its own loading screen** (bedroom → load → shop), which is why the
anchor edge fires a second time.  The truly comparable "playable HOUSE"
instant is the **2nd** `HOUSE_FREEROAM` plus enough frames for the
A-spam-advanced tutorial dialog to clear (~1500 under turbo).

**Why it matters (port↔retail parity):** the **port has no intro event**
(it's unported), so on the port `HOUSE_FREEROAM` fires **once** (~frame
1533) straight into the top-down shop.  This is the canonical "anchor as a
correctness signal" case from `docs/plans/tas-framework.md`: same anchor
NAME, genuinely different sub-state, because the port is missing a scene.
Practical consequence for anchor-relative capture: a bare `HOUSE_FREEROAM`
offset lands in *different* content on each side — retail needs the 2nd
firing + a large offset to reach the shop, the port needs a small offset.
A future precise "shop free-roam" anchor (e.g. records-B `count_b>0`, which
the chr-walker only sees in actual free-roam — see
`scene1-chr-walker.md`) would name the playable instant directly on both
sides; until then, capture the **2nd** retail `HOUSE_FREEROAM`.

> 📍 `src/anchor_trace.c`, `tools/frida/openrecet-agent.js` (`anchorTick`),
> `docs/plans/tas-framework.md` (P2 retail), `runs/rh-sweep/` (the offset
> sweep that surfaced the double-fire).

**2026-05-30 PM — the port now fires `HOUSE_FREEROAM` twice too (stub).**
The "port fires once" above was the blocker for one TAS trace driving both
targets: a segtrace's second `wait HOUSE_FREEROAM` (entered on firing #1)
never resolved on the port, so the run stalled. `src/scene1_intro_events.c`
is a minimal **stub** that reproduces the *anchor shape* without rendering
the intro: armed from `scene_post_fade_init` (the new-game→INGAME flip), a
4-state frame counter waits for the first load to clear (HF #1), then raises
the worker-load gate (`worker_load_begin`) for a few frames and drops it
(`worker_load_end`), driving a second `LOADING_START/END → HOUSE_FREEROAM`
through the existing `anchor_trace` path. Live result (port,
`traces/house_walk.jsonl`): `HF#1@1570 → LS#2@1577 → HF#2@1581`. When the
real bedroom/tutorial intro subsystem ports, it replaces the stub and the
*same* traces keep working — only the visuals fill in. It's ticked at the
top of `sim_step_a` **before** the worker-busy check, so on the frame it
raises the gate the same tick pumps the loading counters (test isolation:
`reset_world()` must `scene1_intro_events_reset()` or a leaked arm short-
circuits later sim aging).

**Segtrace + listed-capture: the spurious frame-0 grab.** A related fix in
`src/main.c::render_dispatch`: capture "listed mode" was keyed only on
`g_capture_frames_count > 0 || g_anchor_captures_count > 0`. A segtrace
schedules its `{capture}` ops *lazily* (only when a segment activates), so
before the first one resolves both counts are 0 and the wall-clock sampler
(the legacy smoke-test default) sneaks in a frame-0 screenshot — which then
becomes `cap_00`, shifting every capture-index golden by one and breaking
port↔retail alignment. Fixed by also treating `g_input_segtrace_path != NULL`
as listed mode (never fall through to the time sampler under a segtrace).

> 📍 `src/scene1_intro_events.{c,h}`, `src/scene.c` (arm), `src/sim.c`
> (tick), `tests/test_anchor_trace.c` (`anchor_intro_events_double_house_freeroam`),
> `tests/scenarios/house-movement/`.

## 56. The shake-target's `DAT_056daed8 == 1` test renders as a float `1.4013e-45`

The inverse of §53.  In the `local_8` shake-target accumulation
(`FUN_0048b850`, decomp L90000), Ghidra shows the gate as
`if ((DAT_056daed8 == 1.4013e-45) && (DAT_056db07c == 0))`.  `1.4013e-45`
is the smallest positive denormal float — its bit pattern is exactly
`0x00000001`.  The objdump (`0x48bf4c`) is `cmp %edi, 0x56daed8` with
**`edi == 1`** (an *integer* compare), not an `fld`/`fcomp`: so
`DAT_056daed8` is an **int flag tested `== 1`**, and the float literal is
the decompiler typing the integer `1` through a float lens.  Port it as
`daed8 == 1` (int); writing `== 1.4013e-45f` would compile but reads as
nonsense and risks a float-equality foot-gun.

Same chip, same `edi == 1` / `ebx == 0` register convention also flips two
nearby Ghidra-rendered comparisons (`DAT_056db034 == 1`, `DAT_056db048 ==
1`) — always confirm the `cmp` operand register before trusting a decomp
`== 0` vs `== 1`.  Ported faithfully as `player_ctrl_shake_target`
(Cpop.5, 2026-05-30); the full constant set (init `0.175`, `+0.02`/`+0.08`
held boosts as **doubles**, `×1.3`, `+0.06`/`+0.03` rumble, `0.5`/`1.0`
state overrides, `0.3 − clamp01(daedc−da1dc)·0.1` proximity ease) is
objdump-verified bit-exact.

## 57. Trail-record `x/y/z` are floats stored through an `int *`, so Ghidra prints `(int)` casts

The dash-trail / after-image advance (`FUN_0048b850`, decomp L90315-90330)
walks its record array through an `int *` (`piVar9`), so Ghidra types
*every* field as `int` and renders the position writes as

```c
piVar9[-5] = (int)((local_14 + 3.0) * (float)fVar14 + DAT_056da1d8);  // x
piVar9[-3] = (int)local_14;                                            // z
```

The `(int)` casts are pure decompiler artifacts of the pointer type — the
objdump (`0x48ca06`, `0x48ca47`) is `fstp DWORD PTR [ebx-0x14]` /
`fstp DWORD PTR [ebx-0xc]`: a plain **float store**, no `cvttss2si`/`ftol`
truncation anywhere.  The fields are floats; the chr-sprite walker reads
them back as floats.  A porter who trusts the decomp and writes
`rec[X] = (int32_t)(...)` would quantise every after-image to integer world
coordinates and visibly stair-step the trail.  This is §53/§56's int↔float
confusion seen from the **store** side, driven by the *container* pointer's
type rather than a single comparison's operand width — when a struct is
walked through a mistyped pointer, audit each field's actual store width in
objdump, not the array element type.  Ported faithfully (x/y/z as `float`
via `memcpy` into the int32 record) as `player_ctrl_trail_advance`
(Cpop.6, 2026-05-30); the `+3.0` radius (`0x519438`), the spawn threshold
`600`, and the `0.7` spawn arg (`0x519748`) are all objdump-verified.

---

## 58. The `dacc0` after-image burst reads every *other* history slot, then clears itself on its last frame

`FUN_0048b850`'s `dacc0` after-image burst (decomp L90270-90298, objdump
`0x48c918-0x48c971`) has two non-obvious behaviours a faithful port has to
honour:

- **It samples every other motion-history slot, starting at slot 3.** The
  engine walks the position source with a `0x18`-byte stride and the record
  source with a `0x58`-byte stride — *exactly twice* the `0xc` / `0x2c`
  per-slot pitches of the two 40-slot history rings. So the 5 burst records
  draw from history slots **3, 5, 7, 9, 11**, not 0–4. Ghidra hides this:
  the source pointer (`puVar6 = &DAT_056da224`) addresses each slot's
  *middle* component, and the three reads `puVar6[-1]`/`puVar6[0]`/`puVar6[1]`
  pull the full slot — but in raw asm the *third* read is `fld [edx-0x14]`
  taken **after** `edx += 0x18`, which resolves to the same consecutive
  `da220`/`da224`/`da228` as the other two. Read literally it looks like a
  boundary-straddling scatter; it is a plain full-slot copy with a 2-slot
  skip. (The doubled stride is presumably a cheap motion-blur spread — wider
  temporal spacing between after-images than the dense dash trail's.)

- **The final burst frame fills the bank and then immediately zeroes it.**
  The drive counter `DAT_056daae0` gates the burst (`if (0 < daae0)`); each
  active frame materializes all 5 records (life `0x14`) **then** does
  `daae0--`, and on the frame that decrements it to 0 a clear pass walks the
  bank zeroing **only** each record's `+0x38` life field. So the last
  materialized burst is retired the same frame it was written — the sprite /
  position dwords are left intact but dead (life 0). A port that treats the
  fill and the counter-decrement as independent, or that clears the whole
  record instead of just the life dword, diverges on that final frame.

Ported faithfully as `player_ctrl_burst_materialize` (Cpop.7, 2026-05-30),
host-tested; geometry objdump-verified. The *steady-state* writer of this
same `dacc0` bank is the still-unported Cf.* `FUN_00436f97`.

## 59. The HP/SP gauges tween at a per-character speed, and the equal frame resets a counter without touching the direction

`FUN_0048b6ad` (407 B), the very first thing `FUN_0048b850` does each frame,
is the on-screen HP/SP bar follower: it eases two displayed gauges
(`DAT_056db0c4`/`DAT_056db0c8`) toward their true values
(`DAT_056db0bc` = player HP / `DAT_056db0c0` = SP) so the bar slides a few
frames after a hit instead of snapping. Two things a faithful port has to get
right:

- **The step rate is per-character, packed as two summed int16s × 0.01.** It
  reads `i16[rec+0x3c] + i16[rec+0x3e]` for HP and `i16[rec+0x40] +
  i16[rec+0x42]` for SP, multiplies each sum by `0.01` (`.rdata 0x5193a4`),
  where `rec = &DAT_04510648 + DAT_0438b7d8*0x6c + DAT_0438b1e0*0x2dfc8` (the
  active character within the active stage). Two adjacent int16 fields summed
  is an odd way to store one rate — likely a base+bonus split — but the order
  is immaterial since it's an add.

- **The two channels are asymmetric, and the equal case is a third branch.**
  The HP channel tracks a run-length counter (`DAT_056db0cc`) and a direction
  flag (`DAT_056db0d0`: 1 = rising/heal, 0 = falling/damage); the SP channel
  tracks neither. The engine's branch order is `jae` then `jbe`, so it has
  *three* outcomes, not two: below-target rises and sets dir 1, above-target
  falls and sets dir 0, and **exactly-equal resets the counter to 0 while
  leaving the direction flag at whatever it last was** (the equal branch is
  `and db0cc,0` with no write to `db0d0`). A port that folds equal into the
  ≥ branch, or that clears the direction on settle, diverges. Both channels
  clamp overshoot to the target rather than stepping past it.

Ported faithfully as `player_ctrl_gauge_track` (Cpop.8, 2026-05-30),
host-tested; objdump-verified `0x48b6ad-0x48b843`.

## 60. HOUSE free-roam walking is real, but it's gated behind two intro events and lives in `FUN_0048b850` (Cpop), not the `FUN_0048670f` state machine

**Superseded correction (2026-05-30):** an earlier draft of this quirk claimed
the HOUSE shop had *no* d-pad free-roam — concluded from a purely static decode
of `FUN_0048670f`'s `DAT_0438cc08` state machine, every branch of which routes
the d-pad to a menu cursor (`cc08==0xf`), a placement/pan pair (`cc08==0x12`),
or a scripted approach (`cc08==4`). That conclusion was **wrong**, and the way
it was wrong is the lesson: the playable controller state is **gated behind the
new-game intro**, so static reading of the *idle/scripted* branches can't see
the movement path that only runs once the player is controllable.

**Ground truth (TAS anchor-segmented drive + per-frame watch + differential
call-trace, see `docs/plans/tas-framework.md`):** on a new game the sequence is
`big load → HOUSE_FREEROAM#1 (2D fixed-picture dialogue event) → hidden load →
HOUSE_FREEROAM#2 (3D-house-background dialogue event) → controllable`. Both
events are `ESC`-skippable (ESC then `Z`/confirm), or advanced line-by-line by
spamming `Z`. **`ESC` only skips while an event is playing — once controllable,
`ESC` opens the pause menu.** Driving a deterministic trace past both events and
then holding **UP** moved the player: `DAT_056da1e0` (pz) `9.35 → 8.941`,
`DAT_056daae8`(anim id) `→ 1` (walk), facing `DAT_056dab00 → 0` — i.e. real
directional free-roam walking, in controller state `cc08==1`.

A differential call-trace (idle frame vs UP-held frame) shows the per-frame
free-roam call set is **`FUN_0048670f` → `FUN_0048b850` + `FUN_00483170` +
`FUN_0048a833` + `FUN_00432e50` + `FUN_004897c6` + `FUN_00482a71` +
`FUN_00486435`** every controllable frame (movement is *nonzero velocity*, not a
distinct function). The roles:

- **`FUN_0048b850` (Cpop, 5 KB) is the movement controller** — not "camera
  effects" as W1 assumed. It sets velocity from a facing angle:
  `DAT_056daabc = sin(db05c)*speed`, `DAT_056daac4 = cos(db05c)*speed`
  (`FUN_00503a44`/`FUN_00503994`), applies gravity `DAT_056daac0 -= 0.03`, sets
  the facing octant `DAT_056dab00` and walk anim `DAT_056daae8`. The d-pad
  reaches it through a decoded movement-intent mask `DAT_056daeac` (raw
  `dddd6` decode is upstream).
- **`FUN_00483170` (3.3 KB) is the physics integrator** — applies the velocity
  (`daabc`/`daac0`/`daac4`) to `DAT_056da1d8`/`dc`/`e0` with collision via
  `FUN_00432e50`, and the vertical/gravity term on `da1dc`.
- **`FUN_0048670f`** is the outer state machine (events, counter menus, scripted
  approach); in the controllable state (`cc08==1`) it drives `FUN_0048b850`.

So this **re-validates the original `project_next_char_controller` direction**
(continue `FUN_0048b850`/Cpop) and corrects the W1 reframing that called
`FUN_0048b850` an effects sub-controller. The HOUSE char-parity target is the
real free-roam movement path (`FUN_0048b850` + `FUN_00483170`), reachable
deterministically via the anchor-segmented TAS trace. The methodology note:
*never conclude "feature X doesn't exist" from static decode of a state that's
gated behind unported intro/dialogue — drive to the live state and diff.*

---

## 61. HOUSE free-roam walk physics: accel 0.1, speed cap 0.175, damp 0.82 — and the velocity is written *through the player-struct pointer*, so it never shows as a `DAT_056daabc =` literal

Ground truth: `runs/w3-walk-watch` (a `--watch` of 15 movement globals over a
TAS segtrace driving a new game into HOUSE free-roam and holding LEFT;
[[reference_tas_anchor_forcing]]). The per-frame trajectory is fully
determined and the model reproduces it to all measured digits:

**Per controllable frame** (`DAT_0438cc08 == 1`, `DAT_056da1bc == 0`):
1. direction held → facing angle `db05c` (world: `vx=sin`, `vz=cos`, so
   `angle = atan2(dx, dz)`; DOWN 0, UP π, RIGHT +π/2, LEFT −π/2), then
   **`daabc += sin(db05c)·0.1`, `daac4 += cos(db05c)·0.1`** (accel 0.1);
   anim id `daae8` → 1 (walk), else 0 (idle).
2. clamp `|(daabc,daac4)| ≤ 0.175` (`FUN_0048b850` @ all.c L90010; the
   `local_8` base is 0.175 with no skill/dash modifiers).
3. facing octant `dab00 = (int)((db05c + DAT_073de39c + π/8)·8/2π + 8) & 7`
   (objdump 0x48bfd2-0x48bffb; consts 8.0 / 2π / π/8; the `+8` keeps the
   ftol arg positive before `& 7`). `DAT_073de39c` (scene camera yaw) is **−π**
   for the fixed HOUSE camera — solved exactly from idle +π/2→oct 6, LEFT
   −π/2→oct 2. Then the diagonal sticky-snap (quirk-free leaf).
4. integrate `da1d8 += daabc`, `da1e0 += daac4` (`FUN_00483170`/`FUN_004830f1`;
   flat HOUSE floor — no mesh hit in this capture).
5. room-bounds clamp **`FUN_00486435`**: small-room arm (`(&DAT_04510578)[…]
   < 3`) does `pz ≤ 9.5` and, while `pz > 7.0`, `px ≥ −1.5` — the left wall the
   capture pins at `px=-1.5`. (NOT the full collision mesh; that's furniture.)
6. damp `daabc,daac4 *= 0.82` (grounded-steady branch L90177).

**The watch reads at end-of-frame (post-damp)**, so the recorded `vx` is the
*next* frame's starting velocity: steady `vx_read = -0.1435 = -0.175·0.82`, and
`px` steps by the *pre*-damp `-0.175`. The release tail confirms the 0.82 to
three digits (`-0.11767/-0.14350 = 0.820`) with no impulse once the d-pad is up.

**Why the §60 "`FUN_0048b850` sets `daabc=sin·speed`" was imprecise:** the only
sin/cos velocity *accumulate* in `FUN_0048b850` (L90074) is gated on
`da1bc ∈ [1,14]` (a stun/hop mode) and uses `0438ccbc·0.01 = 0.3`, NOT the walk.
The real walk impulse (`0.1`) is in the controllable player-struct code and
writes the velocity as `*(float*)(player + 0x904)` — so it never renders as a
`DAT_056daabc =` line in the decomp and a grep for the named global misses it.
Lesson: for player/actor state, grep the struct offset, not just the `DAT_` alias.

Ported (W3, 2026-05-31) in `scene1_player_ctrl_tick` (`src/scene1_player_ctrl.c`),
host-test-validated against this trajectory; walk-cycle *frame timing* and
furniture/mesh collision are deferred (W3b / W4).

---

That's the tour.  None of these prevent the game from running, all of
them are charming in their own way, and at least three of them
(quirks 1, 2, and 7) made us double-check the decompilation against an
external reference before believing what we were reading.

## 62. HOUSE collision is a full triangle-mesh subsystem with velocity sliding — not room-bounds clamps or furniture AABBs

W4 scoping (2026-05-31). The W3 port stops the player with a hardcoded
`FUN_00486435` room clamp (`pz≤9.5`, `px≥−1.5` when `pz>7`). Retail ground
truth shows that clamp is only a spawn-corner approximation: real HOUSE
navigation is governed by the per-level **collision mesh** queried by
`FUN_00432e50` (point→triangle ground/wall query: returns ground height +
surface normal + a hit flag from `&DAT_007ca434 + level*0x2f8020`, per-triangle
0x98-byte records with plane eqs + type codes) and resolved by `FUN_00483170`
(integrate velocity, **slide along the contacted surface**, radial furniture
push, per-actor floor clamp).

Ground truth captured with the new `{wait_until}` TAS op (§see tas-framework
P3b) — `runs/w4-collide` (multi-direction sweep) and `runs/w4-table`/`-table3`
(round-table hit), `--watch px/pz/vx/vz`:

- **Furniture/walls block the player comprehensively.** Holding UP from spawn
  (px −0.30, pz 9.35) pins at the **counter** at `pz=8.941` (velocity keeps
  pushing, position frozen). The room is large (`pz` 9.35…−7.27, `px` −0.30…
  3.10) and every wall/table stops the player.
- **The response is position-block + velocity SLIDE**, not a hard stop: walking
  RIGHT into an angled wall redirects motion along it (e.g. `vz` bleeds in over
  frames while `vx` decays — the `FUN_00483170` reflection at all.c L134-148),
  and walking LEFT into the **central round table** slides the player *around*
  its circular edge (`px 0.69→−0.67` while `pz` climbs 1.5→2.84 — a curved
  trajectory only a real mesh produces). A clean head-on approach instead pins
  flat against the table face at `px≈0.729`.
- **The port walks through all of it** (only the spawn-corner clamp exists; the
  ~3 MB/level `DAT_007ca434` collision mesh is **not loaded** in the port — the
  render mesh is a separate structure).

Consequence for the port: furniture collision does **not** reduce to a handful
of AABBs, so a hand-built per-room approximation would be unfaithful (it can't
reproduce the round-table slide). Real parity needs the subsystem: (1) a
collision-mesh loader to populate `DAT_007ca434`, (2) `FUN_00432e50` (the 2 KB
triangle query), (3) `FUN_00483170`/`FUN_004830f1` (the slide-resolve). The
canonical validation drives are `traces/house_collide.jsonl` +
`traces/house_table_collide.jsonl` (replay → match `px/pz` per-frame).

## 63. Collision mesh is parsed from the render `.x` (material-name typed); vertex pool ×0.2, records negate X into player/world space

W4.1 (2026-05-31). How the per-object collision triangle mesh (§62) is built
from disk, ground-truthed by porting the loader + self-validating extent.

- **Source = the render `.x`, no separate file.** `FUN_00472836` first tries a
  `<name>_s.x` companion, then falls back to the base `<name>.x`. The HOUSE shop
  ships **no** `_s.x`, so collision triangles are read from the same
  `xfile/shop/shop_1st.x` etc. the engine renders. The parser `FUN_00471d45`
  walks every `Mesh`, applies the frame transform, and classifies each face by
  the **material name** it references in the `MeshMaterialList` (not the frame
  name). shop_1st.x's materials are numeric (`Material__189_0`) + `nohit` +
  `xof_default`, so its faces are type **0** (generic solid) or **4** (`nohit`).
- **Type-code table** (material-name prefix → code, `FUN_00471d45` chain;
  decoded from the unpacked exe at `0x5c8364…`): `mizu`(water)=5, `gake`/`yuki_`
  =6, `toumei`/`kabe`(wall)=7, `dame`/`Plane`(floor)=2, `hit`=3, `hikari`(light)
  /`nohit`=4, `crystal`=15, `taimatu`(torch)=16, `takara`(treasure)=13,
  `taru`(barrel)=12, `shokudai`(candle)=14, `tree01`=8, `tree02`=9, `kusa01`=10,
  `kusa02`=11, default=0. **Type 4 faces are DROPPED** from the built mesh
  (`FUN_0043289b` `if (type != 4)`), so `nohit`/`hikari` never collide.
- **Coordinate space.** The vertex pool (`DAT_0432a754`, stride 0xc) holds
  frame-transformed coords **×0.2** (`FUN_00471d45` L344-346). The per-triangle
  record (`FUN_00432ac6`) then **negates X** when it stores the verts/normal, so
  records live in the engine *world* space the player position
  (`DAT_056da1d8/dc/e0`) moves in — i.e. a built record is directly comparable
  to the live player pos. Plane normal = `(C−B)×(B−A)`, `d = −n·A`, plus `|n|²`;
  AABB padded per-level (HOUSE = small: x/z ±0.5, y −0.5/+3.0; the y pad is
  asymmetric for head clearance).
- **Self-validation.** Building shop_1st.x in the port yields **1909 triangles**
  spanning world `x[−43.0,45.0] y[−0.57,20.2] z[−40.4,10.6]` — the back-wall/
  counter edge at z≈10.6 matches the retail counter (§62, pz≈8.9–9.5) and the
  floor sits at y≈0, confirming the ×0.2 + X-negation. The mesh is much larger
  than the player-reachable core (§62's px −1.5…3.1 / pz −7.3…9.35) because it
  includes unreachable outer walls/backdrop.

Port: `src/collision_mesh.{c,h}` reuses the oracle-validated `xfile` parser for
the text and replicates only the transform/scale/classify/plane-build. The
2777-byte `.x` text state machine `FUN_00471d45` is NOT re-ported. Query
(`FUN_00432e50`) + slide-resolve (`FUN_00483170`) are W4.2/W4.3.

## 64. HOUSE walls are *implicit* — the floor mesh ends at them; an off-floor ground-probe is the block signal

W4.2 (2026-05-31). Porting the point→triangle query `FUN_00432e50` clarified
how HOUSE collision actually blocks the player, which is not what "wall
triangles" suggests.

- **The query finds the highest floor under (x,z).** For each non-excluded
  triangle it does an above-plane gate (`n·p + d > 0`, winding-sensitive), an XZ
  point-in-triangle test (3 edge cross-products ≥ 0), and a ground-height solve
  (`y = −(n.x·x + n.z·z + d)/n.y`), keeping the highest. A hit only counts if the
  ground is within 5 units below the query Y. It returns the height + a surface
  normal recomputed from the winning triangle's edges.
- **HOUSE collision triangles are all type 0** (numeric materials → solid; §63),
  and the shop floor is a flat mesh at **y≈0**. There are **no vertical "wall"
  triangles** participating in the ground query — a vertical face has `n.y≈0`
  (no ground solution) and projects to a line in XZ (no area to be "inside").
- **So walls are implicit: the floor mesh simply does not extend under or past
  them.** The slide-resolver (`FUN_00483170`, W4.3) probes the *intended* next
  position with `FUN_004830f1`→`FUN_00432e50`; if that probe finds **no** floor
  triangle (the point is off the floor edge — into the counter base, past a
  wall, over the round-table footprint), the move is **blocked** and the
  resolver slides along the remaining axes. This is what produces §62's
  position-block + velocity-slide without any AABB-per-furniture list.
- Self-validation: the port's query at the room origin `(0,2,0)` on the real
  `shop_1st.x` returns `hit=1, height=0.000, normal=(0,1,0)`; `(0,2,100)` (off
  the room) returns no hit — exactly the implicit-wall behaviour.

Port `src/collision_query.{c,h}`. The worldmap 15×15 grid cell-select +
40-unit tiling wrap (`DAT_073e03ac`, gated off for HOUSE) and the dynamic-prop
path (`DAT_0438c150`) are NOT ported; we test every triangle (the per-triangle
AABB reject keeps the result identical). Bit-exact-vs-retail Frida validation is
deferred to the W4.3 trajectory replay.

## 65. Furniture collision needs a world-placement table separate from the `.x`; the room mesh self-places, furniture does not

W4.3 (2026-05-31). Porting the slide-resolver surfaced that the HOUSE collision
mesh splits into two placement regimes.

- **The room mesh `shop_1st.x` self-places.** Its frames carry per-submesh
  translations (Box02 at +23, …), so the W4.1 build (which bakes the frame
  transform) lands the room/walls in world space directly — the right wall, back
  wall, counter all sit where the player meets them. The radial-push resolver
  (8 rays, ~1-unit standoff) correctly pins the player against them.
- **The furniture meshes do NOT self-place.** `shop_table01/02.x`,
  `shop_jihanki*.x` all parse to geometry centred on their **local origin**
  (AABB ≈ ±2.5), with no world translation in the `.x`. Their world positions
  come from the engine's per-object origin table `DAT_0438c058/0a8/0f8` (the
  query/raycast subtract it: `query_pt − origin`), populated by `FUN_00436f97`
  (block 21) from a `stage_positions` source that is **not yet ported** (main.c
  documents the render path working around it with synthetic / test-captured
  positions via `--force-walker-phase2`). So in the W4.1 world-space build every
  furniture object overlaps at the origin.
- **Consequence:** wall/counter collision works today; **furniture collision
  (the central round table, vending machines) is blocked** on porting the
  furniture world-placement — the same `DAT_0438c058` data the render path still
  fakes. Until then the resolver is host-tested for the room wall only and is
  **not wired into the live player tick** (wiring it would block walls but let
  the player walk through the table — a visible partial state).

Resolver port: `src/collision_resolve.{c,h}` (`collision_raycast` = ray-vs-mesh
FUN_00433674; `collision_resolve_player` = integrate + 8-ray radial push
FUN_00483170 L207-247 + ground snap). The standoff is approximate (room wall
settles px≈2.5 vs retail 2.29) pending bit-exact tuning against the w4-table3
trajectory once furniture is placed.

## 66. HOUSE wall collision wired live: radial push blocks; floor-edge try-move climbs the counter

W4.3 live (2026-05-31). Wiring the resolver into the player tick (`collision_house.c`
builds the room mesh from `shop_1st.x` at HOUSE entry; `scene1_player_ctrl_tick`
calls it) revealed which resolver model actually fits HOUSE:

- **The radial push (FUN_00483170, `collision_resolve_player`) is the working
  model.** 8 head-height rays hit the modeled vertical faces of the room walls +
  the counter front and push the player out. Blocks correctly: holding RIGHT now
  pins px≈1.55 (was a clean walk-through to px≈41.5). No counter-climb.
- **The pure floor-edge try-move (FUN_004830f1, `collision_resolve_player_floor`)
  does NOT work alone, even though §64 says HOUSE walls are an off-floor probe.**
  FUN_004830f1 is `FUN_00432e50(pos + velocity)` — accept the move iff the
  destination is over a floor triangle. But `shop_1st.x` models the **counter as
  a solid block with a walkable TOP triangle** (world y≈2.2). The try-move finds
  that top as a valid floor and the player **climbs onto the counter** (py 0→2.21,
  px walks on to ≈4.1). Retail does not — so retail's ground query must gate
  step-up height (reject a floor whose height jumps too far above the player),
  which is not yet ported. The full engine resolver runs the try-move (walls) AND
  the radial push (furniture) together; for the room-only case the radial push
  alone is the faithful subset.
- **RESOLVED to 1:1 (2026-05-31) — two Ghidra-dropped details, both confirmed
  against retail (`runs/wall-retail` Frida probe):**
  1. **20 rays, not 8.** The ray count is `8`, or **`20` when `*DAT_068dd2f0`
     (stage-palette mode) == 0 AND pz > 0.7` (asm 0x4837xx)** — i.e. HOUSE at the
     back of the room. The extra 12 rays sample the wall/counter at stacked
     heights (`(i/8+1)·0.08 + py+0.1`) and use a 1.03 (not 1.05) cos scale. The
     retail call graph confirms it: `FUN_00433674` is called **exactly 20×** per
     `FUN_00483170` per frame at the counter row.
  2. **The push is penetration-scaled, `(1 − frac)·dir`, not the full `dir`.**
     Ghidra rendered the push as `px -= sin · 1.0`; the asm at **0x483bc3** is
     `fld1; fsubs frac; … fmuls dir; fsubrs px` → `px -= (1 − frac)·dir_x`,
     `pz -= (1 − frac)·dir_z` where `frac` is the raycast hit fraction. This is
     the whole game: a full-vector push makes the player **bounce ~1 unit off the
     wall every frame** (oscillates 2.2↔3.1); the penetration-scaled push lands
     the player **exactly against the wall** — Δpx per frame cancels the into-wall
     velocity to the digit (retail: px=3.1019 dead constant, vx=0.1435, while the
     push's z-component slides the player along at −0.042/frame).
  With both fixed the port reproduces retail's trajectory frame-for-frame: holding
  RIGHT from the back-right corner the player **slides −z down the wall** at a
  constant px=3.1019, settling at (3.103, 0.684) — bit-identical px to retail, pz
  within one slide-frame (an intro-onset phase offset, not physics). Validated by
  `tools/wall_collide_diff.py` + the `house-wall-collide --target both` amplified
  comparison. (The earlier "contour 2.15/2.29/3.10" reading was an artifact of a
  different approach trajectory; for the straight-RIGHT approach the wall section
  is a constant px=3.1019.)
- **Per-frame validation (2026-05-31, follow-up pass).** The endpoint match above
  understates it: the *whole trajectory* is physically identical. `wall_collide_diff.py`
  now searches a ±3-frame anchor-phase window — diffing `port[rel]` against
  `retail[rel+shift]` — and at **shift +1** the residual collapses to **RMS Δpx =
  0.0000, max|Δpx| = 0.0000, max|Δpz| = 0.0000** across all 2547 shared frames
  (vs RMS 0.0147 / max 0.175 at shift 0). So `port[rel] == retail[rel+1]` exactly,
  through the counter-row contact (px≈2.15 @ pz≈9.27, frame 1580), the contour, and
  the resting pin. The shift-0 "0.175 gap" is purely the **single-frame anchor-phase
  offset** from load-frame-count jitter (the determinism leak — sim is bit-exact),
  NOT a collision-accuracy error. The contour (counter jut included) is reproduced
  1:1; this front is closed. The pre-fix "px~1.55, ~0.6 short" figure was the old
  radial-push resolver, before the 20-ray + (1−frac) fix landed.

## 67. HOUSE off-map walk: the floor-edge try-move blocks it, with an implicit +1.5 step gate; exact pins need the multi-object query

W4 full-resolver pass (2026-05-31). Drove the two parked repros (walk-DOWN
off-map, walk-LEFT) on both targets with deterministic scenarios
(`tests/scenarios/house-walk-{down,left}`) + retail Frida watch
(`runs/{down,left}-retail`), and ported the missing front of `FUN_00483170`.

- **Retail cardinal pins from the spawn (px=-0.30, pz=9.35), holding each
  d-pad:** RIGHT px=3.1019, LEFT px=-1.500, DOWN(+z) pz=9.500, UP(-z) pz=8.941.
  All four are *position-block + velocity-slide* (vx/vz stays at the 0.1435
  into-wall value, position frozen, py=0 — no fall). So the player is boxed in
  a thin front strip in front of the sales counter.
- **The radial push (the 20-ray FUN_00433674) only catches RIGHT.** RIGHT is a
  modeled vertical face with an *inward* (−x) normal, so the player (approaching
  from −x) is on the +normal side and the ray hits it. The front wall (tri
  263-268, z≈10.3) has an *outward* (+z) normal — both the port AND the engine
  raycast back-face-cull it (`0 ≤ originDist && dir·n < 0`), so the radial push
  never blocks DOWN. (Confirmed: the port's mesh normal formula `(C−B)×(B−A)` is
  identical to the engine's, so they agree on the +z outward normal.) The
  pre-fix port walked clean off the map — pz→92, px→−76.
- **DOWN/LEFT are blocked by the FLOOR-EDGE TRY-MOVE (FUN_004830f1), not the
  radial push.** `FUN_004830f1` is `FUN_00432e50(pos+vel)` — accept the move iff
  the destination is over a floor; else slide per-axis. `FUN_00483170` gates the
  velocity integration (`bVar11`) on it. Porting that gate ahead of the radial
  push (replacing the unconditional `pos += vel`) stops the off-map walk and
  keeps RIGHT **bit-identical** (`wall_collide_diff.py` still shift-+1
  RMS Δpx=0.0000). `src/collision_resolve.c`.
- **The step-height gate is IMPLICIT in a +1.5 head-height offset.**
  `FUN_00432e50` adds **+1.5** to the query Y before the per-triangle above-plane
  gate (decomp L140: `local_c = … + 1.5`). A floor counts only when the query
  point at py+1.5 is above its plane, so any floor taller than py+1.5 (the
  counter TOP at y≈2.2 from a grounded player at y≈0) is rejected — the player
  mounts a ≤1.5 lip but never climbs the counter. The W4.3 try-move "climbed the
  counter" (§66) because it probed at py+1.0 and/or without this gate; the
  resolver now probes at py+1.5 (`CR_HEAD_HEIGHT`) for the try-move AND the
  ground snap. No separate step-gate needed.
- **CORRECTION — the front/left box is a HARDCODED CLAMP, not mesh geometry
  and not furniture.** The DOWN (pz≤9.5) and LEFT (px≥−1.5 when pz>7) bounds are
  `FUN_00486435`, a 200-byte room-bounds clamp the engine runs UNCONDITIONALLY
  at the tail of the player controller `FUN_0048670f` (L1640 `LAB_004893ff`),
  *after* the mesh collision (`FUN_00483170`, called from `FUN_0048b850`). For
  the HOUSE small-room arm (selector `(&DAT_04510578)[slot*0xb7f2] < 3`):
  `if pz>9.5: pz=9.5; if cc08≠4 && pz>7 && px<−1.5: px=−1.5`. Verified: neither
  the room mesh (floor query at x=−0.3 is clear flat floor to pz=10.3, no wall;
  the only front geometry is a y≤0 sill at z=10.32 the player's ≥0.26 rays miss)
  NOR the captured collision objects explain the box — the live object table
  (`tools/dump_collision_objects.py`) is room@0 + carpet@(−2,0,−1) + 3 tables
  (`shop_table01/02.x`) all at the BACK (z 0/−2/−8), none near the spawn
  (z=9.35). So: the mesh gives the right wall (px 3.10) + counter (pz 8.94); the
  clamp gives the front (pz 9.5) + left (px −1.5). The port already had the
  clamp (`player_ctrl_house_room_clamp`) but it was wired as the W3 *no-mesh
  fallback* — restoring it after the resolver (the engine runs both) makes all
  four cardinals match retail to the digit (DOWN 9.500, LEFT −1.500/9.350, RIGHT
  3.1034 still bit-identical). The multi-object query (`FUN_00432e50` subtracts
  `DAT_0438c058` per object) IS still unported, but it only matters for repro #1
  proper — the squeeze/trap when the player navigates BEHIND the counter into
  the central round table (`shop_table01.x` @ z∈[−2.5,2.5]); the cardinal-walk
  box does not depend on it.

## 68. Multi-object HOUSE collision ported: the central table blocks 1:1

W4.6 (2026-05-31). Ported the per-object query/raycast (§67's remaining piece).
`FUN_00432e50`/`FUN_00433674` loop over every placed collision object and
subtract its world origin `DAT_0438c058/0a8/0f8` before testing the object's
local-space triangles. `collision_object` gained a `float origin[3]`;
`collision_query_ground` and `collision_raycast` translate the probe into each
object's local frame (and add `origin_y` back to the returned floor height).

The HOUSE object table (captured live with `tools/dump_collision_objects.py`,
new-game tier 0) is hardcoded in `collision_house.c`: room (`shop_1st.x` @ 0) +
carpet (`shop_jutan.x` @ −2,0,−1) + 3 display tables (`shop_table01.x` @ −2,0,0
and `shop_table02.x` @ −4,0,−8 / −10,0,−2). The origins equal the already-ported
render placement (`g_scene1_walker_phase1/phase2_pos`); the `FUN_0044c88f` writer
that fills `DAT_0438c058` per stage/tier is still unported, so the table is
new-game-HOUSE specific for now.

Validation: the `house-walk-table` scenario (the w4-table3 navigation
RIGHT 14f → UP 51f → LEFT 70f, rebased onto the HOUSE_FREEROAM anchor) walks the
player around the counter to the central round table. The port pins HEAD-ON at
**px=0.729 pz=0.107 — bit-identical to retail** (`runs/w4-table3/watch.jsonl`,
§62). No regression: the four cardinal pins stay exact (the room is object 0 at
origin 0; the furniture sits at the back, clear of the spawn strip).

## 69. HOUSE table-corner edge-slide divergence is a CONTROLLER bug, not collision

W4.7 follow-up (2026-05-31). The `house-walk-tables` bench tracks retail
**bit-for-bit through rel frame 1820** (the whole head-on slide along the central
round table's front edge, obj 2 / tri 33), then diverges at **rel 1821** — where
the recorded input starts STEERING (d-pad left→up-left→down) at the table's
front-left corner. The port leaves the corner (radial push shoves it +x and it
walks back); retail slides AROUND the corner. The 9-frame drill-in scenario
`house-table-corner` (caps rel 1805–1851, between this bench's cap_04 and cap_05)
pins this moment as a permanent regression guard.

**The previous "port FUN_00483170's curved slide branch (L116–186)" plan was
WRONG.** Disassembled and disproved:
- The slide branch fires ONLY when the destination floor type ∈ {1,2} (slopes):
  asm `0x4835a1`/`0x4835a8` `cmp floortype,1` / `cmp ,2`, else → block path.
  HOUSE tris are all type 0 (§63), so it NEVER fires for the table.
- All 8 velocity (`DAT_056daabc`/`daac4`) write sites in `FUN_00483170` are inside
  that slopes-only branch (or the entity-proximity clamp / the `+0.005` nudge) —
  `objdump` `fstp …aabc/aac4` at `0x4834ee/503`, `0x4837f1/801/80a`, `0x4838d9/eb`,
  `0x4839d3`. **None are in the radial-push path** (`0x483a40–0x483c55`). So the
  resolver provably never redirects velocity for a type-0 table.
- The radial push itself is faithfully ported: single push, `(1−frac)` penetration
  scaling (`fld1; fsubs frac` at `0x483bc3`), push types {1,2,7} + normal-gated
  type-0 (`|nrm.y|<0.75`). Matches `collision_resolve.c` exactly.

So **the collision side is faithful.** Frame-aligned port-vs-retail velocity
(captured `daabc`/`daac4` via Frida) is bit-identical through the steady slide
(rel 1816–1820, dv=0), then at the corner **retail holds the slide velocity
(−0.1015,+0.1015 post-damp = 45° at speed 0.175) for ~2 more frames while the
port immediately rotates** toward the new steer direction. The gap is the player
controller's facing→impulse: the port uses raw `atan2(dpad)` every frame
(`scene1_player_ctrl.c` `player_ctrl_dpad_angle`), but the engine's walk impulse
(`FUN_0048b850` L319–326: `daabc += sin(_DAT_056db05c)·accel`) uses a STORED
facing-direction `_DAT_056db05c`, updated by the cc08 state machine in
`FUN_0048670f` — which during diagonal transitions changes more slowly/stickily
(8-way octant + the `DAT_056dae3c` sticky-diagonal bias, cf. `player_ctrl_facing_snap`)
than raw `atan2`. (The "stored db05c slewed by an 8-way/sticky law" hypothesis
in this paragraph turned out to be **wrong** — see the resolution below.)

**RESOLVED 2026-05-31 (W4.7) — it's a single-frame OPPOSING-PAIR d-pad, not a
facing slew.** Reconstructed retail's per-frame impulse heading from the existing
`golden-retail/watch.jsonl` velocities (no new capture needed): the recurrence is
`V_n = 0.82·clamp₀.₁₇₅(V_{n-1} + 0.1·(sin d_n, cos d_n))`, and since clamp+damp
both preserve direction, `d_n` is recoverable by solving
`atan2(V_{n-1} + 0.1·dir(d_n)) = θ(V_n)` per frame (`tools/facing_reconstruct.py`).
Result: retail's heading equals raw `atan2(dpad)` on **every** frame **except one**.

- The whole divergence is **rel 1822**, where the recorded input is `0x0b` =
  **LEFT+RIGHT+DOWN** (the human momentarily held both L and R while rolling the
  d-pad from down-left `0x0a` to down-right `0x09`). The port's `atan2` cancels
  L+R → straight DOWN (0°) and **snaps** facing; retail's velocity at 1822 is
  **byte-identical to 1821** (`0.14350 @ −45°`, the held down-left heading) — it
  ignored the conflicting frame entirely. The multi-frame *trajectory* drift §69
  saw downstream is just the momentum accumulator slowly rotating after that one
  bad frame.
- So `db05c` is **not** slewed/sticky and is **not** written from `atan2(dpad)`
  anywhere as a literal (like the velocity in §61, the free-roam facing write is
  through the player-struct pointer — invisible to a `_DAT_056db05c =` grep). The
  base law is plain `db05c = atan2(dpad)`; the **only** correction is opposing-pair
  rejection: **when L&R or U&D are both held, the engine discards the frame's
  d-pad and repeats the previous facing + previous moving state** (keeps walking
  the stored heading). Cardinals/valid-diagonals/empty d-pad are unchanged, so the
  W3 cardinal walks + wall slide can't regress (they never press an opposing pair).

Fix: `player_ctrl_dpad_intent()` in `scene1_player_ctrl.c` (the tick decodes the
d-pad through it; `player_ctrl_dpad_angle` unchanged). Validation: port↔retail now
**bit-exact (max |Δθ| 0.0°, max |Δpos| 0.00000) across the whole corner rel
1805–1851** (`tools/facing_reconstruct.py`; `house-table-corner` port golden
blessed); 3041 host tests pass (+1: `player_ctrl_dpad_intent_opposing_pair_holds`).
Method to reproduce: `tools/run-openrecet.sh --input-segtrace
tests/scenarios/house-table-corner/trace.jsonl --player-pos-log out.jsonl …` (the
pos-log now carries `vx/vz/facing/sticky/buttons`), then `facing_reconstruct.py`.

**Next gap (separate, already known — §60):** past the corner the
`house-walk-tables` bench shows retail stop responding to held RIGHT at the front
strip (stays pinned at spawn) — the **unported cc08 event-gate** (the port's
controller drives whenever a d-pad is held, ignoring intro/dialogue
non-controllable states). Not a controller-physics bug.

## 70. HOUSE walk-cycle anim ran 1 tick ahead: the idle→walk seed frame must observe counter 0

W3b (2026-05-31). Drilling into the `house-table-corner` cap_08 residual (the
character looked slightly off at rel 1851 while the player **world position was
bit-exact**) found it was the player's **walk-animation-cycle phase**, drifting
cumulatively over the slide — the char-region pixel diff grew 20.8% → 25.4% →
32.0% across rel 1829/1841/1851.

Ground truth `runs/w3b-anim-watch` (retail per-frame `anim/timer/counter/frame`,
a LEFT-walk drive) gives the cycle law:

- **The walk cycle is counter-driven, 4 frames × 9 ticks, wrapping counter 36→1.**
  `counter` increments every frame; cycle `frame` advances 0→1 at counter 10,
  1→2 at 19, 2→3 at 28, then wraps. The cold-start frame 0 spans counter **0..9
  (10 ticks)** only because the counter begins at 0; every wrapped cycle begins at
  counter 1, so steady frame 0 is 9 ticks. The `timer` field stays 0 throughout —
  retail does not use a separate per-frame timer for this anim.
- **The bug: the port's idle→walk transition seeded `counter=0` then ran
  `chr_anim_tick` unconditionally, whose end-of-call `counter++` left the seed
  frame at counter 1.** Retail observes counter **0** on the transition frame
  (increment starts the NEXT frame). That single +1 offset persisted through every
  wrap, so the port's whole walk cycle ran exactly **1 tick ahead** of retail —
  invisible early, visibly out-of-phase by the end of a long walk.
- **Fix (`scene1_player_ctrl.c`):** on an idle↔walk transition, seed the new anim
  (frame 0 / counter 0 / timer 0) and **skip `chr_anim_tick` that frame** — on a
  seed frame it can neither advance nor wrap, so skipping suppresses only the
  unwanted `++`. Internal wraps still `++` to 1 (counter→0 then ++), matching
  retail's steady wrap (counter 36→1). The non-transition path is unchanged
  (ticks every frame, so idle keeps breathing — §quirk idle-animates).

Validation: driving the port through the exact `w3b-anim-watch` trace, the actor
`counter` + cycle-`frame` now match retail **bit-for-bit over 11097 frames, 0
mismatches** (idle and walk segments). The corner cap_08 char-region diff dropped
**32.0% → 25.2%** and is now flat with cap_06 — the accumulating drift is gone.
Player position stays bit-exact (max |Δpos| 0.000008). The remaining ~25% is the
**untouched Tear companion sprite** (a constant lower cluster, its own
position/anim system) + octant-specific sprite content, both separate fronts.
Regression guard: host test `player_ctrl_walk_anim_starts_at_counter_zero`.

## 71. HOUSE companion (Tear) is actor 2 / char 1, a spring-follow fairy — FUN_0048a4d1, not the actor-1 spring

W-companion (2026-05-31). Rendering the HOUSE companion turned on a chain of
ground-truth surprises that each inverted a plausible static read. Captured via
`tools/frida_capture.py --watch` over 25 globals across the new-game→HOUSE tour
(`runs/companion-truth/`):

- **The live companion is actor 2 (char id 1), NOT actor 1.** `FUN_00435c98`
  inits the three actor char ids `(da1cc,da1d0,da1d4) = (0,3,1)`, but
  `FUN_00436f97` (scene-entry) then **disables actor 1** (`da1d0 → -1`) for the
  free-roam case and settles all live scales to 1.0 with record FACING 4. So at
  HOUSE free-roam only actor 0 (player, char 0) and **actor 2 (char 1)** render;
  actor 1 (char 3, a guest) never does. The actor-1 spring at the top of
  `FUN_0048a833` (`LAB_0048a899`, da1e4) is therefore dead here.

- **Actor-2 position `da1f0/f4/f8` ALIASES the particle `spawn_origin`.** Several
  integrator handlers read `da1f0/f4/f8` to anchor effects on the companion —
  it's the same memory as the engine's "spawn origin". Modeled in the port as one
  contiguous `g_scene1_actor_pos[3][3]` with `g_scene1_player_pos` = slot 0 and
  `g_scene1_spawn_origin` = slot 2 (alias macros; every old call site unchanged).

- **The visible follow is the SPRING helper `FUN_0048a4d1`, not a fixed-offset
  hover.** A first hypothesis (port the `FUN_0048a833` `local_c!=0` else block:
  ±1.3 side offset, 0.1 lerp, 2.0 deadzone) reproduced retail XZ to only one-step
  mean 0.075. The real driver is `FUN_0048a4d1` (which the dispatcher invokes for
  the controllable companion): **stay 1.5 units from the player on the
  companion's bearing** — `desired = player.xz + dir(comp−player)·1.5` when
  `dist>1.5`; `vel = (desired−comp)·0.15` clamped to 0.35; `comp.xz += vel`. That
  law reproduces retail XZ to **one-step mean 0.0036** (20× better). Y is a slow
  hover bob: `comp.y += (sin(db054·0.04)·0.2 + ground_y + 3.0 − comp.y)·0.15`
  (`ground_y = DAT_056daf88 ≈ 0` in HOUSE → bob band 2.8–3.2). Constants
  objdump-verified (0x5198cc=0.15, 0x519bc4=0.35, 0x5198c4=0.04, 0x5198d8=0.2,
  0x519438=3.0).

- **Facing is two rules, not one.** When the companion MOVED this frame
  (`|Δcomp|>0.01`) it **copies the player's facing octant** (`dab58 =
  dab00[target·0xb]`, verified 621/621). When idle it takes the standing-pose
  **side-rule** `dab58 = (comp.x ≤ player.x) ? 6 : 2` (95% of idle frames). The
  port collapses both into one free-roam controller (it has no §60 intro window);
  seeding only the engine's scene-entry FACING 4 left the idle fairy facing
  *down* instead of *left toward Recette* — a user-visible miss until the
  side-rule was added.

- **The draw default was wrong.** `FUN_004552d0`'s actor loop bound is
  `local_14 = 4.2039e-45` = the float bit-pattern of **int 3** — i.e. it draws 3
  actors BY DEFAULT (`DAT_0438b1a0`/`easydisp==1` only *recomputes* it to 1/3).
  The port had defaulted to 1 (player-only MVP); restoring the engine default of
  3 (with the per-slot `char!=-1 && scale>0` gate) lets the companion draw.

Port: `src/scene1_companion_ctrl.{c,h}` (FUN_0048a4d1), wired after the player
controller in `scene1_ingame_default_arm_tick`; actor-2 seed in
`player_ctrl_pose_house_standing`; contiguous actor-pos array in
`scene1_particles_tick.{c,h}`; draw fix + multi-char sheet cache in
`scene1_shop_walker.c` / `scene1_preload.c`. Validation: replaying retail's player
trajectory through the law (one-step mean 0.0036, facing 621/621, bob 2.806–3.197)
+ the port's own drive (one-step mean 0.0024, follow ≤1.5, facing 341/341, bob
2.806–3.194) + 4 host tests. **Deferred PORT-DEBT:** the fairy's glowing wing
sparkle (the `FUN_00447f4f` emit + spawn_origin-anchored particle handlers); and
un-MVP the placeholder chr-sheet cache → the real roster loader `FUN_00431a80`
(`DAT_073a9b18[char·0x10]`).  *(The "→ FUN_00431a80" pointer here is a static-read
error — see §72.)*

## 72. HOUSE party chr sheets load from the boot "read systemtex" init (FUN_00472f5d, sheets 0/1/2), NOT the roster FUN_00431a80

W-rostersheets (2026-05-31). Un-MVP follow-up to §71. The deferred note ("un-MVP
the chr-sheet cache → the real roster loader `FUN_00431a80`") was itself a
**static-read error** of exactly the kind §71 kept hitting. Tracing the *only*
three writers of the engine's chr-sheet table `DAT_073a9b18` (a 100-record
`{tex,w,h}` array — `FUN_00504076(&DAT_073a9b18,0x10,100,…)`) settles where the
HOUSE player+companion sheets actually come from:

- **`FUN_00472f5d`** (the boot **"read systemtex"** init — its caller
  `FUN_0047b2e7` logs `read_systemtex_ok` immediately after it; also re-run on
  device-reset). Among ~25 UI/system bitmaps it runs a **fixed 3-iteration loop**
  (`all.c` L71646; loop bound `&DAT_073a9b48 − &DAT_073a9b18 = 0x30 = 3` slots)
  loading `bmp/chr/chr%02d.bmp` for ids **0, 1, 2** into `DAT_073a9b18[0/1/2]` —
  the resident **main party**: player = sheet 0, companion (Tear) = sheet 1, the
  3rd party slot = sheet 2. Dims come from the per-chr record array
  (`DAT_0438cec8`, stride `0x1416`), which is **BSS-zero at boot**, so the loader
  passes `w=h=0` and the decoder takes the native atlas size. **This is the real
  loader for the HOUSE player+companion sheets.**
- **`FUN_00474a9a`** HOUSE branch (`all.c` L73059) loads the fixed **21-entry
  NPC/customer table** `{10,35,29,28,32,…,66,67}` (`g_scene1_chr_portrait_ids`,
  `0x5c8058`) on HOUSE entry — the shop's customer billboards. **Excludes 0/1/2.**
- **`FUN_00473c15`** (DUNGEON) — the *only* caller of the roster builder
  `FUN_00431a80`, and it **early-returns when `*DAT_068dd2f0==0`** (i.e. in
  HOUSE). So `FUN_00431a80` **never runs in HOUSE** and cannot feed HOUSE sheets.
  It builds a per-char "active this dungeon floor" bool array (looks up the
  stage/event range `DAT_0438b4cc` in the table at `0x53f8e8`, marks
  `roster[char]=1`), consumed only by the dungeon preloader's enemy/party sheet +
  mesh loads.

**Consequence for the port:** the player(0)+companion(1) sheets must load at the
**boot** "read systemtex" point (`scene1_preload_init`, which already holds the
live device), as a fixed `{0,1,2}` set keyed by **sheet id**, NOT on-demand from
the player/companion char id at HOUSE entry and NOT via any roster. The actor draw
(`FUN_004552d0`) then binds `DAT_073a9b18[char_id]` per actor (player char 0,
companion char 1, guest char 3→`-1` skipped, §71). chr sheets are `sprite_t`
(their own `IDirect3DTexture8`), outside the mesh-tex cache that HOUSE entry
resets, so a boot load survives to the first HOUSE draw. Ported:
`scene1_preload_chr_party_sheets()` (the `FUN_00472f5d` slice) into a 100-slot
sheet-id-keyed `g_chr_sheets[]` (was an 8-slot char-keyed LRU). The 21-entry
customer loop and the rest of `FUN_00472f5d`'s UI/effect textures stay deferred
PORT-DEBT (customer billboards + HUD are separate fronts).

## 73. HOUSE companion wing-glow sparkle: FUN_0048a833 tail emit, type 0x1f, with two Ghidra-dropped spawn args recovered from the asm

W-companion-glow (2026-05-31). Closed §71's deferred "fairy's glowing-wing
sparkle (FUN_00447f4f emit)" PORT-DEBT — the **emit side**. The companion driver
`FUN_0048a833` ends (`LAB_0048b2a0`, all.c-equivalent L353-366) with a particle
spawn that drops one sparkle just off the fairy every 4th frame, along her
facing. It's the ambient glow on Tear's wings.

**The emit (decompiled L353-366):**
```c
if (DAT_0438b4b4 == 0 && 0.0 < _DAT_056dae20 && 0.0 < _DAT_056dae2c &&
    (DAT_0438b8f8 != 0 || DAT_056db054 % 4 == 0) && DAT_0438b1a0 == 0) {
  angle = (float)DAT_056dab58 * 2π/8 - _DAT_073de39c;       // facing octant → world angle
  FUN_00447f4f(0, da1f0 - sin(angle)*0.6, da1f4 + 1.1, da1f8 - cos(angle)*0.6, …);
}
```
- `DAT_056dab58` = the companion's facing octant (the same value the chr-sprite
  walker reads to pick the sprite direction; in the port = actor-2
  `rec[CHR_ACTOR_FACING]`). `_DAT_073de39c` = `g_scene1_camera_yaw` (= π in HOUSE).
- `da1f0/f4/f8` = the companion (actor 2) position (= `g_scene1_actor_pos[2]`),
  post-spring-follow. The sparkle sits 0.6 toward the facing direction and +1.1
  in Y (up at the wings).

**Two args Ghidra dropped — recovered from `objdump` @ `0x48b38e`.** The decomp
shows `FUN_00447f4f(0, x, y, z)` (4 args), but the call's stack cleanup is
`add $0x1c,%esp` = **28 bytes = 7 dwords**, and the spawn API is
`FUN_00447f4f(slot, x, y, z, type, scale, param7)`. The compiler **reused two
values already on the stack** from the immediately-prior `cos` helper
(`FUN_00503994`) call:
- `push $0x1f` (the cos helper's 2nd arg) is left at `[esp+0x10]` → becomes
  **type = 0x1f (31)** — the "scene-counter wave" particle.
- the `0.1` const push (`flds 0x5193a0`) is left at `[esp+0x14]` → becomes
  **scale = 0.1** (`param_6`).
- `param_7` is an `esi` leftover, **unread** by type 0x1f (its init body
  `init_type_1f_100` + integrator `decay_drift_grav_pre` use only pos/RNG).

This is the same "the engine relies on a value the optimizer already pushed"
pattern as the §61 facing write and the §69 db05c read — invisible to a decomp
that trusts Ghidra's inferred arg count. The `.rdata` constants
(`0x51969c=0.6`, `0x519724=1.1`, `0x519398=2π`, `0x519378=8.0`) were read out of
the PE to confirm.

**Gate:** the four non-frame terms are all true in HOUSE free-roam, so only the
every-4th-frame rate term varies: `b4b4` (scene fade-in countdown) is 0 once
faded in; `dae20/dae2c` (companion render scale cw/ch) are 1.0 (>0) for the live
fairy; `b1a0` (config `easydisp`) defaults 0; `b8f8` (per-frame-emit override) is
0 → the `db054 % 4 == 0` branch. `db054` is the per-frame bob counter — the same
counter §71's hover-bob reads (the engine reads `db054` once per frame for both),
so the `%4` phase rides §71's validated bob alignment.

**Port:** `scene1_companion_ctrl.c` `co_emit_wing_sparkle()` + a call at the tick
tail (pre-`s_bob_counter++`, so it shares the bob's counter value). Calls
`scene1_spawn(0, …, 0x1f, 0.1f, 0)` and keeps `g_scene1_spawn_scene_counter_dab58`
(the init body's `DAT_056dab58` model) in step with the facing.

**[UPDATE 2026-06-05: NO LONGER invisible — the type-0x1f arm IS ported.]** The
paragraph below was written 2026-05-31 when only `pass_f` (type 0x92) drew. Since
then the **records-A type-0x1f arm of `FUN_004176ff`** landed as
`scene1_wing_glow.c` (P0.1), so the wing-sparkle particles now render — and were
**confirmed bit-1:1 vs retail** in the `house-idle-npc-drift` phase+RNG-pinned
diff (the full free-roam frame, incl. the sparkles, is 0.04%/mean0.00; see
confirmed-parity-ledger Tear row, 2026-06-05). The original (now-stale) note:

~~**Faithful but INVISIBLE today.**~~ The spawned type-0x1f particle is integrated
+ killed by `scene1_particles_tick` (`decay_drift_grav_pre`: grav −0.001, damp
0.97, kill age 0x20 — so no slot leak), but the table-A glow-billboard renderer
that would *draw* it, `FUN_004176ff` (30 KB), is unported — only `pass_f` (type
0x92) draws today. The sparkle becomes visible for free once that renderer lands;
porting it is a separate, large front. Validation here is the emit law/rate/
position only (host tests `companion_wing_sparkle_emit` + `_period`).

**Gotcha — populating records_a in HOUSE wakes the half-ported Pass F.** Landing
this emit surfaced a *latent* render bug: `scene1_records_counter_scan()` (live at
the top of the HOUSE render, `scene1_render.c`) recomputes
`g_scene1_records_a_count` from occupied slots, so the first type-0x1f sparkle
flips the count 0→1. That woke `scene1_pass_f_render` (an MVP that draws only type
0x92) which wrote its state preamble (`LIGHTING=FALSE`, texture-stage SELECTARG1,
CULLMODE=NONE) and then drew nothing — **leaking that state into the frame**, a
scene-wide lighting regression (highlights/windows shifted; `house-table-corner`
went 9/9 → 0/9, ~58k px, ~7.5%). Before this emit, *nothing* populated records_a
in HOUSE, so Pass F's `count>0` gate never fired. Fix: Pass F now scans for a
drawable 0x92 slot and returns **before any device-state write** when there is
none (a live-wired MVP must be a true no-op when it has nothing to draw). The
real fix is the full `FUN_004176ff`/`FUN_004161c7` render with complete state
management. `house-table-corner` back to 9/9 with the emit live.

## 74. HOUSE collision-object origins are the render placement arrays, offset-aliased by the 5-slot phase split — not a separate table

W4 furniture-placement de-MVP (2026-05-31, plan Step 3.4). `collision_house.c`
hardcoded the 5 new-game-tier-0 furniture objects (room, carpet, 3 tables) with
literal origins. Retiring that meant finding the real `DAT_0438c058` writer —
and the surprise is there isn't a *separate* one.

The per-object collision arrays the query (`FUN_00432e50`) subtracts are
**bit-for-bit the same memory** as the render walker's per-instance placement
columns, just at a different base address inside the same parallel-array block:

```
  render rot_y   DAT_0438c01c   collision rot  DAT_0438c008   Δ = 0x14 (5 dwords)
  render pos_x   DAT_0438c06c   collision ox   DAT_0438c058   Δ = 0x14
  render pos_y   DAT_0438c0bc   collision oy   DAT_0438c0a8   Δ = 0x14
  render pos_z   DAT_0438c10c   collision oz   DAT_0438c0f8   Δ = 0x14
```

So `collision_ox[k] == render_pos_x[k-5]`, etc. The 5-dword skew is exactly the
phase split: `FUN_00436f97` writes phase-1 instances (room/carpet/walls) into
collision slots `0..count1-1` and phase-2 instances (furniture) into slots
`(i-count1)+5`, while the render walker reads phase-2 at index `i` of its own
`+0x14`-shifted view. One write, two aliased readers — which is why §67's "the
origins equal the render placement" is literally true, not merely numerically.

Consequence for the port: `scene1_postload_walker_phase2_init` already ports
that placement (block 21, sourced from the real save-record furniture template
via the `FUN_0048ffd9` seeder), so `collision_house_build` now iterates the same
live `g_scene1_walker_phase1/phase2_pos_*` arrays — phase-1 mesh_index → stage
`map[]` path, phase-2 mesh_type → `scene1_walker_draw_b_mesh_index` → shop_table
`.x` — instead of a hardcoded table. For HOUSE tier 0 the built objects are
byte-identical to the old literals (room@0, carpet@−2,0,−1, table01@−2,0,0,
table02@−4,0,−8, table02@−10,0,−2 rot π/2), so wall collision stays bit-exact
(§66), but it now generalises to every tier/stage the writer handles, with no
duplicated placement data. The earlier `PORT-DEBT` attribution to `FUN_0044c88f`
was wrong — that function writes the *actor* spawn positions (`DAT_056da1dc`),
not the furniture origins (cf. the `FUN_0044376a` misattribution, §see ledger).

> 📍 `src/collision_house.c` (build), `src/scene1_postload.c`
> (`scene1_postload_walker_phase2_init`), `tools/dump_collision_objects.py`
> (the slot-map + VA list that revealed the aliasing).

## 75. The HOUSE in-game controller is a 3-function pipeline keyed on `cc08`; the free-roam walk is split across all three, and the walk impulse lives in the controllable code, NOT `FUN_0048b850`

Structural map for the un-MVP of the hand-rolled `scene1_player_ctrl_tick`
(`PORT-DEBT(simplified, FUN_0048670f)`) → the real engine decomposition. The
in-game tick is **three** functions, all keyed on the state id `DAT_0438cc08`
("cc08"); the HOUSE free-roam walk is **spread across all three plus a tail
clamp**, which is exactly why a single hand-rolled function could approximate it
but not mirror it.

**The three functions:**
- **`FUN_0048670f`** (1637 lines, `all.c:86539-88178`) — the whole INGAME
  interaction controller. A cc08 state machine: shop-counter haggling, customer
  approach, menus, cutscene timing, **and** free-roam walking (the `cc08==1`
  arm). Runs **first** in the default sim arm (`FUN_00442cef`).
- **`FUN_0048b850`** ("Cpop", 5 KB, `all.c:89757+`) — the movement/effects
  sub-controller `FUN_0048670f`'s `cc08==1` arm calls. Velocity clamp, facing
  octant, the integrate-and-collide call, damp, particle/after-image effects,
  and the actor render-slot population.
- **`FUN_00483170`** — physics integrate + collide. **Already ported** as
  `collision_resolve_player` (§66/§69).

**cc08 state inventory (`FUN_0048670f` dispatch):**

| cc08 | hex | role | near-path |
|----:|----:|------|-----------|
| 0 | 0x00 | free-roam entry / idle-anim init | yes (entry) |
| 1 | 0x01 | **free-roam walk** (d-pad → `FUN_0048b850`) | **yes (core)** |
| 2 | 0x02 | in-scene NPC/prop crowd | later |
| 3 | 0x03 | camera/viewpoint preview on entry | entry-adjacent |
| 4 | 0x04 | scripted NPC approach lock | stub |
| 10 | 0x0a | customer approach setup → 0x17/4 | stub |
| 15 | 0x0f | shop-front cursor / counter proximity | stub |
| 16 | 0x10 | object-interaction router | stub |
| 17 | 0x11 | fade-in input guard → 0xf | stub |
| 18 | 0x12 | menu / camera-pan cursor | stub |
| 23 | 0x17 | customer dialogue cutscene → 3 | stub |
| 30 | 0x1e | NPC dialogue choice select | stub |
| 50 | 0x32 | shop counter menu | stub |

Per-frame machinery that runs **regardless of state** (must be ported, not
stubbed): the actor-record spawn refresh (`DAT_005ce3c4` loop), the
scene-transition flag early-returns (`DAT_0450f470/485/488/495` — fades), the
camera-shake ramp counters (`DAT_0438b74c/750`), the event-timing counters
(`DAT_0438b924/b4e0`), and the common tail `LAB_004893ff` (room-bounds clamp
`FUN_00486435` → `FUN_00485861` → return).

**The free-roam per-frame pipeline, and where each step actually lives** (this is
the load-bearing finding — it reconciles §61 and §69):

| # | step | constant | function / site |
|--:|------|----------|-----------------|
| 1 | walk **impulse** `daabc/daac4 += sin/cos(db05c)·0.1` + anim id `daae8` | accel **0.1** | **`FUN_0048670f` cc08==1 controllable code** — written as `*(float*)(player+0x904)`, invisible to a `DAT_056daabc =` grep (§61) |
| 2 | speed-cap + velocity clamp `|v| ≤ 0.175` | cap **0.175** | `FUN_0048b850` @ L90010 (cap tree resolves to 0.175 in HOUSE: `*DAT_068dd2f0==0`, no dash/run) |
| 3 | facing octant `dab00` + sticky-diagonal snap | π/8, 2π, 8 | `FUN_0048b850` @ 0x48bfd2 + the `dae3c` snap (L90016-90048) |
| 4 | integrate `da1d8 += daabc; da1e0 += daac4` + collide | — | `FUN_00483170` (called from `FUN_0048b850` @ L90122) = `collision_resolve_player` |
| 5 | room-bounds clamp `pz≤9.5; px≥−1.5 when pz>7` | — | `FUN_00486435` in the `FUN_0048670f` **tail** = `player_ctrl_house_room_clamp` |
| 6 | damp `daabc,daac4 *= 0.82` | **0.82** | `FUN_0048b850` damp tree @ L90161-90198 (resolves to 0.82 when grounded `da1dc==daf88`, `db048==0`, no dash) |

Steps 5 and 6 are order-independent (clamp touches position, damp touches
velocity), so the engine's split (damp inside `FUN_0048b850`, room-clamp in the
`670f` tail) and the hand-rolled tick's order (room-clamp then damp) produce the
**same** end-of-frame state.

**The §61/§69 reconciliation (do not re-derive this):** §69 cited "the walk
impulse at `FUN_0048b850` L319-326" — that is **imprecise**. Those lines
(`daabc += sin(db05c)·(ccbc·0.01)`) are the **da1bc-gated stun/hop path** (accel
0.3, fires only for `da1bc ∈ [1,0xf)` with `db048 != 1`), and in normal
free-roam **`da1bc == 0`** so that path is skipped entirely (§61, authoritative).
The real walk impulse is step 1 above, in the controllable code, accel **0.1**.
Implication for the port: **the impulse is ported in the `FUN_0048670f` chip
(cc08==1), not the `FUN_0048b850` chip.** `FUN_0048b850`'s free-roam-active body
is steps 2/3/4/6 + the render-slot fill; its dash/egg-spawn/`da1bc`/`db048`-
cutscene branches are structurally present but **gate off** in HOUSE.

**The render-slot population (the other `FUN_0048b850` job, §71's missing writer):**
`FUN_0048b850`'s tail (L90242+) does the motion-history ring shifts (after-image
trails) and copies the 0xb-dword actor record `DAT_056daae8` into the actor
render array, calling `FUN_0044376a(&DAT_056da1b8)` (the per-actor spawn/record
allocator) — this is the live writer of `DAT_056dacc0` that
`scene1_chr_walker.c`'s `PORT-DEBT(synthetic-data)` single-slot inject stands in
for.

This map is the foundation for the controller un-MVP chip series (plan
`un-mvp-structural-parity.md` Step 3.1/3.2): Chip 1 = `FUN_0048b850` free-roam
body (steps 2/3/4/6), Chip 2 = its render-slot populator, Chip 3 = `FUN_0048670f`
prologue/bookkeeping/tail shell, Chip 4 = `FUN_0048670f` cc08 dispatch + the
cc08==1 arm (step 1 impulse) with off-path states as structural stubs.

> 📍 Decomp `all.c:86539-88178` (`FUN_0048670f`), `all.c:89757+`
> (`FUN_0048b850`); src `scene1_player_ctrl.c` (leaves 24-327 + tick 528),
> `collision_resolve.c` (`FUN_00483170`); reconciles §60/§61/§69/§71.

## 76. The `chr-walker` (`FUN_00456f56`) draws AFTER-IMAGE EFFECT banks, not the player — its `DAT_056dacc0`/`DAT_056dab6c` arrays are `FUN_0048b850`-tail trails, empty in free-roam (controller un-MVP Chip 2)

Chip 2 of the controller un-MVP (plan `house-controller-unmvp.md`) set out to
"retire the synthetic single-slot inject by wiring the real
`DAT_056dacc0` populator." Tracing the two walkers reconciled what that array
actually is — and corrected the §75 / `scene1_chr_walker.h` premise that
`FUN_00456f56` is the player draw:

- **The solid player + companion draw via `FUN_004552d0` (`scene1_shop_walker`)**,
  reading the live actor model (`DAT_056da1cc` char, `DAT_056dae18/24` scales,
  the `DAT_056daae8` sprite-state record) — i.e. the `player_ctrl_actor_*`
  accessors. This has been the visible-player path since Cchr.2g (`bf4efaa`).
- **`FUN_00456f56` (`chr-walker`) draws the ADDITIVE overlays:** the two player
  after-image banks (sweep 0 = `DAT_056dab6c` dash-trail, sweep 1 =
  `DAT_056dacc0` burst), a companion glow (`DAT_056dab40`), and the NPC people
  billboards. The sweep-0/1 banks are **5×0x44 effect records** (sprite[0..10],
  pos x/y/z at 0xb..0xd, life/age at 0xe), written by `FUN_0048b850`'s tail
  (`all.c` L90242+): the motion-history ring shift (Cpop.3), the burst fill
  (Cpop.7 → `DAT_056dacc0`), and the dash-trail advance (Cpop.6 → `DAT_056dab6c`).
- **The synthetic inject (`scene1_chr_walker_set_inject`) was DEAD code** — never
  called since the `--force-chr-walker` flag was retired to a no-op in Cchr.2h.
  So `chr-walker`'s Pass 2 was gated closed (`player_char == -1`) and the walker
  drew nothing; the player was solid via the shop-walker the whole time.

**Chip 2 retired the `PORT-DEBT(synthetic-data, FUN_0048b850)` cleanly:** the
hand-built single slot is gone; `scene1_player_ctrl.c` now owns the two banks +
the two 40-slot history rings + the burst/decay counters, and the b850 tail
(`player_ctrl_b850_render_tail`, history-shift → burst → decay-edge →
trail-advance) is wired as their live writer. `scene1_chr_walker.c` reads the
banks via `player_ctrl_render_bank_slot()` / `player_ctrl_burst_count()`.

**Net-zero visible: the banks are DORMANT in HOUSE free-roam.** Nothing spawns a
trail record (the dash/`FUN_0044376a` alloc path is a later b850 sub-chip) and
the burst counter stays 0, so both banks stay empty and the walker draws no
after-images — matching retail's plain walk. House-walk-tables frames are
**byte-identical** to the pre-chip build; house-table-corner stays 9/9.

**Why Pass 2 must stay gated closed (do not re-open without sourcing the fade
counter):** the engine scopes the whole companion+player block to the
scene-entry fade window (`DAT_0438b4b4 < 0x5b`), which the port stubs to
"always in-window" (`chr_walker_fade_counter == 0`). Pointing
`chr_walker_player_char` at the live `player_ctrl_actor_char(0)` opens Pass 2
**every** frame, toggling z-write state (`ZWRITEENABLE = TRUE` at the end of
sweep 0) outside the window retail does — which shifts steady-state walk frames.
Since the banks are empty there is nothing to draw, so opening Pass 2 is pure
risk until a later chip sources the real `DAT_0438b4b4` gate AND the banks carry
content (the dash-spawn path). Pass 2 therefore stays dormant like the
companion-glow / people passes — the standard count-stub pattern.

> 📍 src `scene1_player_ctrl.c` (`player_ctrl_b850_render_tail` + banks +
> `player_ctrl_render_bank_slot`/`_burst_count`), `scene1_chr_walker.c`
> (accessors + dormant Pass 2 note); decomp `all.c:52392+` (`FUN_00456f56`
> draw), `all.c:51618+` (`FUN_004552d0` solid player draw), `all.c:90242+`
> (`FUN_0048b850` tail writer); leaves Cpop.3/6/7 in `scene1_player_ctrl.{c,h}`.
> Reconciles/corrects §75 + `scene1_chr_walker.h` "DORMANT IN HOUSE".

## 77. `FUN_0048670f`'s prologue guard and tail rumble are BSS-gated off in HOUSE free-roam, and the room-bounds clamp runs in the tail (after `FUN_0048b850`), disjoint from the damp

Three behaviours of the engine's INGAME controller (`FUN_0048670f`, the cc08
dispatcher §75) that hold throughout steady HOUSE free-roam:

- **The prologue guard `FUN_00434d6a` returns 0.** It is the save/load dialog
  ramp gate: `if (DAT_0438b148 < 1) return 0`. `DAT_0438b148` is BSS-zero and
  **only `FUN_00434d6a` itself writes it** — and only by *incrementing* an
  already-≥1 value, never raising it from 0. The dialog is opened from the title
  screen, and its closing mode ramps the counter back to 0 (returning 1, not -1)
  on close. So any HOUSE frame sees 0 → returns 0 → the controller does **not**
  early-return. The `-1` early-return (which aborts the whole tick) only fires
  while that save/load dialog is actively pumping, which never happens in-shop.
- **The tail screen-rumble `FUN_00485861` (`LAB_004893ff`) is wholly gated on
  `DAT_0438b764`**, BSS-zero in free-roam, so its entire body is skipped — the
  tail rumble is a no-op until something kicks `DAT_0438b764` (the camera-shake
  subsystem, e.g. a dungeon hit). The per-frame RNG-pool churn `FUN_0046f621`
  likewise touches no player-visible state.
- **The room-bounds clamp `FUN_00486435` runs unconditionally in the tail**
  (`LAB_004893ff`, `all.c:88173`), *after* `FUN_0048b850`'s mesh collide + damp —
  the free-roam path reaches it via `goto LAB_004893ff` (`all.c:87758`). It is
  order-independent of the damp: the clamp touches only the player *position*
  while the damp touches *velocity* (and the anim update touches the sprite
  record) — three disjoint state sets. The small-room arm clamps `pz ≤ 9.5` and
  `px ≥ -1.5` while `pz > 7.0` (§67); these HOUSE bounds are not in the room
  collision mesh or any placed object.

> 📍 decomp `all.c:86539-88176` (`FUN_0048670f`: 86575 guard `FUN_00434d6a`,
> 88171-88175 tail `LAB_004893ff` → `FUN_00486435`/`FUN_00485861`). Port:
> `scene1_player_ctrl.c`, `title_save_dialog.c`. Builds on §75/§76.

## 78. `FUN_004850ec` is the engine's canonical "enter free-roam" setter (`cc08=1`); the `cc08==1` arm wraps the walk in nested escalation/`cc04`/proximity/interaction guards

Two structural facts about the retail `cc08` dispatch (§75), filling in §75's
state table:

- **`FUN_004850ec` (0x4850ec, 18 B) is the canonical "hand control back to the
  player" setter:** `DAT_074b2ec4 = 0; DAT_0438cc08 = 1;` — it clears the
  scene-exit latch (`DAT_074b2ec4`, read at the dispatch's `all.c` exit arm) and
  sets the in-game state id to free-roam. The engine calls it on every path that
  returns to free-roam: scene entry, and the close of a dialogue / menu / counter
  state. So `cc08==1` is the resting state, and the other states are entered
  transiently by their own transitions (customer approach → `cc08=4`, counter
  open → `cc08=0x32`, …) and return via `FUN_004850ec`.
- **The `cc08==1` free-roam arm (`all.c:919-1225`) is a nested guard chain around
  the `FUN_0048b850` walk call**, not a flat "read d-pad and move":
  1. **customer-approach escalation** (922-957) — when the shop-customer count
     `(&DAT_0450fb98)[shop]` crosses 1 / 2 / 6 / 0xd thresholds the controller
     flips `cc08=4` (scripted approach) and consumes the frame;
  2. **`cc04==0` gate** (958) — `DAT_0438cc04` is the free-roam interaction
     sub-state: 0 = walking, 1/2 = an object/customer interaction is mid-flight
     (the `all.c:1227+` arms);
  3. **proximity / approach detection** (961-1082) — reads the nearest customer
     (`DAT_056da1f0`) and item-pickup positions vs the player to raise the
     talk / pick-up affordance bools and tick the approach timers
     (`DAT_0438be7c/be80`, the `DAT_056db000` gauge);
  4. **d-pad interaction** (1086-1214, under `db048==0`) — the action-button
     masks (`DAT_073dddd4 & 0x20` cancel/exit, `& 0x40` talk, `& 0x10` object/
     door) each require a live target;
  5. **the walk** — `FUN_0048b850()` (1216) only when none of the above consumed
     the frame.

  With no customer/item present and `cc04==0`, all four guards fall through and
  the arm is just the walk — but the guards run every free-roam frame, which is
  why the proximity/approach timers advance even while the player is only walking.
  The other `cc08` states (`0,2,3,4,0xa,0xf,0x10,0x11,0x12,0x17,0x1e,0x32`) are
  inline regions that each fall through to the common tail `LAB_004893ff`.

> 📍 decomp `all.c:919-1225` (the `cc08==1` arm), `all.c:85361-85366`
> (`FUN_004850ec`). Port: `scene1_player_ctrl.c` (`player_ctrl_cc08_*`). Builds
> on §75.

## 79. The Tear wing-glow (records-A type 0x1f) is drawn by `FUN_004176ff`'s records-A sweep **catch-all `else`** as a blue billboard — records-B is empty in free-roam, so its L1180 0x1f arm is a decoy

§73 found the emit (records-A type-0x1f, every 4th frame off the fairy). The
render side, resolved 2026-06-01 by a live retail probe (`--dump-records-b`
+ `--quad-hist`, `runs/tear-glow-probe/` + `runs/tear-glow-draws/`):

- **The sparkles live in records-A** (`DAT_069b2f80`, stride 0x25, TYPE at +0x30).
  At any free-roam frame there are ~8 live type-0x1f slots, scale 0.1, clustered at
  Tear's hover pos ≈(1.2, 4.0, 9.0) (y≈4 = the flying fairy; Recette is y=0), ages
  stepping 1/5/9/…/29 (emit-every-4, kill at 32). **records-B (`DAT_069324b0`) is
  EMPTY (`count_b==0`)** the whole run — free-roam *and* the tutorial dialogue.
- **So `FUN_004176ff`'s only int-`== 0x1f` arm (L1180, the records-B sweep) NEVER
  runs in free-roam** — it's a decoy. Porting it would draw nothing.
- The actual renderer is `FUN_004176ff`'s **records-A sweep** (L1422–~L1995, base
  `&DAT_069b2fb8`, count `DAT_0076b960`). A `--quad-hist` draw trace shows it emit
  6 `DrawPrimitiveUP` billboards at exactly the 0x1f positions (±256 obj-space quad,
  TRIANGLESTRIP, FVF 0x142, glow atlas, per-particle world matrix).
- **Why a `== 0x1f` grep can't find it:** the records-A sweep dispatches type as a
  **float-reinterpreted** constant (`fVar22 = pfVar15[-2]`, e.g. `== 1.23314e-43`
  = 0x58). Its explicit arms are {0x58,0x93,0x5a,0x56,0x42,0x41,0x61,0x72,0x62,0x1,
  0x2}; **0x1f is not one**, so it falls into the sweep's **catch-all `else`** (≈L1708),
  which is the wing-glow arm.
- **The blue:** that `else` writes the vertex diffuse as `B=uVar5, R=G=uVar5/2`
  (`uVar5` = age-fade intensity, `age·0x10` clamped 0xff then ramped down) →
  D3DCOLOR ≈ `0xFF7F7FFF`, a bright translucent **blue** — matching what the user
  sees. (The records-B L1180 arm, by contrast, is greyscale — another sign it's the
  wrong path.)

Lesson: a particle "type" can be read as `int` in one sweep and `float-bits` in
another within the *same* function; and a type with no explicit arm is still drawn
(by the catch-all). Grep both encodings, and never trust an arm fires without
confirming its table is non-empty at runtime.

> 📍 decomp `4176ff.c` records-A sweep L1422-1995 (`else` ≈L1708), the decoy at
> L1180. Probe: `runs/tear-glow-probe/`, `runs/tear-glow-draws/`. Port target:
> a records-A blue-billboard renderer modeled on `scene1_pass_f.c`. Builds on §73.
>
> ⚠️ **Superseded by §80** — the "L1422 sweep / catch-all `else` ≈L1708 / blue
> *diffuse* `0xFF7F7FFF`" claims above are WRONG. Kept only as a record of the
> wrong turn. Read §80 for the verified arm + recipe.

## 80. The Tear wing-glow is the records-A **main** sweep arm L3818 (grey age-fade diffuse × *blue texture*, additive) — §79's L1422/catch-all/blue-diffuse theory was wrong

Landed 2026-06-01 as `src/scene1_wing_glow.c` (chip P0.1). §79 correctly placed
the sparkles in records-A type-0x1f, but mis-located *and* mis-coloured the draw.
Corrections, all confirmed against retail ground truth
(`tools/dump_wingglow_groundtruth.py` for the vbuf, `--d3d-trace` state-replay at
the draw; `runs/wingglow-gt*`, `runs/wingglow-d3d`):

- **Two records-A sweeps, not one.** L1422 (base `&DAT_069b2fb8`) is an *earlier*
  additive pass whose explicit arms are {0x58,0x93,0x5a,0x56,0x42,0x41,0x61,0x72,
  0x62} — **0x1f is not among them and falls through to no-draw there** (the L1708
  "catch-all" is a sub-switch *inside* the 0x41/0x61/0x72/0x62 arm, not a main-type
  default). The real draw is the **main** records-A sweep (L2608+, base
  `&DAT_069b2f80`, type read as **int** at +0x30), arm **L3818**
  (`iVar16 == 0x1f || 0x64 || 0x6d || 0x65 || 0x68 || 0x6c`), draw at **ret_va
  0x41e165**.
- **The blue is the TEXTURE, not the diffuse.** The arm writes a **grey** diffuse
  `0xFF·iii` with `i = (age>0)?(0x7f − 4·age):0x7f` (age-fade), then MODULATEs it
  against a bright pale-blue 32×32 cell of `bmp/effect.bmp` (UVs u[0.252,0.373]
  v[0.502,0.623]) under **additive ONE/ONE** blend. §79's `0xFF7F7FFF` blue-diffuse
  was a misread.
- **The vbuf geometry is unreachable statically.** `&DAT_0064b548` is a BSS
  billboard template (±256 quad, FVF 0x142) whose xyz+uv are **never written in
  the decompile** — had to be read live from retail. Per-slot world =
  `RotZ(rot.z)·billboard·Scale(scale·0.005)·Translate(pos)`.

Verified: port now draws additive blue at Tear (brightest pixel +67,+100,+140 RGB).
Residual aggregate-glow gap vs retail is the frozen player (controller unported →
Tear hovers offset) + the unported Tear sprite palette, not the renderer.

Lesson (reinforcing §79): a `--quad-hist` *position* trace tells you a function
draws *somewhere*, but not which arm, what blend, or what the vertex buffer holds.
For a render port, replay the full `--d3d-trace` state at the draw and dump the
actual vertex bytes — don't theorise the recipe from the decompile alone,
*especially* when the buffer is BSS with no visible initialiser.

> 📍 arm `4176ff.c` L3818, draw ret_va 0x41e165, tex `DAT_073cc8c0`=effect.bmp.
> Recipe + GT: `docs/findings/scene1-wing-glow.md`. Port: `src/scene1_wing_glow.c`.
> Tool: `tools/dump_wingglow_groundtruth.py`. Builds on §73, §79.

## 81. The HOUSE companion (Tear) idle-FLAPS her wings — a 4-frame anim loop the port left FROZEN, so her body+wing-glow stuck on one chr01/chr02 cell set and the additive glow diverged from retail

`scene1_wing_glow.c`/`scene1_chr_walker.c` Pass 1 + `scene1_shop_walker.c`
draw Tear's **body** (chr01, sheet 1) and her additive **wing-glow** (chr02,
sheet 2) through the same leaf `FUN_0045a56f`, both reading the live companion
sprite-state record (engine `&DAT_056dab40` = actor 2). The leaf picks which
sprite cells to emit from the record's `(ANIM, FRAME, facing-bank)` via the
formdata LUT — so the record's **FRAME field decides the wing pose**.

The port left actor 2's anim fields at the **zero-init FRAME 0 and never
advanced them**: `player_ctrl` pose-house-standing set only char id / scale /
facing / position, and `scene1_companion_ctrl` ran the spring-follow + facing
but **never ticked the sprite animation** (its `co_set_anim` only *resets* the
cycle on an idle↔moving transition, and early-returns at steady idle since
`CO_ANIM_IDLE==0` matches the zero ANIMSEL). So Tear's wings were frozen on one
pose while the player (actor 0) was ticked every frame.

**Ground truth** — retail's idle companion ANIMATES a **4-frame wing-flap loop**
(`runs/comp-anim-probe`, Frida `--chr-leaf` over a 120-frame window):

| anim FRAME | cell | glow cells (`fd_ncells`) | wing |
|---|---|---|---|
| 0 | 8 | 8 `[2,3,5,6,7,9,10,11]` | **spread (big glow)** |
| 1 | 9 | 8 | **spread** |
| 2 | 10 | 6 `[1,2,5,6,9,10]` | folded (smallest) |
| 3 | 11 | 7 | intermediate |

…then wraps 3→0; each frame is held ~8–12 ticks (cycle ~44 frames). It advances
**+1 per engine frame** — consecutive retail frames 17544/17545 show `COUNTER
25→26`, `TIMER 5.0f→6.0f` (`runs/cchr2b/chr_leaf.jsonl`), i.e. `chr_anim_tick`
with `dt=1.0` called once per frame. At the folded **FRAME 2** the body (chr01)
and glow (chr02) resolve the *same* 6-cell layout (only the per-char formdata
base differs — body `fd_base 1666`/`fd_start 55`, glow `3100`/`92`, both
`fd_pos [1,2,5,6,9,10]`), so the glow's quads exactly overlap the body and the
additive blue washes over her silhouette; at the spread frames 0/1 the glow's
8 cells extend *wider than* the body (the wings). (This overlap is what reads
as Tear's "bluer" hair/face in retail — it's the glow, not the body palette,
which is bit-identical between targets.)

Because the leaf reads the live FRAME, a frozen FRAME stuck the glow on one
pose: seeding it to FRAME 2 (an early mis-fix) picked the *folded/smallest*
pose, and the zero-init FRAME 0 picked the spread pose but never flapped.

**Fix:** tick the companion's sprite anim every non-transition frame in
`scene1_companion_ctrl_tick` via `chr_anim_tick(rec, char, 1.0f)` — mirroring
the player controller (`scene1_player_ctrl.c` L919). On an idle↔moving
transition `co_set_anim` already reset the cycle to frame 0, so skip the tick
that frame (CO_REC_ANIMSEL changed). The wings now flap faithfully (validated
+1/frame against the table above); the glow washes over the body and her hair
reads silvery-blue.

**DEFERRED — flap PHASE alignment (user: "we will chase phase later", 2026-06-01).**
The port's flap *mechanism* matches retail, but its *phase* at the
house-movement capture anchor is offset: the port animates the companion all
through the ~1540-frame stubbed intro/tutorial, so its cycle position at
free-roam differs from retail's. Retail is on a *spread* frame at all three
house-movement captures; the port lands on a folded/intermediate phase. This is
the same load-jitter / intro-timing baseline as Tear's hover-bob Y (cf. the
deferred scenario auto-shift, [[project_next_char_controller]]). Chasing it
means aligning the anim start to free-roam onset (or gating the tick to the
controllable state) — not done.

> 📍 Tick: `scene1_companion_ctrl.c` `scene1_companion_ctrl_tick`.
> Leaf cell selection: `scene1_chr_sprite.c` `chr_sprite_build_quads`.
> GT: `tools/frida_capture.py --chr-leaf` → `chr_leaf.jsonl` (`runs/comp-anim-probe`,
> `runs/cchr2b`). Builds on §71 (companion = actor 2 / char 1), §80 (sparkle trail).

## 82. Character ground shadows (`FUN_0045aa36`) are a D3DXMatrixShadow projection of a ±256 shade quad, NOT a sprite-attached blob — size shrinks with height, alpha = `(int)(height·5)`, multiplicative-darken blend

The HOUSE shadow pass `FUN_0045aa36` (0x45aa36, 4493 B, engine `FUN_00459dfd`
L205 — right after `FUN_00459847(0)`) draws ground shadows for *seven* distinct
actor/effect tables, all with one recipe: the static ±256 XZ quad `DAT_0064bd88`
(`all.c` L9111 init; UVs sample the 64×64 `shade.bmp` blob at `(0.5..63.5)/256`),
textured with `shade.bmp` (`DAT_073cc8f0`), **projected onto the actor's floor
plane by a D3DXMatrixShadow**, grey-keyed, drawn as a 2-prim `TRIANGLESTRIP`.

Only **Block A** (L59-121, the `DAT_056da1b8` actor table) is live in free-roam:
the player (actor 0) + companion (actor 2) shadow. The other six blocks walk
empty tables (customers / objects / combat projectiles / spawn-flash).

Two non-obvious things:

1. **The world matrix is `Shadow · Scaling · Translation`** (built `W1 =
   Scaling*Translation` at L99-100, then `W = Shadow*W1` at L108-109; verified by
   the push order at objdump 0x45ad53 — `Multiply(out, Shadow, W1)`). The shadow
   matrix is `D3DXMatrixShadow(light=(0,1,0,0), plane)` where `plane =
   PlaneFromPointNormal((0,0.2,0), (-n.x, n.y, -n.z))` and `n` is the **floor
   surface normal from the `FUN_00432e50` ground query** (ported as
   `collision_query_ground`, W4.2). On HOUSE's flat floor `n=(0,1,0)` so the
   shadow matrix is nearly a no-op (just lays the flat quad on the plane) and the
   placement is all in Scaling·Translation — but on a slope it skews the quad to
   lie on the surface. The engine caches the per-actor floor height + normal in
   `DAT_056daf94` / `DAT_056daebc..ec4` via `FUN_00483170`; the shadow re-reads
   them. The port re-queries `collision_query_ground` at draw time (deterministic,
   same result).

2. **Two newly-identified D3DX PSGP helpers.** The matrix thunks dispatch through
   the `FUN_004cdd9f` PSGP table (`PTR_LAB_005fda48`, copied from the default impls
   at `PTR_FUN_005fdb30`). Slot 12 (`*0x5fda78`, default @ 0x4a4f65) =
   **D3DXPlaneFromPointNormal** → `(n, -dot(point,n))`, NOT normalised. Slot 27
   (`*0x5fdab4`, default @ 0x4a5c86) = **D3DXMatrixShadow** → normalises the plane,
   `dot = P·L` (4-component), `out._ii = dot - L_i·P_i`, off-diagonal `-L_j·P_i`
   (row-major). Both ported into `math3d.c` (`plane_from_point_normal`,
   `mat4_shadow`).

Geometry/colour (per live actor `i`, objdump 0x45ab90-0x45ae44, `.rdata` consts):
- `height = pos.y - floor_y`
- `alpha  = clamp((int)(height·5.0), 0..255)`   — grey level; **truncates** (ftol).
- `size   = clamp(0.038 - height·0.0015, .025..038) · 0.14`
- gates: `char != -1`; floor hit (`daf94 != -100`); `|n.y| >= 0.7`;
  `scale_xz > 0`; `scale_y > 0`.
- **companion (i==2) tweak (L86-89):** `size ×= 0.9`, `alpha += 0x40` — a smaller,
  more-opaque shadow than the player's.
- colour = `0xFF<a><a><a>` (opaque grey); the translucency is the **blend**:
  `SRCBLEND=ZERO, DESTBLEND=SRCCOLOR` → `result = DST · SRC.rgb`, multiplicative
  darkening (set up at L48-54; `ALPHABLENDENABLE` + `COLOROP=ADD` + `ALPHAOP=
  MODULATE` precede it, but the ZERO/SRCCOLOR pair is what darkens).

Consequence: a player standing with feet ≈ on the floor has `height ≈ 0` → faint
shadow; the companion floats (`height ≈ 3`) → its `+0x40` term keeps the shadow
visible. So the **player shadow strength is a direct function of the port's
player-Y vs floor-Y agreement with retail** — a useful cross-check.

> Port: `scene1_chr_shadow.c` (`chr_shadow_build_actor` pure core + Win32 pass),
> wired at `scene1_render.c` L205. Helpers: `math3d.c` `mat4_shadow` /
> `plane_from_point_normal`. Object/furniture shadows (`FUN_00470385`,
> `DAT_073a6e84` table — the missing table contact-shadow in
> scene1-house-render-gaps.md §4) are the natural follow-up; they need the object
> table modelled. Builds on §71 (companion = actor 2), W4.2 (collision query).

## 83. HOUSE background-window NPCs (mislabelled "ambient motes"): dead pause path → simple back-wall drifters; a dark contact shadow + a bright character sprite

> **CORRECTION (2026-06-02):** this is NOT an ambient-particle effect — it is the
> **background-window NPC system** (the townsfolk drifting past the shop's back
> window).  The "bright sparkle" the section below calls unported is the NPC's
> **character sprite** (`FUN_0046f737`), now **ported + user-verified** (the
> red/10×-debug observation below — "blobs at the back-wall window line" — was
> literally these NPCs).  See [[scene1-bg-npc]].  Renamed `scene1_motes` →
> `scene1_bg_npc`.  The drift/RNG/dark-shadow mechanics documented here remain
> accurate.

The subsystem is four functions sharing one 100-byte SoA record array (base
`DAT_073a7f80`, stride `0x64`, count `DAT_005c7dd4 == 6` NPCs):

- **`FUN_0046f621`** (warmup pump): the FIRST call ever (latch `DAT_073a8bb8`)
  runs the sim **180×**; every later call runs it once. Called once/frame on
  `FUN_0048670f`'s main path (all.c:86722), i.e. the free-roam controller.
- **`FUN_0046f2a3`** (894 B, sim): spawns one mote per call until the cursor
  (`DAT_073a8bb4`) hits 6, then integrates all 6. **The shared-LCG consumer.**
- **`FUN_0046f648`** (render): one dark quad per mote, drawn from inside the
  ground-shadow pass (`FUN_00470385` @ the `FUN_0045aa36` L122 slot).

**The motes live at the BACK of the room.** Spawn places them at
`x = idx·4.6 − 14` (∈ [−14, 9]), `z = −11 − unit·4` (∈ [−15, −11]), `y = 0`.
With the HOUSE camera (eye ≈ (−1, 22, 15), lookat ≈ (−1, 1.2, 1)) that's the
top of the screen, up by the back-wall windows — far from the player's z≈9.35.

**The per-tick "pause/crossing" machinery is dead code.** objdump @ 0x46f4dc /
0x46f502 (dir+1) and 0x46f50e / 0x46f54f (dir−1) compute a flag `ecx` from
`x vs vthresh` and then re-test `x vs vthresh` for the pause-set — with the
opposite sense, using the SAME current `x`. The two are mutually exclusive, so
the pause counter (`+0x54`) is **never** set, the `+0x54 > 0` branch (and its
lone `rng15` direction-flip roll) never runs, and `vthresh`/`mode` end up
vestigial (they feed only this dead path). The motes are therefore pure
one-axis drifters: `x += dir·speed·0.05` each tick, and at the room bounds
(`x > 25` for dir+, `x < −15` for dir−) they **bounce** — flip direction and
re-roll z/vthresh/mode (x is NOT reset). So "respawn" is a bounce, not a
teleport.

**RNG order (must match retail in count AND order — the foot-dust stream
depends on it, scene1-rng-stream-parity.md):**
- spawn: **7 rolls** (8 if the first mode roll ≥ prob/2): dir·sign(`rng15`),
  z(`unit`), speed(`unit`), vthresh·sign(`rng15`), vthresh·mag(`unit`),
  prob(`rng15`%100), mode-r1(`rng15`%100)[, mode-r2(`rng15`%100)].
- bounce: **4 rolls** (5 likewise): z, vthresh·sign, vthresh·mag,
  mode-r1[, mode-r2]. **speed + prob are NOT re-rolled.**
The 180× warmup front-loads 6 spawns (+ rare bounces) onto the first HOUSE
frame, exactly where retail runs `FUN_0046f621`.

**`FUN_0046f648` draws the dark CONTACT blob, not the bright sparkle.** It sets
no texture/FVF/blend — it inherits the shadow pass's envelope (shade.bmp,
`SRCBLEND=ZERO/DESTBLEND=SRCCOLOR` multiplicative darken, ZWRITE off), draws the
shared ±256 quad `DAT_0064bd88` at `Scaling(−0.0046, 0.0046, 0.0046) ·
Translation(x, y+0.08, z)`, tinted `0xff202020`. Result: a small soft dark spot
on the back floor — visually subtle (verified by a bright-red/10× debug build:
the blobs sit at the back-wall window line). The actual VISIBLE floating
sparkle is a SEPARATE bright-sprite pass keyed off the per-record sprite-anim
header (`+0x00..+0x14`, stepped by `FUN_00482a51/71` via the
`DAT_005c7ce0[type·2]` LUT). **UPDATE 2026-06-02:** that bright pass is
`FUN_0046f737` and is now ported (`scene1_bg_npc_sprite_render`) — it is the
NPC's character billboard (sheet `DAT_073a9b18[char]`, scale 0.03, `0xff7f7f7f`),
not a sparkle.

> Port: `scene1_bg_npc.c` (`scene1_bg_npc_tick` / `_sim_once` /
> `_shadow_render` [dark] / `_sprite_render` [bright, FUN_0046f737]), wired at
> `scene1_player_ctrl.c` (controller prologue, replacing the old
> `player_ctrl_prologue_churn` no-op), `scene1_chr_shadow.c` L122 (shadow), and
> `scene1_shop_walker.c` L457 (sprite). Data tables `DAT_005c7dd8`
> (type = {0,1,6,7,9,8}) / `DAT_005c7ce0` (type→sheet char, → chr{10,35..39}) are
> static `.data`. See [[scene1-bg-npc]].
> The steady per-frame dust consumer `FUN_0046c9a2` is still unported, so the
> foot-dust *phase* won't fully match retail until that lands too (both needed).

## 84. Opening-prologue dialogue: standee position/colour tween, char-based reveal, and effect-sprite fades (the `.ivt` animation model)

The `0x46c` dialogue interpreter animates its character/effect standees through a
per-frame tween loop (`FUN_0046c320` lines 107-134) the port had deferred (it
SNAPped everything to its final pose). Three non-obvious mechanics, all
raw-disasm + retail-probe (`runs/dlg-opening-probe`) confirmed:

**Position tween (`chr:move`/`moveto`/`speed`).** `move:x,y` (0x46da33/0x46dc0a)
sets BOTH current (field1/2) and target (field3/4); `moveto:x,y`
(0x46da6e/0x46dc30) sets the **target only**. Each frame the current slides
toward the target by `speed` (field5/6) with a ±(speed-1) deadband. `speed:f`
(0x46dc45) is a **×1000 fixed-point** arg: handler does `field5 = (a2 & 0xffff) /
1000.0`, and the parser passes `round(atof·1000)` — so `speed:5` → field5 = 5.0
px/frame (retail-confirmed: Tear's `move:-390 → moveto:-100 speed:5` slides at
exactly 5 px/frame, the divisor `DAT_0051958c` = 1000.0).

**Colour fade (`chr:col`/`colto`/`fadeframe`).** `col:r,g,b,a` (0x46da83) sets
the CURRENT colour floats (field15=b,16=g,17=r,18=a) only. `colto` (0x46db20)
does NOT store a target — it computes the **per-frame delta** field19-22 =
`(target_ch - current_ch)/fadeframe` and sets the countdown field10 = field9
(`fadeframe`, set by 0x46dc82). The tween adds field19-22 to field15-18 each
frame while field10 > 0. So the slow fade-from-black is just `chr:5` =
`kuro.tga` (a 640×480 black image), `col …,255` → `colto …,0` over
`fadeframe:240` ≈ -1.06 alpha/frame; "fade in from black" is a standee colour
tween, not a dedicated fade op.

**Effect sprites appear at FULL alpha, then fade OUT.** A pop-up like the sigh
(`chr:6 tameiki.tga`) scripts `col …,0 / colto …,255 / … / col …,255 / colto
…,0` — but every `chr:*` setup command returns 1 (advance+run-next same frame),
so they all execute in ONE frame and the **second** `col …,255` overwrites the
fade-in. Net: the sprite snaps to alpha 255 the frame it shows, then the second
`colto …,0` fades it out over its `fadeframe`. (Verified bit-identical vs retail
via the `EXTRA_SPRITE_*`-anchored `intro-sigh` scenario.)

**Text reveal is CHARACTER-based, not pixel-based.** The END flag
(`DAT_073a3e04`, the "book-icon / awaiting-advance" gate) is latched in the DRAW
(`FUN_0046c9a2` 350-388): budget = `(reveal-4)·DAT_005c78dc/32` **logical
characters** (`DAT_005c78dc` = text speed, default **32**; table
`DAT_005c78e0`={16,32,1024} slow/normal/fast); each row subtracts
`FUN_00405a52`'s return = the row's full logical char count minus the final;
END iff the budget clears every row by >2. The reveal counter climbs +1/frame,
so a line typewriters in ≈ its char-length frames (retail probe: a 2-row
~42-char line's END rises at reveal≈47). Port: `scene1_dialogue_run.c`
`ive_completion` + `ive_row_count` (SJIS-aware). (The old nominal-320-px metric
made a line take ~640 frames to "complete", so it never auto-completed — the
player had to press the advance to slam the reveal, then again to advance; with
the real metric the book icon appears on its own and ONE press advances.)

Port: `scene1_dialogue_run.c` (`ive_run_tween`, the col/colto/move/speed
handlers, `ive_completion`), `scene1_dialogue.c` parser (`ive_atof_milli`).

## 86. Conversation pose (iv1_2): the freeroam chibis pose to face + animate at each other (Recette anim 6 look-up/blink, Tear anim 4 talk) — PORTED, blink-phase deferred to §85

`FUN_0048407f`'s conversation branch (the master event-actor tick) poses the two
HOUSE freeroam sprites whenever the talk-event flag `DAT_0450f470[save] == 0`:
they turn to face each other on the X axis (Tear.x ≤ Recette.x → Recette octant
2 / Tear octant 6 / player facing-angle `db05c` = −π/2; else 6 / 2 / +π/2), then
**Recette enters anim/state 6** (「ティアの話を聞くよ」 — a look-up at Tear whose
4-entry loop 38(d20)→39(d6)→38(d32)→39(d6) IS the blink: cell 38 eyes-open held
20/32 ticks, cell 39 eyes-closed 6 ticks) and **Tear enters anim/state 4** (her
talking pose). The state field (record dword 5 = engine `daafc`/`dab54`, the same
field the companion ctrl calls `CO_REC_ANIMSEL`) gates the enter so the cycle
resets only on the transition into the state; the per-frame `FUN_00482a71`
anim-step (= `chr_anim_tick`) then advances the blink. Released to idle (anim 0)
when the flag is set.

Ported in `scene1_conversation_pose.{c,h}` (the pure branch + a tick that holds
the pose on the live actor records during iv1_2 and steps both actors' anim).
Wired at the top of `scene1_ingame_default_arm_tick`; the companion controller
yields its own anim/facing selection while `scene1_conversation_pose_active()`,
and the player's freeroam arm was already gated off for the dialogue. User-
verified 1:1 vs retail (`intro-iv2-gap`) **modulo** the known-deferred Tear
position (confirmed-parity ledger) + the radial-burst billboard near Tear (a
talk-manager effect spawn, still unported) + the blink **phase** (next §).

**PORT-DEBT — the producer + the blink phase.** The faithful talk-flag producer
is the intro event timeline `FUN_00470a46` (clears the flag at the END of the
deferred shatter transition `FUN_004526f5(0,0x1e)`, once `FUN_004528b3()!=0`) +
`FUN_004852fb` (sets it on scene-out). That path is entangled with the still-
deferred shatter-transition render, so the port derives the flag from the iv1_2
dialogue lifecycle (`scene1_intro_dialogue_active() && generation>=2`) instead.
This enters the pose at the iv1_2-**arm** edge, whereas retail clears the flag
~`0x122−0x104 + 30 = 0x4a` frames later (after the shatter transition completes),
so the blink cycle — which resets on entry — is **phase-shifted** at any fixed
HOUSE_FREEROAM+N capture (the §85 mechanism, plus this deterministic producer
offset). The blink anim itself is faithful (rate + sequence); only the entry
frame differs. See `conversation-pose-driver.md`.

## 85. PHASE-ALIGNMENT (open, defer): the port arms the prologue scripts at a different load-offset than retail, so fixed-anchor-offset captures sample animations at the wrong phase

Captures keyed to a *fixed* offset from `HOUSE_FREEROAM` (e.g. the first
`intro-opening` revision) sample the iv1_1 fade-from-black / Tear slide at a
slightly different point on port vs retail, because the port's new-game HOUSE
load + the iv1_1→iv1_2 inter-script load are **synthetic fixed-frame brackets**
(PORT-DEBT — the real async asset loads aren't reproduced; see
`opening-prologue.md` §"script-load / gate / transition subsystem"). So iv1_1
*arms* a few-dozen frames earlier/later than retail relative to `HOUSE_FREEROAM`,
shifting every subsequent animation's phase. The animations themselves render
bit-identically — proven by re-capturing anchored to the animation's OWN edge
(`EXTRA_SPRITE_FADEOUT` for the kuro fade → 0.07 mean|abs|/ch vs retail). **For
parity captures, anchor to the per-effect `EXTRA_SPRITE_*` / `TEXT_ANIM_*` edges,
never a fixed `HOUSE_FREEROAM`+N.** Open work to make the *absolute* timing match
(not just per-effect phase): model the real load durations so the prologue arms
at retail's offset. Low priority (the synthetic load timing is
environment-dependent / not byte-reproducible anyway), but tracked here so the
phase gap isn't mistaken for a render bug later.

## 87. Filename-loaded SE / voice clips live on SE AudioPath B (not the "dead" path it was assumed to be); single-slot loader replaces the prior clip

Two distinct SE playback paths exist, and they target **different AudioPaths**:

- **Resource-baked SEs** (`FUN_00499c63`, the 110-entry `WAVE`-resource table)
  route to **SE path A** (`DAT_0964310c`). Their per-row +4 channel flag is
  all-zero in vendor data, so the path-B route + voice-stealing scan inside
  `FUN_00499c63` are dead (engine-quirk §46).
- **Filename-loaded SEs / voice clips** (`FUN_0049933c`) route to **SE path B**
  (`DAT_09643110`) with the SE-B slider (`DAT_056e577c`) volume. So path B is
  **not** dead overall — §46's "path B is dead" applies only to the *resource*
  dispatcher; the filename path uses it exclusively. This is the path the
  opening-cutscene voice lines play through.

`FUN_0049933c` is a **single-slot** loader: one segment (`DAT_09643034`) + its
play-state (`DAT_09642e78`). Each call `Unload`s + `Release`s the previous clip
before `LoadObjectFromFile`-ing the new one, so only one filename SE plays at a
time — a new dialogue line's voice cuts off the previous one. Segments get
`SetRepeats(0)` (play once) and `PlaySegmentEx(DMUS_SEGF_QUEUE=0x80)` on path B.
Missing files `MessageBoxA` "File not found" in the engine (the port logs to
stderr instead — no modal in headless runs).

The clips are loose `bin/se/.../*.bin` files in the install dir — plain
RIFF/WAVE PCM (16-bit/44.1 kHz/mono), `.bin` extension notwithstanding;
`LoadObjectFromFile` reads the RIFF header, not the extension. They resolve
against the `SetSearchDirectory(cwd)` set at `audio_init` (the game dir), same
as the BGM `.wma` loads — read at runtime from the retail install, never
redistributed.

The `.ivt` `se:<bin>` command (`IVE_OP_SE`, handler `0x46d885`) names the clip
by path; the dialogue tick fires `FUN_0049933c` the instant the command walk
reaches it (ret 1, same frame as the following `msg` line), so the voice lands
as the line appears. Port: `src/audio.c::audio_play_se_file` (mirror of
`FUN_0049933c`) reached from the pure-C interpreter via the `g_ive_se_play_fn`
bridge (like `g_music_swap_fn`). Verified end-to-end: the `intro-dialogue-lines`
port run loads + plays all 12 opening-cutscene voice/SE clips (`tea_mataku`,
`re_fue`, `piko`, … ×2 for the repeated `tea_mataku`) with zero load/play
failures.

## 88. SE PlaySegmentEx flag is 0x80 = DMUS_SEGF_SECONDARY (overlay), NOT DMUS_SEGF_QUEUE (which is 0x100) — the SE-inaudible bug

Both SE play paths — `FUN_00499c63` (resource SEs → path A) and `FUN_0049933c`
(filename/voice SEs → path B) — call `PlaySegmentEx` with `dwFlags = 0x80`. That
constant is **`DMUS_SEGF_SECONDARY`** (`dmusici.h`: SECONDARY = 0x80, QUEUE =
0x100, CONTROL = 0x200). SEs are *secondary* segments: they overlay whatever is
already playing on the AudioPath. Play one WITHOUT the SECONDARY flag and it goes
on as the **primary** timeline segment, which is not how a one-shot sound effect
sounds — in practice it produces no audible output alongside the BGM.

The port had both call sites coded as `DMUS_SEGF_QUEUE`, in the belief that
`0x80 == QUEUE`. The macro is **0x100**, so the port was sending the wrong flag
and *every* SE was silent (menu cursor/confirm beeps, dialogue voice lines) while
the BGM — a genuine primary segment on its own path — played fine. This is the
long-standing "SEs inaudible on the user's host" open issue (audio-backend.md):
the 2026-05-21 "revert to DMUS_SEGF_QUEUE (0x80) for engine fidelity" actually
swapped the audible `DMUS_SEGF_SECONDARY` (0x80) macro for `DMUS_SEGF_QUEUE`
(0x100). Fix: use `DMUS_SEGF_SECONDARY` at both `audio_play_se_win32` and
`audio_play_se_file_win32` — matches the engine's real 0x80 and restores audio.

Lesson: when a decompile shows a bare hex flag, resolve it against the SDK header
values, don't name it from memory — 0x80/0x100/0x200 (SECONDARY/QUEUE/CONTROL)
are easy to transpose.

## 89. Input auto-repeat lives in the button ring (FUN_004536cb), and its repeat-bit clear is UNCONDITIONAL — the cursor cadence is press, +13, then +5

The menu cursor's auto-repeat is NOT a menu feature — it's baked into the
per-frame input button ring `FUN_004536cb` (50380-50404), which derives two edge
masks per player from the held mask + a 16-entry per-bit counter array (record
stride 0x2a: held@0, prev@2, **pressed**@4 = `~prev & held`, **held/repeat**@6 =
`held` minus the gated bits, then 16 counters @8):

```
edge   = ~prev & held;            // DAT_073dddd4 — pure rising edge; A reads this
repeat = held;                    // DAT_073dddd6 — gated below; directions read this
for each of 16 bits:
  if (held^prev bit changed)  counter = 0xc;                 // press/release: arm to 12
  else if (counter < 1)       counter = 4;                   // reload → bit passes (pulse)
  else                      { counter--; repeat &= ~bit; }   // settling → gate the bit OUT
prev = held;
```

The two `else` branches are mutually exclusive, and the gate clear in the second
is **unconditional** — every frame the counter is `>= 1` it both decrements AND
clears the bit. So a held direction surfaces in `repeat` (DAT_073dddd6) on the
press frame (counter armed to 0xc, bit not yet gated), then the counter counts
`0xc → 0` over the next 12 frames with the bit gated, and the *only* re-pulse is
the frame it enters at 0 (reload-to-4, no gate). Net cadence: **press, +13, then
every +5** — single pulses. Confirmed by retail measurement (`runs/title-repeat`:
hold DOWN on the title menu, watch cursor `DAT_09643540` → moves at hold-frames
0, 13, 18, 23). The title/settings menus read `DAT_073dddd6` for UP/DOWN/LEFT/
RIGHT (so they inherit the repeat) and `DAT_073dddd4` for A (pure edge → no
repeat, one select per press).

**Port bug (fixed 2026-06-02).** `sim_button_ring_update` (the port of this ring)
had the gate clear as `counter--; if (counter > 0) clear;` — gating only while the
*post-decrement* value stayed positive. That let the bit through on the `1 → 0`
frame AND again on the next reload frame: a **double pulse** that repeated the
cursor ~2× too fast vs retail (the user's "the repeat rate is slower in retail").
The engine clears unconditionally; fixed to match. (A first, localized attempt —
a `scene_title_dir_fires` throttle in the menu — was wrong: the menu already reads
the repeat-gated `DAT_073dddd6`, so throttling there double-counted. Reverted in
favour of the one-line ring fix, which corrects every consumer at once.)
(Analog-stick → digital is a separate debounce path, `DAT_0438c14c = 0x1e`.)

## 90. The bottom-right "Fps NN" overlay is ON by default (`dispfps == 0` shows it) and is a benign timing divergence in captures

`FUN_004523e6` draws the debug FPS counter — a "Fps" label + up to two digit
glyphs sampled from `bmp/fps2.tga` (DAT_073d9fe0, 256×32) — in the bottom-right
corner (label at screen 594,468; digits at 616/624,462). It is called near the
end of every `FUN_004547ab` frame, **gated on `DAT_0438cce0 == 0`**, where
`DAT_0438cce0 = GetPrivateProfileIntA("setup","dispfps",0,…)`. The default is
**0 → counter shown**: a stock retail install (no `dispfps`, or `dispfps=0`)
displays it, which is why every retail house-walk-tables capture has "Fps 8x" in
the corner. `dispfps=1` hides it. (Counter-intuitive naming — the value is a
*disable*-when-nonzero flag, default-on.)

The displayed value `DAT_073de63c` is computed in `FUN_004547ab`'s tail (decomp
L51311-51324), AFTER Present: `frames_this_window * 1000 / elapsed_ms`, refreshed
once per ~second, where the clock is `timeGetTime()`. This is inherently
machine/timing dependent, so the **number never matches retail run-to-run** — a
benign environment divergence (the label glyph, position, and digit rendering ARE
1:1). The port (`src/scene1_fps.c`, C7p, 2026-06-02) drives the value off
`tick_now_ms()` (the tick virtual clock) instead, so under `--turbo` the value is
*deterministic* and capture goldens stay reproducible; in realtime it tracks wall
time like the engine. The turbo value reads low (~15) because turbo pumps several
sim ticks per rendered frame, so rendered-frames-per-virtual-second is below 60 —
expected, not a bug. Glyph layout in fps2.tga: "Fps" label at (0,0)-(23,12);
digit `d` at x = `d*0x12+0x21 .. d*0x12+0x31` (16 px wide, 32 px tall), drawn
right-shifted by 8 px per non-space char of the 2-wide formatted value.

## 87. Bottom-left "Merchant Level" HUD (`FUN_00409925` body) draws its frame with COLOROP=ADDSIGNED so a 0x7f-grey diffuse pulse idles at native brightness and *adds* a glow

The bottom-left HUD — the circular level-number badge, the gold "Merchant Level"
label, and the experience bar — is the body (decomp L124-L179 / asm
0x409cf0-0x409f6x) of `FUN_00409925`, the HOUSE-town HUD whose tail
(`LAB_0040a5fd`) is the "Button 4: Change Camera" hint. Three 192×40 layers from
`item_win.tga` (DAT_073d8748): a back frame (src (640,544)-(832,584)), the
experience-bar fill (src row y 592-632, width `clamp((xp-floor)/(next-floor),0,1)
*142` at dst x = ox+39), and a front frame (src (640,640)-(832,680)) over the
bar. The level number is the sub-helper `FUN_00481ec3` from a dedicated large-
digit row (src y 848-888, 32 px/digit, displayed as `level+1`).

**The quirk:** the three frame layers are drawn with `SetTextureStageState(0,
D3DTSS_COLOROP, D3DTOP_ADDSIGNED)` (asm 0x409d0b), then reverted to MODULATE
(asm 0x409f3e) for the digits. ADDSIGNED computes `result = Arg1 + Arg2 - 0.5`
= `texture + diffuse - 0.5`. The diffuse is a grey glow pulse
`gray = abs((int)(sinf(phase·π/30)·64)) + 0x7f` (range 0x7f..0xbf, phase
`DAT_0064827c` 0..29) built as `0xff000000 | gray·0x010101`. At rest (phase 0,
gray 0x7f ≈ 0.5) the frame shows at its **native** texture brightness; as the
pulse climbs during an experience gain it *adds* up to +0.25 — a breathing glow,
NOT a MODULATE dim. Reading it as MODULATE would render the gold frame at half
brightness. The `·64` multiply on the sin result is dropped by Ghidra (the
argless-trig pattern, [[feedback_argless_trig_decomp]]) and recovered from asm
0x409d66.

The decomp's apparent fourth quad (locals set at L167-L176, then only a flush)
is a dead store — asm shows exactly three `404efc` calls; that block's only live
effect is `local_14 = ox+16`, the level-number x anchor.

**Gating:** the body has NO internal dialogue gate. Its visibility == the top
HUD's — emitted by aggregator `FUN_0040a765` on the INGAME + HOUSE(stage 0) +
status-screen-closed path, then suppressed only by the screen-covering-cutscene
gate at `FUN_004547ab` (dialogue active AND `FUN_0046c869()` non-zero, §… the
1349470 top-HUD fix). So retail shows the badge in free-roam AND during the
iv1_2 dialogue over the live HOUSE (`intro-iv2-gap` golden-retail confirms it),
and hides it only during the iv1_1 screen-covering opening.

The "LEVEL UP!" pop drawn just below the badge is a separate helper
(`FUN_00407ab4`, gated `0 < DAT_0438b920`, the level-up animation counter that
the per-frame updater at decomp L4831 holds at 0 except during a level-up).

## 91. Back-window NPCs "stop & look": a leftward (dir==-1) mode==2 NPC pauses on a vthresh crossing — and the binary's crossing guard is ASYMMETRIC (rightward NPCs never pause)

The shop's back-window townsfolk (`scene1_bg_npc`, FUN_0046f2a3) are not pure
one-axis drifters: a subset stop and face the window for ~180 ticks, then turn
around. This is gated on a per-NPC threshold `vthresh` (+0x58) and `mode` (+0x5c):
a `mode==2` NPC that **crosses** its vthresh sets the pause counter (+0x54),
holds anim 3 while it counts up to 180, then clears it and flips drift direction
off one RNG bit.

**The crossing test is asymmetric in the binary** (objdump 0x46f4bf-0x46f559),
and it is a *real* asymmetry, not a decomp artifact:

- **dir == -1 (leftward):** the old-x compare (`ecx = old_x > vthresh`) is
  captured at 0x46f50e **before** the x update at 0x46f52f, so the guard is a
  genuine downward crossing `old_x > vthresh && new_x <= vthresh` → pause IS set.
- **dir == +1 (rightward):** x is updated at 0x46f4d1 **before** both vthresh
  compares (0x46f4dc and 0x46f502 both read the post-update value), so the guard
  reduces to `new_x < vthresh && new_x >= vthresh` — self-contradictory → pause
  is **never** set. Rightward NPCs walk straight past; only leftward ones pause.

So in retail you see townsfolk occasionally stop and look through the window,
but only while drifting one direction. The pause path consumes the shared LCG
exactly **once**, at expiry (the dir-flip coin at 0x46f44a), so it perturbs the
free-roam RNG stream — relevant to `scene1-rng-stream-parity.md`.

**Port bug fixed 2026-06-03:** the original port dropped the crossing check
entirely (an earlier note here wrongly called the pause path "dead" — it
confused the genuinely-dead +1 guard for the live -1 one). NPCs never stopped.
`scene1_bg_npc.c`'s drift branch now reproduces the asymmetric guard; the pause
branch (anim 3, count to 180, RNG dir-flip) was already correct, just never
reached. User-flagged from the README hero ("NPCs in retail occasionally stop to
look through the window, which we don't"). Locked by
`tests/test_scene1_bg_npc.c::test_bg_npc_leftward_crossing_pauses`.

## 92. Free-roam sprites (chars, dust, wing-glow) are FULL camera-facing billboards (`DAT_0438cdf8`) → each quad has CONSTANT view-depth = its anchor's depth

`FUN_004424e7` builds the shared billboard orientation `DAT_0438cdf8 =
RotationX(π/2 − camElev) · RotationY(camAz + π)` once per frame from the
camera→target vector. Every free-roam sprite billboard (char/companion/NPC via
`FUN_0045a56f`, foot-dust + wing-glow via `FUN_004176ff`) is built as
`RotZ(rot)·Scale(s)·DAT_0438cdf8·Translate(anchor)` and its **local quad is at
Z=0** (`FUN_0045a56f` writes `0.0` to all four vertex Z slots). Verified against
the retail d3d trace (`runs/walkdust-d3d` f5495, WORLD·VIEW): the char billboard's
local-Y world-direction is `(0, +0.5547, −0.8321)` and the VIEW pitch rows are
`(0,0.5547,0.8321)/(0,−0.8321,0.5547)`, so moving up the sprite changes view-Z by
`0.5547·0.8321 + (−0.8321)·0.5547 = exactly 0`. **The quad is perpendicular to the
view axis → the whole sprite (head to feet) sits at ONE depth = the depth of its
anchor translation.** Consequence for occlusion: the player/companion sprite is
anchored at the **feet** (`FUN_004552d0` standing path `(px,py,pz)`; `FUN_00456f56`
free-roam path `(actor.x, actor.y, actor.z+0.02)`), while foot-dust is emitted at
**`py+0.5`** (engine-quirks dust recipe, [[scene1-walk-dust]]). Higher world-Y is
nearer a down-tilted camera (~`0.42` view-units per `0.5` Y), so **fresh dust at the
current feet is always NEARER than the feet-anchored char quad and draws in FRONT —
in retail too**; the body only occludes dust that has **drifted behind the walker's
feet-plane** in world Z (confirmed in the retail trace: NDC-z has fresh dust in
front at f5495–5528, trailing dust behind at f5597+). So "the body should hide the
puff at her feet" is NOT how retail works — see `docs/findings/scene1-walk-dust.md`
§2026-06-03b.

## 93. Billboard depth layering is done by swapping the PROJECTION z_far per pass — two billboards at the SAME world position get DIFFERENT NDC depth purely from the active projection

The scene-1 renderer controls which billboards occlude which **not** only via
draw order + Z-state, but by **swapping `D3DTS_PROJECTION` between passes** — each
pass calls the perspective build (`scene1_render_push_projection`, engine
`FUN_00459dfd`/`FUN_004552d0`) with a **different `z_far`** while near stays `1.0`.
Because `ndcz = z_far·(d−1)/(d·(z_far−1))` depends on `z_far`, the **same world
point projects to a different NDC-z under a different `z_far`** — a larger `z_far`
maps a given depth NEARER (smaller ndcz). Measured live (house free-roam, synced
d3d-trace):

| pass | z_far | PROJ[10] |
|---|---|---|
| room/near meshes | 350 | −1.00287 |
| **char body** (`FUN_004552d0`) | **1450** | −1.00069 |
| wide scenery / chr **glow** / dust | 2000 | −1.00050 |
| (close-up override) | 1100 / 3025 | … |

The char-body `z_far` is computed in `FUN_004552d0`: `z_far = 2200 −
(local_14 − 11)·75`, `local_14 = DAT_0438b778 + DAT_044e2c70`. In HOUSE free-roam
`DAT_0438b778 = 0` and **`DAT_044e2c70 = 21.0`** (the camera eye.y-add constant,
`.rdata DAT_005c4fd8`), so `local_14 = 21 → z_far = 1450`. The body therefore lands
**farther** in NDC than the additive wing-glow drawn under `z_far = 2000`, so the
glow (ZWRITE=0, ZFUNC=LESSEQUAL) passes the depth test and **draws over her head**
— this is what produces Tear's blue-washed hair/face. If the body shared the glow's
`z_far` (or a larger one) the glow would be occluded behind her.

So in retail, **billboard occlusion is partly governed by the per-pass projection,
not only by draw order + Z-state**: two billboards at the same world XYZ and scale
land at different NDC depth purely from which `z_far` was live at each draw (larger
`z_far` → nearer in NDC). The char body sits at `z_far` 1450 — between the near
meshes (350) and the effect layer (2000) — so the additive effects (glow, sparkles)
read in front of her while she still occludes the room geometry behind. The dust
layer (also `z_far` 2000) is governed the same way.

(How this manifests as a *port divergence* — and how to recognise/diagnose it when a
stub feeds the wrong `z_far` — is written up port-side in
`docs/findings/scene1-tear-visual-diffs.md` #1, not here; this section records
retail behaviour only.)

## 94. The per-scene phase counter DAT_056db054 is FROZEN through the intro video — it only ticks while the HOUSE per-frame open runs

`DAT_056db054` (the per-scene counter that drives the companion hover-bob
`sin(db054·0.04)·0.2`, the every-4th-frame wing-sparkle emit, and the §83/§81
periodic spawns in `FUN_00483e7b`) is **reset to 0 in `FUN_00436f97`** (the HOUSE
stage-position setup) and incremented **+1 per frame** only by the HOUSE per-frame
drivers — `FUN_0048b850` (free-roam player tick, L490) and `FUN_0048407f` (the
conversation-pose driver, L84658). It is **NOT** incremented during the
`recet_op.wmv` intro video or the loading screens that precede the HOUSE scene
(the HOUSE sim isn't pumping those frames).

Ground truth (retail Frida `--watch db054=0x056db054`, `runs/tear-phase/retail`,
new-game→HOUSE on the `house-walk-down-dense` trace):

```
BOOT@0  NEW_GAME@71  LOADING_START@71 ........ (intro video, db054 == 0) ........
CONV_POSE_START@1683  ← db054 first becomes >0 here (HOUSE per-frame open starts)
LOADING_END / HOUSE_FREEROAM@1725  ← db054 == 43  (only 42 frames of conversation)
... thereafter db054 == (frame − 1683), a clean +1/frame monotone with no resets ...
```

So at any HOUSE moment retail's `db054` equals *the number of HOUSE-active frames
since the conversation began* — the ~1610 frames retail spends in the intro video +
load contribute **nothing**. This is the retail-side fact behind the port's
Tear anim-phase divergence (the port skips the video, so its db054/companion-anim
phase is offset at free-roam — that port consequence is written up in
`scene1-tear-visual-diffs.md` §"#3/#4 determinism verdict", not here).

---

## 95. The INGAME sim burns one LCG step EVERY frame on an invisible dev coordinate readout — a fingerprint on the determinism stream

> **RE-CORRECTED 2026-06-05 — the step is UNCONDITIONAL after all (the
> 2026-06-04 "movement-gated" correction was WRONG).** The original write-up
> ("+1 LCG step every INGAME frame, forever") was right. The 2026-06-04 revision
> mis-read a `house-idle rngcalls DESYNC` as "retail consumes 0 when idle" and
> movement-gated the port — but that idle measurement was **confounded by the
> un-pinned background-window NPCs**, which share this very LCG and were freely
> desyncing port↔retail until the 2026-06-05 bg-NPC `{phasepin}` landed. With the
> NPCs pinned to a shared RNG origin, a clean `house-idle-npc-drift --target both`
> rng-callsite drill shows retail consuming the overlay step (caller ret
> **`0x443606`**) **once on EVERY idle frame** (per-frame tally = 1.000/frame over
> 300 idle frames; the player is confirmed idle, vx=vz=0) — and the decompile tail
> is unconditional (`LAB_004435f7` is reached on every path; the gates above it
> only choose whether `FUN_004427f1` runs first). Two independent ground truths —
> the direct LCG-hook caller tally and the decompile — agree: **unconditional.**
> The 2026-06-04 doc explicitly noted "the decompile tail reads as unconditional"
> yet trusted the confounded observation; lesson logged. Port consequence + fix:
> `docs/findings/freeroam-rng-consumption.md` Lead C.

At the very tail of `FUN_00442cef` (the default-running INGAME arm, decompile
`442cef.c` L417-429, asm `0x4435f7`-`0x44364b`), after the table-A/B/C ticks,
the engine runs a **developer coordinate overlay** (its LCG step UNCONDITIONAL —
`thunk_FUN_005041f6()` at `LAB_004435f7`, ret `0x443606`, past every gate):

```c
FUN_0040fb3a();                 // table-A particle tick (= scene1_particles_tick)
FUN_004426a7();                 // (consumes no RNG)
uVar4 = thunk_FUN_005041f6();   // <-- ONE raw LCG step, every frame
FUN_005038ff(buf, "%d",   uVar4);          FUN_00451874(0,4,buf);  // the rng value
FUN_005038ff(buf, "X:%f", DAT_056da1d8);   FUN_00451874(0,4,buf);  // player X
FUN_005038ff(buf, "Y:%f", DAT_056da1dc);   FUN_00451874(0,5,buf);  // player Y
FUN_005038ff(buf, "Z:%f", DAT_056da1e0);   FUN_00451874(0,6,buf);  // player Z
```

`thunk_FUN_005041f6` is the bare `jmp 0x5041f6` at `0x471084` — the raw LCG
(not the `& 0x7fff` unit wrapper `FUN_00471089`). `FUN_00451874` just copies the
formatted chars into the debug text-grid at `DAT_06a47aac` (80 cols × rows 4-6);
**that grid is never drawn in the retail Steam build**, so the overlay is
invisible. But the LCG state `DAT_006023a0` is advanced once per frame regardless,
and this is the **last** RNG consumer of the tick.

Why it matters: it's a textbook "observation changes the system" quirk. A feature
that renders *nothing* still leaves a deterministic fingerprint — +1 LCG step per
INGAME frame, forever. Any determinism comparison that omits it desyncs the entire
shared RNG stream by one step per frame, so every *downstream* RNG-driven position
(foot-dust jitter, wing-sparkle, ambient particles) drifts even when each emitter's
own logic is bit-exact. We found it the hard way: with the companion anim counters
and the periodic emitters all verified bit-exact, `phase_probe`'s `rngcalls` still
showed a steady −1/frame port deficit (first visible at db054≈37, the frame the
trace's walk begins). The culprit was this single invisible `%d`/`X:%f` readout.

Port note (not part of the quirk, for cross-ref): `src/scene1_sim.c` consumes the
step **unconditionally every INGAME frame** (`scene1_debug_overlay_consume_rng()`
→ `(void)rng_next15();` at the default-arm tail) and renders nothing, matching
retail. Verified end-to-end: with the bg-NPCs pinned to a shared RNG origin, a
clean `house-idle-npc-drift --target both` keeps the window NPCs **bit-locked
across a 260-frame idle window** (≤69 px @ mean 0.00 at every offset; before the
fix they desynced at +100 → 2971 px @ mean 1.42 at the first post-desync respawn).
Both `house-walk-down-dense` AND `house-idle` stay `rngcalls`-ALIGNED. See
`docs/findings/freeroam-rng-consumption.md` Lead C.

**Seeing it on retail (confirmed 2026-06-04).** The grid IS drawn by
`FUN_00451ea7`, gated at its call site by `if (DAT_06a49938 == 1)` — and
`DAT_06a49938` is the same debug-menu activation gate `debug_param_tick.c`
documents as BSS-zero in normal play. Force it on with the Frida `{poke}`
segtrace op (sticky u32 write):

```
{"poke": [1545, 111450424, 1]}    # 111450424 == 0x06a49938, after HOUSE_FREEROAM
```

(headless, in a capture). To turn it on for *interactive* play / clip recording,
launch the game normally and attach with `tools/dev_overlay.py` (holds
`DAT_06a49938=1`; `--full` also sets `DAT_06a4993c=1` for the verbose hex-dump).

and the overlay appears: the live player coords render as
`X: -0.300000 / Y: 0.000000 / Z: 9.500000` (= `DAT_056da1d8/dc/e0`), the per-frame
`%d` rng value on the row above, plus a debug tile-grid top-right. Visual pushed to
the llm-feed (2026-06-04). So the quirk isn't theoretical — it's a real, fully
wired dev HUD the retail build simply never flips on.

The grid `DAT_06a47aac` is a **shared** debug text-buffer: `FUN_00451874` (the
80-col char-grid writer) is called from ~15 sites across subsystems, so the live
overlay also shows event-script state, message/dialogue counters, a `muteki`
(無敵 = "invincible") debug-godmode flag, free-texture/handle counts, etc. — each
subsystem scribbles its own row. Only the X/Y/Z+rng row is traced here (it's the
one that touches the determinism stream via §95's per-frame LCG step); the rest is
a grab-bag dev readout. The labels render through the engine's 8×8 **ASCII** font
atlas (so the JP fields show as romaji).

---

## 96. The hidden debug menu has a 1024-bit save-flag editor — a built-in story-flag cheat that writes straight into the save

Riding on §95's dev overlay (enable with `DAT_06a49938=1`; the full menu with
`DAT_06a4993c=1`, e.g. `tools/dev_overlay.py --full`) is a **bit-grid flag
editor**. `FUN_00451ea7`'s `DAT_06a4993c==1` branch renders a 16-wide × 64-tall
grid of `%02X` cells, and the menu input handler (`FUN_004518a3` neighbourhood,
asm `0x4523xx`) edits it:

```c
// d-pad moves the cursor over the grid:
DAT_06a49944 = column (0..15)      // left/right, mod 0x10
DAT_06a49948 = row    (0..63)      // up/down,   mod 0x40
// the action button (buttons & 0x10) toggles the selected cell:
flag[row*0x10 + col + slot*0x2dfc8] ^= 1;   // XOR the low bit
```

So it is a **1024-entry boolean bank** (`DAT_0450f3e0`, i.e. offset `+0x2bc48`
inside the active save slot `DAT_044e3798 + DAT_0438b1e0*0x2dfc8`), and "setting a
bit" is a one-bit `^= 1` toggle — the `%02X` readout is really just `00`/`01` per
cell.

**These are the game's event / story-progression flags.** At three independent
gameplay sites the bank is read with the canonical scenario-gate pattern — flag
*indices* come out of a script/scenario record, prerequisites are checked, then a
completion flag is set:

```c
// FUN_0045de68 (scenario condition eval), also FUN_00450877 / FUN_00462403:
if (flag[rec[-0x2f]] == 0 && flag[rec[-0x2e]] != 0 && ...)   // prereqs
    flag[rec[-0x2f]] = 1;                                    // mark event done
```

So toggling cells artificially advances/gates story events (scenes seen, NPCs
met, dungeons/recipes unlocked, tutorial steps, …). A few cells are dev toggles
rather than story flags — e.g. cell **[0]** is read in `FUN_00489c79` (a combat
hit calc) and forces a value to 1 when set. The bank is read almost entirely
through accessors (only 4 raw references to `DAT_0450f3e0` exist in the whole
image), which is why individual cell *meanings* aren't greppable; the mechanism,
though, is certain.

Two gotchas: it edits the **save-slot struct**, so anything toggled **persists if
you save that slot** (it's a save editor, not a RAM cheat); and it acts on the
**active slot** (`DAT_0438b1e0`). To map specific flags: toggle a cell, save, and
diff the save file, or hook the scenario accessor (`FUN_0045de68`) and watch which
indices each event checks. Sibling debug-menu pages (`FUN_00451b07`, cursor modes
in `FUN_004518a3`) edit other save fields directly — a PIX/money editor (caps at
9,999,999, ±100/1000/10000 steps), a chapter/progress counter, and a per-adventurer
stat editor (it iterates the party records — the `Louie` name string is right
there in the table).

## 97. Integer colour bytes appear in the decompile as tiny-denormal `float` literals — bit pattern IS the value

In `FUN_0049c644` (title menu render) the unselected menu-item brightness is
written as the float literal `1.33123e-43` and the selected-item white clamp as
`3.57331e-43`. These are not real magnitudes — they are **denormalised floats
whose 32-bit pattern, reinterpreted as an integer, IS the colour byte**:

```
bits(1.33123e-43) = 0x0000005F = 95   → unselected grey 0x5F
bits(3.57331e-43) = 0x000000FF = 255  → selected clamp  0xFF
```

The slot (`local_8`) is a `float` only in Ghidra's type inference; the engine
stores a small integer there and later truncates it back (`__ftol`) when packing
`0xff000000 | b<<16 | b<<8 | b`. So when a decompiled colour/byte constant shows
up as a `e-43`/`e-44`-scale float, **read its hex bit pattern, not its decimal
value** — `python3 -c "import struct;print(hex(struct.unpack('<I',struct.pack('<f',1.33123e-43))[0]))"`.

This bit it once: the unselected grey was ported as `0x95` (a hex/decimal slip —
95 decimal is `0x5F`, and `0x95` is 149), which over-brightened every unselected
title-menu item. Ground truth (`render_quad_add` diffuse = `0xff5f5f5f`) and the
denormal both say `0x5F`. The same denormal-as-byte idiom recurs across the
engine's UI colour code, so expect it elsewhere in the menu/HUD render functions.

## 98. Title 2D render batches same-texture quads into one DrawPrimitiveUP; standalone images flush per-quad

`render_quad_add` (FUN_00404efc) is a **pure vertex appender** — it writes 6
verts into the shared buffer at `DAT_00647e0c` and bumps the count; it never
flushes. Batch shape is therefore entirely **caller-driven** by where the code
calls the flush (FUN_00405354, which `DrawPrimitiveUP`s `vcount/3` tris and
zeroes the count).

The title scene (FUN_0049c644) is deliberately **inconsistent** about this:

- **Standalone background images** (title03 BG, the gradient, the fuki corner
  strip, the title01 band) are each a *distinct texture*, drawn `add` → `flush`
  → one `vcount=6` DrawPrimitiveUP per image.
- **The menu-item row** (N items from the single `fuki` sheet) is drawn as N
  `add`s then **one** `flush` → `vcount = N*6` (e.g. 4 items → `24`). This batch
  runs under `COLOROP = ADDSIGNED`; the flush fires right before COLOROP is
  restored to `MODULATE`.
- **The selected-row decoration tiles** (frame + big label glyph + ribbon, 3
  quads, also all from `fuki`) are likewise one batch → `vcount=18`, under
  `MODULATE`.

So a stock boot title frame emits flushes `[6, 6, 6, 6, 24, 18]`. The grouping
is by texture+state continuity, NOT "one draw call per sprite" — binding the
same texture before each add inside a batch is a redundant no-op the engine
relies on (last bind wins for the whole DrawPrimitiveUP window). Pixel-identical
to per-quad flushing; this is a command-stream structural fact, used as a
frame-by-frame parity check (`flow_diff` vcount field on FUN_00405354).

## 99. Title selected-item brightness pulse: scale constants are −128.0 / −32.0, sin float32-rounded before the scale-multiply, two independent truncations

The title menu's *selected* item (`FUN_0049c644 @ 0x49c8ce..0x49c95d`) is a
greyscale quad whose brightness byte pulses every frame. The engine builds it
from two independent sine terms, each computed in x87 then truncated to int:

```
        sa   = (float) sin( select_phase        * 3.1415927 / 15.0 )   ; engine sin = x87 fsin
        term1 = 0x7f - ftol( sa * SCALE1 )        ; SCALE1 @ .rdata 0x519468 = -128.0
        sb   = (float) sin( (pulse_phase % 45)   * 6.2831855 / 45.0 )
        term2 = 0x20 - ftol( sb * SCALE2 )        ; SCALE2 @ .rdata 0x519820 =  -32.0
        v     = min( term1 + term2, 0xff )        ; greyscale: 0xff000000 | v<<16 | v<<8 | v
```

Three details that matter for bit-exact parity, all confirmed against the binary
and live retail (Frida) on the `title-z-press` select countdown:

- **The select scale is −128.0, NOT −127.** Easy to misguess as 127 (the byte
  range max), but the constant in `.rdata` is `0xC3000000` = −128.0. The idle
  (`pulse_phase`) scale is −32.0 (`0xC2000000`). Because `trunc(-x) == -trunc(x)`,
  `0x7f - ftol(sin*-128)` is identical to `0x7f + ftol(sin*128)`. The off-by-one
  in the scale only changes the output on frames where `sin*scale` lands across an
  integer boundary — e.g. `sin(0.4π)·127 = 120.78` (→120) vs `·128 = 121.74`
  (→121), a **1-LSB** brightness difference (249 vs 250) visible on at most one
  frame of a ramp. The rest of the ramp is identical either way, which is exactly
  what makes the wrong constant survive casual side-by-side inspection.
- **sin is rounded to float32 BEFORE the scale-multiply.** The asm does
  `fstp [local]; fld [local]` between the `call sin` and the `fmul SCALE` — i.e.
  the 80-bit `fsin` result is collapsed to a 32-bit float, then the multiply runs.
  (`sinf()` in a port already returns float32, so a port matches this for free.)
- **Two SEPARATE truncations**, with `0x7f` and `0x20` added as *ints* outside
  each `ftol` — not one `ftol` of a combined float sum. (Contrast the unrelated
  pulse at `0x49cd00`, which adds its base of 127.0 *inside* the `ftol`.)

The render reads `select_phase`/`pulse_phase` from `DAT_09643544`/`DAT_0964352c`
AFTER `scene_title_sim` has incremented them for the frame, so the render-time
values are +1 vs whatever a sim-`onEnter` probe logs (Frida-confirmed: at the
frame the sim logs `(5,35)`, the render consumes `(6,36)`).

## 100. The shared menu hand-cursor bob counter DAT_0438b154 free-runs from boot with no reset — a load-dependent (§85) phase that must be {phasepin}'d

`DAT_0438b154` is the phase for the shared menu hand cursor's horizontal wobble
(`FUN_00435747`: `bob = |sin(b154·0.1)|·8`, period ≈ 62.8 frames). It has exactly
**one writer** in the whole binary — the `+1` in `FUN_004356cd` — and **no reset
anywhere** (BSS-zero at boot, never zeroed). So its value at any frame equals the
total number of frames since boot on which the save dialog was closed
(`DAT_0438b148 == 0`, i.e. almost always).

`FUN_004356cd` is called once per frame by whatever scene is active:
- **title** — `FUN_0049a59e` (the title sim) every title frame;
- **INGAME (state 1)** — `FUN_00406584`, which `FUN_004536cb` runs every state-1
  frame right after `FUN_004427d3` (`Cs3`);
- the modal/transition path (`DAT_06a49998 != 0`) runs `FUN_0047fa76`/`FUN_0048f931`,
  which also tick it.

So the cursor bobs continuously across the whole intro/prologue, not just while a
menu is open. Consequence for parity: because b154 accumulates through the
non-deterministic load (and the intro video the port skips — §85), its **absolute
value** at a given anchor differs between port and retail by a non-reproducible
constant, so the cursor bob position at a fixed-offset capture diverges (~7 virtual
px) even though the bob *formula* and the slide are bit-1:1. The fix is the same as
the companion db054 (§94): a `{phasepin}` op zeroes b154 at a deterministic
post-load anchor (`title_save_dialog_phasepin` / the Frida agent), after which it
increments identically on both sides and the hand cursor is bit-1:1 (user-confirmed,
`intro-skip-prompt`, 2026-06-05).

## 101. The LOAD GAME slot-picker (FUN_0049b556): cards under ADDSIGNED, two text scales, playtime in frames

The continue/load slot picker draws its save cards as a **horizontally-paged**
grid from `item_win.tga` (`DAT_073d8748`): each visible column is 3 slots tall;
DOWN/UP step ±1 within a column (a vertical row-slide), LEFT/RIGHT step ±3 between
columns (a horizontal page-slide). The renderer draws the centre page plus its two
off-screen neighbours (only once the open-anim `DAT_09643520` is fully ramped, >9)
so the page-slide has cards to move in.

Retail behaviours worth pinning:
- **Cards + all card content draw under `D3DTOP_ADDSIGNED`** (COLOROP=8 set once up
  front); only the scroll arrows switch to MODULATE. So the per-card greyscale
  diffuse is an *additive* bias on the parchment art: the **selected** card uses
  brightness `sin(anim·0.1)·32 + 159` (a gentle shimmer, plus a confirm-flash
  `sin(pulse·π/30)·128` while the A-press countdown `DAT_0964351c` ramps to 0x1e),
  unselected cards a flat `0x5f` (95) — so the selected card reads brighter than
  the rest. (New-game-overwrite mode darkens by 0x40 / drops to 0x20.)
- **Two text scales on one card.** The big slot number (`%03d`) and the empty-slot
  `NO-DATA` draw at scale **1.0** (`fld1`); the SCORE/LOOP/TIME label+value columns
  and the game-mode tag draw at scale **0.8** (`.rdata 0x519470`). (The sine/scale
  brightness constants are Ghidra-dropped FPU loads, the §97/§99 class — recovered
  from objdump: `0x5193a0`=0.1, `0x519474`=32, `0x519d98`=159, `0x519468`=−128.)
- **Saved play-time is stored in FRAMES at 60 fps** (bank dword 2, which doubles as
  the slot's "occupied" test): the card's `TIME %3d:%02d:%02d` is
  `frames/216000 : (frames/3600)%60 : (frames/60)%60`.

Port `title-load-picker --target both` is pixel-1:1 (0 diff px on the slide-in
frame; on settled frames 1 px > 16 LSB — a sub-pixel rounding on the rotated
clock-dial hand). User-confirmed effectively-1:1 with the 1-px notice, 2026-06-05.

**Residual sub-LSB noise (deferred — `[[2d-ui-scaling-filter]]`).** At the LSB
level the settled picker has 947 px (0.12%) differing by ≤4 LSB (none >4) — the
same imperceptible 2D-UI texture-scaling / colour-precision noise the user flagged
on the skip-prompt gold banner and the dialogue box edge (the `ledger #52`
box-edge-halo class, cf. §54 POINT-vs-LINEAR / box-mip). Almost certainly ONE
shared root cause across the 2D-UI quads (the sampler/mip filter on `item_win.tga`
+ the menu textures), worth investigating once as a likely-easy shared fix
(user-flagged 2026-06-05, next session).

**Sharpened lead (user, 2026-06-05):** the title MENU items (`fuki.tga` tiles) are
**0-px** bit-identical, while the picker cards carry the noise — and the
distinguishing factor is **scaling**. Where a 2D quad's dst pixel size equals its
src region size (texel-to-pixel 1:1), POINT and LINEAR sample the same texel
centres so there is no ±1 LSB divergence; the noise appears only on the **scaled**
elements (the clock-dial detail panel, the 0.8 stat text, anything magnified/
minified). So the next-session probe should compare the sampler filter + the
half-texel/UV-origin handling specifically on SCALED 2D-UI quads (port vs retail)
rather than the unscaled ones — that is where the rounding diverges.

## 102. Title submenu back-out: the engine never clears the submenu id (DAT_09643524) — it slides the panel off-screen via the cursor_anim render-gate, and resets select_phase to 0 on the cancel

When you back out of a title submenu (the LOAD GAME slot picker `DAT_09643524==1`,
or the settings panel `==2`), `FUN_0049a59e` does **not** clear the submenu id.
The picker's B/cancel (`DAT_073dddd4 & 0x20`) jumps to `LAB_0049aaff`, plays the
cancel SE `0x13d`, and falls out of the `DAT_09643520==10` block to the shared
fall-through at the end of the function (`L101197`):

```
DAT_09643544 = 0;   // select_phase  → 0   (was pinned at 0xf from the open-countdown)
DAT_09643528 = 1;   // slide dir     → fold-out (DAT_09643520 decrements toward 0)
```

So **two** things happen on a back-out, and a third deliberately does *not*:
- **`select_phase` (DAT_09643544) is reset to 0.** It was left at 0xf when the
  A-press countdown dispatched the open. The main-menu input handler only runs in
  the `DAT_09643544 < 1` branch, so without this reset the menu would never accept
  input again. (This is the bug the port hit — see below.)
- **The slide direction flips to fold-out** so `DAT_09643520` (cursor_anim) ramps
  10→0, sliding the panel back toward x = `640 − cursor_anim·64` = 640 (off the
  right edge).
- **The submenu id `DAT_09643524` is left set** (stays 1/2 until the next dispatch
  overwrites it). It is *not* a state flag the engine bothers to clear: the panel
  render is gated purely on `DAT_09643520 >= 1` (`FUN_0049c644` L101986/102014), so
  once cursor_anim hits 0 the panel simply isn't drawn (and at cursor_anim==0 it
  would draw fully off-screen anyway). Net effect: **submenus SLIDE off-screen on a
  back-out, they do not pop**, and the main menu re-accepts input on the frame
  cursor_anim reaches 0.

Settings exits the same way through a different door (`DAT_09643560==2/3` →
`LAB_0049a5d3`, L100574: same `DAT_09643544=0; DAT_09643528=1`).

Port note (not retail behaviour, recorded here only to explain the parity choice):
the port's main-menu input gate keys on `submenu_state == 0`, so the port *does*
clear `submenu_state` — but only once cursor_anim reaches 0 (in the slide tick),
keeping it set through the fold-out so the panel slides off-screen like retail.
Verified `title-picker-cancel --target both`: sim state (`select_phase`,
`cursor_anim`, `cursor_pos`, `submenu_state`) bit-1:1 through the fold-out; the
post-back-out main menu + a DOWN press are 0-px bit-identical to retail. The only
residual is the benign steady-state `submenu_state` (port 0 vs retail's stale id) —
no render/input effect because the render gate is cursor_anim-gated. This fixed the
"LOAD GAME → X-back soft-locks the main menu" bug: the port had reset
`submenu_state`/`menu_folding_out` on cancel but left `select_phase` pinned at 0xf,
so the main-menu gate fell into the no-op countdown branch forever.

## 103. The dialogue box open/close "wobble" (FUN_0046c86f) is a SIN squash-and-stretch, not cos — at the settled box (box_open=15) the scale is exactly 1.0/1.0

`FUN_0046c86f(box_open n, *sx, *sy, *alpha, closing)` computes the dialogue
box's squash-and-stretch scale as the box-open counter `DAT_073a3e14` ramps
0→15 (one step/frame while a line is shown). The wobble term calls
**`FUN_00503a44` = `sinf`** (NOT `FUN_00503994` = cosf — the two FPU trig thunks
are easy to swap; `FUN_00503a44` is sin throughout the corpus, see
scene1-char-sprite-render / scene1-table-b-allocators / scene1-particles-tick):

```
amp   = n<6 ? 0.8 : n<11 ? 0.3 : n<16 ? 0.1 : 0.0
lim   = min(n*0.2 + 0.4, 1.0)
s     = sinf(n * 9.424778 / 15)        // 9.424778 = 3π; angle = n·36°
sx    = s*amp*0.125 + 1.0
sy    = (1.0 - s*amp*0.125) * lim       // = (2 - sx)·lim   ⇒ sy ≈ 2-sx for n≥3
alpha = min(n*0x56, 0xff)
```

The **sin** choice is load-bearing for parity: at the fully-open box (n=15) the
angle is exactly 3π, so `sin(3π)=0` ⇒ **sx=sy=1.0** — the settled box sits at its
natural full size. (cos would give `cos(3π)=-1` ⇒ sx=0.9875/sy=1.0125 — a box
~5px narrower + a few px shifted in X, since the box X is centred via
`local_20 = (local_c+208) - sx·208`. That offset rippled through the box frame,
nameplate, AND glyph text, mismatching retail by ~116k px/line on the iv1_1
bedroom lines — the long-standing dialogue box-edge "halo".) The ramp produces a
visible overshoot-then-settle: n=2→sx1.0951, n=3→1.0588, n=4→1.0, n=5→0.9779,
n=6→0.9645 … damping to 1.0 by n=15.

`closing` (= `DAT_073a6a38 < 0`, no current line) is a separate branch that
overwrites sx=1.0 and `sy = max(1 - (15-n)·0.15, 0)`, `alpha = max(n·0x32-0x1ef,
0)` — a vertical shutter, independent of the sin wobble.

Measured on retail (`runs/dlg-box-watch2`, Frida watch of `DAT_073a3e14` +
`--d3d-trace-verts`, box quad width decoded per frame): the box-width sequence
over a line's open-ramp matches the **sin** table frame-for-frame and matches
**no** cos value. `box_open` itself is bit-1:1 port↔retail at every captured line
(0/46 over `intro-dialogue-lines`, END+2 capture), so the only divergence was the
cos/sin swap; fixing it (`src/scene1_dialogue_run.c ive_box_scale`) makes the box
scale 1:1 (cap_00 vs retail 116539px → 856px, the 856 being the benign stale-
golden FPS corner — see benign-divergence-registry).

## 104. The ESC skip-prompt box (FUN_0043537e) draws its banner AND text under D3DTOP_ADDSIGNED (=8), not MODULATE2X — a near-zero signed offset so the 0x7f7f7f vertex colour reads as the TRUE texel, resetting to MODULATE only after the text

`FUN_0043537e` (the "Do you want to skip this event?" Yes/No choice box) sets
`SetTextureStageState(0, D3DTSS_COLOROP, 8)` once at the top (`0x43537e`+offset,
right before binding `savewindow.tga`), draws **banner + prompt text + Yes/No**
all under that COLOROP, then resets `SetTextureStageState(0, COLOROP, 4)`
(MODULATE) at the very END (L32830) — so the **text is under ADDSIGNED too**. The
cursor (`FUN_00435747`, a separate call after `FUN_0043537e` returns) therefore
draws under MODULATE.

`8` is **D3DTOP_ADDSIGNED** (`Arg1 + Arg2 − 0.5`), NOT `D3DTOP_MODULATE2X` (which
is enum **5**). The two are trivially mis-mapped (a port once read the decompile
value `8`, labelled it "MODULATE2X", and coded the *name* `MODULATE2X` = 5). They
are NOT equivalent at the vertex colour the box uses:

```
vertex diffuse = 0x7f7f7f (per channel 127/255 ≈ 0.498)
MODULATE2X (5): texel · 0.498 · 2 = texel · 0.996   → texel − ~1 LSB (DARKER)
ADDSIGNED  (8): texel + 0.498 − 0.5 = texel − 0.002  → ≈ texel  (TRUE brightness)
```

So the 0x7f7f7f colour is a **near-zero signed bias** under ADDSIGNED (draws the
texture at its true brightness), but a **0.996 dimmer** under MODULATE2X. The
MODULATE2X mis-port made the gold banner exactly 1 LSB dark (`[237,200,52]` vs
retail `[238,201,52]`); drawing the *text* under MODULATE (the port reset COLOROP
before it) left a matching ≤1-LSB halo on the glyph anti-aliased edges — visible
*only* here because all other engine text is MODULATE+white on both sides.

Verified on `intro-skip-prompt --target both`: the COLOROP set sits in the trace
immediately before the `savewindow.tga` bind on BOTH sides — port emitted `5`,
retail `8` — with the banner verts/UVs/diffuse otherwise **bit-identical** (so it
was never a scale/phase/TGA-precision issue, just a wrong render-state). Fixing
both the banner op AND the text path (`src/choice_box.c`: ADDSIGNED for the
banner, keep it through the text drawn at 0x7f7f7f, reset to MODULATE after)
makes the entire box+text+cursor region **0 px** vs retail (was 92k ≤1-LSB px in
the banner bbox). The residual full-frame delta is the prologue background's §85
phase only. **Lesson:** for a flat-colour ≤1-LSB UI divergence with bit-identical
verts, suspect the COLOROP/blend render-state (and the enum-value-vs-name trap)
before the texture decode.

## 105. The dialogue `rmb:a,b` command is a per-standee + bg screen-shake: while a countdown is live, each DRAWN standee's Y (and the scroll-bg's Y) jitters by `(rand()&0x1f)-16` every frame — a per-frame RNG offset, NOT a uniform screen shake

The `.ivt` `rmb:a,b` command (handler `FUN_0046d926`) arms two independent
shake countdowns: `DAT_073a6d98` (bg) `= a+1`, `DAT_073a6d9c` (chr) `= b+1` (the
`+1` is applied by the command parser, so `rmb:0,0` shakes for one frame). The
per-frame dialogue tick `FUN_0046c320` decrements each (once per internal step,
while `> 0`). The DRAW `FUN_0046c9a2` applies the jitter:

- **chr-shake** (`L67606`): in the standee draw loop, for each standee that
  passes the active (`field11 != 0`) + registered-graphic (`chrname[g][0] !=
  0`) checks, `y = ftol(field2); if (DAT_073a6d9c != 0) y += (rand()&0x1f)-0x10;`
  — **one `rand()` (`FUN_005041f6`) per drawn standee, per frame**, applied to
  **Y only** (X = `field1` is untouched). Each standee therefore gets an
  **independent** ±[−16,+15] vertical jitter — this is why the iv1_2 "RECETTE!"
  standees (Tear / Recette portraits + the `giku`/`hatena` manga marks) jitter
  by *different* amounts each frame, not as a rigid screen shake.
- **bg-shake** (`L67507`): only in the **scroll** bg path (`bg_mode != 0`); one
  `rand()` computes a shared Y offset `(rand()&0x1f)-16` for all three 1024-tiles.
  The **static** bg path (`bg_mode == 0`, the prologue caps) does NOT shake and
  consumes no RNG.

Because the offset is a fresh `rand()` every frame, a standee's Y at any *frozen*
capture is a per-frame RNG/load-phase artifact (§85): retail's own value at a
fixed-offset frame is non-reproducible. The port (`scene1_dialogue_draw.c`
`draw_standees` / `draw_background`, `scene1_dialogue_run.c` `IVE_OP_RMB` +
the per-step decrement) reproduces the shake faithfully for the shipped game,
and `{phasepin}` zeros both countdowns on **both** sides (port
`scene1_intro_dialogue_phasepin`, Frida `DAT_073a6d98/9c ← 0`) so a parity
capture lands on the **un-shaken base pose** — the port's base standee positions
are bit-identical to retail's static (pre-shake) ones. **Diagnosis trail:** the
retail `--d3d-trace` standee top-Y, decoded per frame, is static (`giku=96
hatena=32 Rec=0`) until the "RECETTE!" line then jitters ±20px every frame; the
port drew them static at base → the cap_44 residual. The earlier
"freeroam-actor / not-a-standee" call was wrong — its logic ("standees settled
`y==ty` → not the standee") only compared port-y to the port *target*, never to
retail.

## 106. The LOAD GAME slot-picker's selected-card "load flash" rides the SAME global as the title→in-game fade counter (DAT_0964351c) — confirming a slot brightens the chosen card as the screen fades

The picker render `FUN_0049b556`'s **`param_6`** (which adds
`sin(param_6·π/30)·128` to the selected card's greyscale, `FUN_0049a59e`
L101990 → `FUN_0049b556` L101340-101349) is **`DAT_0964351c`** — and that is the
**identical** global the title scene uses as its commit-to-in-game **fade
counter**. There is no separate "flash" timer.

Lifecycle of `DAT_0964351c` (`FUN_0049a59e`):
- `0` while browsing the picker (or the main menu) → `param_6 ≤ 0` → no flash,
  the selected card only does its idle shimmer `sin(DAT_09643574·0.1)·32+159`.
- Armed to `1` the frame **A confirms an occupied slot** (L100907; the main-menu
  NEW/CONTINUE confirm arms it the same way at L101073).
- Incremented **every subsequent frame** at the top of the tick (L100596-100597:
  `if (0 < DAT_0964351c) DAT_0964351c++`) up to `0x1e` (30), at which point the
  fade is fully out and the scene transitions.

So during the ~30-frame fade-out the chosen card visibly **brightens then dims**
(`sin(param_6·π/30)` peaks at `param_6=15`, back to 0 at `param_6=30`) — the
"slot lights up when you pick it" the player sees before the screen goes black.
The card-content TEXT does the inverse (`0x7f − sin(param_6·π/30)·…`, L101438),
darkening as the card brightens. The picker keeps rendering throughout because
the confirm path takes the fade branch *before* the `cursor_anim` slide-out, so
`cursor_anim==10 && submenu==1` (its render gate) holds until the transition.

Implication for any port: this counter must be modeled **once**. (OpenRecet had
split it into a live `fade_counter` and a dead picker `pulse` field that was
never incremented, so the flash never played; fixed by reading the one
`fade_counter` as `param_6`.)

## 107. CONTINUE-load SNAPS the day-clock hand to the saved time-of-day; NEW-game eases it up from 0 (the day-start sweep). The hand target is the working-arena dword 0xb0fc

The shop's gold clock dial has a rotating day-hand (`FUN_00406241`, angle
`π/2 − DAT_0438b7d4·π/3`). `DAT_0438b7d4` is the *displayed* hand position; it
eases at +0.005/frame toward a target `DAT_0450fb88` (`FUN_00453xxx`
L50643-50655, only ever easing UP, clamped to [target−1, 3.5]).

At the title→in-game commit (`FUN_0049a59e`):
- **L100611** writes `DAT_0438b7d4 = 0.0` unconditionally.
- **CONTINUE** branch (L100638) then **snaps** `DAT_0438b7d4 = (float)(int)
  DAT_0450fb88[slot]` — the hand jumps straight to the saved time-of-day, no
  sweep (you resume mid-day where you left off).
- **NEW** branch leaves it at 0, so the hand **eases up** from 0 toward the
  day's target over the first ~200 frames — the visible day-start hand sweep.

The target `DAT_0450fb88` is the **working-arena dword `0xb0fc`** (the same
offset the *picker-card* code reads from the disk-bank copy as "portrait rot",
`DAT_05712670`; in the live arena it is the clock target). The day number the
HUD prints is `DAT_0450fb84` = working dword `0xb0fb` (= the bank's CARD_DAY).
Both are part of the serialized save, so a CONTINUE restores the hand exactly.

Verified on retail (Frida watch of `DAT_0438b7d4`/`DAT_0450fb88` over a load):
both read **1** at the loaded `fa7c82` save (day-1 start), and that save's
dword `0xb0fc` is **1** — i.e. the hand resumes at phase 1, NOT at the
new-game 0. A port that defaults the hand to 0 on load draws it ~one
sixth-turn off.

## 108. The records-A particle/overlay dispatch runs 2-sided: FUN_004176ff sets CULLMODE=NONE (at 0x41784d) before the overlay billboards, so the 目玉商品 sparkle, the display-item billboards, and the wing draws are all back-face-visible

In the HOUSE free-roam render, **FUN_004176ff** (the records-A dust/wing/
overlay pass) sets `D3DRS_CULLMODE = D3DCULL_NONE` (=1) at **ret_va 0x41784d**,
immediately before its `FUN_00414ee2` overlay dispatch. Verified on the retail
d3d-trace (`runs/sr-retail/d3d_trace.jsonl`, free-roam frame 707): the
SetRenderState(CULLMODE,1) at 0x41784d precedes the sparkle `SetTexture
0x172fcf30` + `DrawPrimitiveUP @ 0x415e61`, and **every** particle/billboard
draw in that group runs under CULL=NONE —

- the 目玉商品 sparkle (`0x415e61`),
- the shop-display item billboards (`0x4161c3`),
- the Tear wing-glow (`0x41e165`).

These overlay quads are emitted as a TRIANGLESTRIP whose winding is
**back-facing under the scene's default CULL=CW**, so the engine draws them
two-sided. A port that inherits the scene-default cull (CW) silently culls the
sparkle quads — they are issued (DrawPrimitiveUP runs) but rasterize 0 pixels.
NONE is left set through the wing/dust draws that follow; the next pass resets
its own cull (retail toggles CULLMODE 1↔3 ~23×/frame). The sparkle keeps
ALPHATESTENABLE=1 / ALPHAFUNC=GREATER / ALPHAREF=0 (its effect00.bmp star
texels are opaque, so alpha>0 passes) and additive ONE/ONE blend.


## 109. The free-roam companion (Tear) does NOT update its facing while idle — FUN_0048a4d1 writes the facing octant only on a moving frame, so a stationary fairy HOLDS its entry seed (octant 4 = facing DOWN). The `(comp.x≤player.x)?6:2` side-rule is FUN_0048a833's INTRO-ONLY branch

The fairy companion's per-frame controller in HOUSE free-roam is **FUN_0048a4d1**
(the spring-follow, called every frame from **FUN_0048a833**; both fire 49/49
over a loaded-shop capture window). Its facing law (all.c:89083-89121) is gated
on **movement**:

- **moved ≤ 0.01 (idle):** set the idle anim (`DAT_056dab40 = 0`, or `5` when
  `DAT_0450f405[slot] != 0`) and **write NO facing** — `DAT_056dab58` is left at
  whatever it was: the entry seed, or the last walking direction.
- **moved > 0.01 (walking):** anim 1 + `DAT_056dab58 = DAT_056dab00[target·0xb]`
  (copy the followed actor's — the player's — facing octant).

So a fairy that is stationary from scene entry (a **CONTINUE-load** straight into
the shop, player not moving) keeps the octant the scene-entry pose seeded — **4
(facing down / toward camera)**. Confirmed on retail with the flow-trace:
`flow_diff --verdict --align-field db054` over `house-loaded-display-pinned`
reports `coct` retail **4** every frame of the window, with **every other actor
field (player + companion pos/anim/facing-angle, db054, rng/rngcalls)
bit-identical** — `coct` was the lone divergence (port was 2).

The `dab58 = (comp.x ≤ player.x) ? 6 : 2` SIDE-rule that *looks* like the idle
law is actually **FUN_0048a833's branch A**, gated `DAT_0438b928 == 1 &&
DAT_0438b924 < 200` — the **intro window** (the iv1_1 scene where Recette stands
looking UP at Tear). It is NOT the free-roam law. A port that applies the 6/2
side-rule to all idle frames matches new-game WALK scenarios only by luck (Tear
is *moving* there, so both sides take the copy-player-octant path) and diverges
exactly when the fairy is idle from entry. Gates observed on retail in the
loaded shop: `DAT_056da1c8=0` (no early return), `DAT_056db048=0`,
`DAT_0438be6c=0` (idle-countdown wander state). See [[scene1-wing-glow]],
[[scene1-tear-visual-diffs]].
