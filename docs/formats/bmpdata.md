# `bmpdata.bin` — LZW-compressed update overlay

**Status:** known format. Independent C reimplementation in
`src/bmp_lzw.c` + `src/storage.c`, validated byte-for-byte against
[`UnrealPowerz/recettear-repacker`](https://github.com/UnrealPowerz/recettear-repacker)
on the current Steam build (22 entries, 100% match — see
`/tmp/storage_diff.py` in the session that landed this).

The engine checks `bmpdata.bin` **first** on every asset lookup, falling
back to the `lnkdatas.bin` + `bin/data*.bin` archive on miss. That makes
it the developers' patch mechanism: re-ship a small `bmpdata.bin`
instead of a 120 MB rebuild. The shipping Steam build's `bmpdata.bin`
is ~150 KB and contains a handful of patched cutscene/dialog files
(`iv/*.ivt`, `kyaku/*.txt`, etc.).

## Layout

All integers **big-endian**. Names are ASCII, NUL-padded, no trailing
slash.

```
struct bmpdata {
    int32_t      n_items;
    bmp_entry_t  entries[n_items];   // 96 bytes each
    uint8_t      lzw_payload[ ];     // concatenated per-entry slices
};

struct bmp_entry_t {                 // total 96 bytes (0x60)
    char    name[84];                // NUL-padded, e.g. "iv/iv7_2.ivt"
    int32_t dsize;                   // decompressed bytes
    int32_t offset;                  // byte offset into lzw_payload
    int32_t csize;                   // compressed bytes (slice length)
};
```

- The data section starts at byte `4 + n_items * 96`.
- Each entry's compressed slice is `lzw_payload[offset .. offset+csize]`.
- After decoding, the result is exactly `dsize` bytes long.

## Integrity hash

The whole file (header + entries + payload, byte-for-byte as on disk) is
fed to the same CRC-16 variant used for `lnkdatas.bin`
(`src/lnkdatas_hash.c`, originally `FUN_00474f14`). The expected
sentinel is **`0x21dc`** — different from `lnkdatas.bin`'s `-0x7456`
(EN) or `-0x3a1f` (JP). Mismatch is fatal (engine pops a MessageBox and
exits).

## LZW variant

- **Code width:** fixed 12 bits, MSB-first within each input byte.
  (Three input bytes pack into two output codes.)
- **Code 0..255:** literal byte.
- **Code 256:** reset / end-of-stream marker. The decoder zeroes its
  dictionary counter and continues; in practice every shipping slice uses
  256 as a trailing sentinel just before the input bits run out.
- **Code ≥257:** index into the online dictionary, built as we go.
- **Dictionary growth:** classic LZW (each new entry = previous string
  plus the first char of the just-decoded string). Frozen at **3839
  entries** (codes 257..4095). Once full, no further entries are added
  but existing entries remain usable.
- **K-w-K case:** when the encoder emits a code equal to the next
  not-yet-defined dictionary index, the decoder treats it as
  `prev_string + prev_string[0]`. Standard LZW behavior — required for
  patterns where the encoder is one step ahead.

### Engine notes

The engine's main LZW loop (`FUN_00434b32`) **doesn't handle code 256
explicitly** — it would naively look up `dict[-1]` and emit a few bytes
of garbage past the caller's buffer. This is benign in the shipping
game (callers allocate `dsize` and the overrun lands in malloc padding),
but our `bmp_lzw_decompress` honors the reset code so we never write
past the buffer the caller asked for.

The bit reader (`FUN_00434c2c`) keeps reading after EOF, returning bits
from a sentinel pattern (effectively `0x0c` repeated — the value
happens to be the requested bit count due to an uninitialized-variable
quirk in the original). The post-EOF reads never make it into the
output because the main loop exits on the next iteration anyway.

## Validation

The standalone harness in `src/storage.c` (`#ifdef
STORAGE_TEST_EXTRACT`) exposes `storage_init → storage_read → stdout`
for one asset. Build + diff:

```fish
nix develop --command bash -c "
  i686-w64-mingw32-gcc -DSTORAGE_TEST_EXTRACT -O2 -Wall -Wextra -std=c11 \
      src/storage.c src/lnkdatas_hash.c src/bmp_lzw.c \
      -o /tmp/storage_extract.exe -luser32
  python3 /tmp/storage_diff.py  # 22/22 ok on current Steam build
"
```

## Cross-references

- **`/opt/src/recettear-repacker/bmp_unpack.py`** — authoritative Python
  reference for the LZW + header layout (84-byte name, 3 × int32, MSB-first
  12-bit code packing).
- **`docs/formats/data-bin.md`** — the larger `lnkdatas.bin` +
  `bin/data*.bin` archive that `bmpdata.bin` overlays.
- **Engine functions:** `FUN_00434b32` (main LZW), `FUN_00434c2c` (bit
  reader), `FUN_00434ca9` (dict walker), `FUN_00434585` (size lookup),
  `FUN_004346bf` (read into buffer).
