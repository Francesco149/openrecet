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
