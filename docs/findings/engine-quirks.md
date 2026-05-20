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

That's the tour.  None of these prevent the game from running, all of
them are charming in their own way, and at least three of them
(quirks 1, 2, and 7) made us double-check the decompilation against an
external reference before believing what we were reading.
