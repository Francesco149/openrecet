# Recettear asset archive: `bin/data*.bin` + `lnkdatas.bin` + `bmpdata.bin`

**Status:** known format. Spec reverse-engineered by
[UnrealPowerz/recettear-repacker](https://github.com/UnrealPowerz/recettear-repacker)
and validated on the current Steam build (2026-05-19: 1188 files extracted,
440 MB uncompressed — matches the upstream README's "1200 files, 435 MB"
figure, so format is current).

Our own extractor: `tools/extract/data-bin.py` (independent reimplementation
matching the spec below, validated by output diff against upstream).

## Layout overview

```
recet root/
├── bin/
│   ├── data000.bin  ─┐ 10 MiB each, concatenated logically
│   ├── data001.bin   │ (asset bytes, individually LZSS-compressed)
│   └── …            ─┘
├── lnkdatas.bin       (index: path → offset/size in the data*.bin stream)
└── bmpdata.bin        (LZW-compressed update overlay; checked FIRST at runtime)
```

The game checks `bmpdata.bin` first for each lookup; if not found, falls
back to `lnkdatas.bin` + `bin/data*.bin`. That's the developers' patch
mechanism — re-ship a small `bmpdata.bin` instead of 120 MB of `data*.bin`.

## `lnkdatas.bin` — index header

All integers **big-endian**.

```
struct lnkdatas {
    int32_t      n_items;
    item_entry_t items[n_items];
};

struct item_entry_t {                // 128 + 12 = 140 bytes
    char    name[128];               // null-padded ASCII path, e.g. "bmp/title01.tga"
    int32_t decompressed_size;       // bytes after LZSS decode
    int32_t offset;                  // absolute byte offset into the logical data*.bin stream
    int32_t compressed_size;         // bytes in the stream
};
```

The 128-byte name field is null-padded. Decode by reading up to the first
NUL.

## `bin/data*.bin` — concatenated LZSS-compressed blobs

Each file is **exactly 10 MiB** (10485760 bytes) except the last, which
is the tail. Logically they form one contiguous byte stream:

- `offset = 0` is the first byte of `bin/data000.bin`.
- `offset = 10485760` is the first byte of `bin/data001.bin`.
- An entry that straddles a 10 MiB boundary crosses files seamlessly.

## LZSS variant

Custom bit-packing — neither vanilla "LZSS reference" nor LZ77. From
`recettear-repacker/lnk_unpack.py`:

- Read **control byte**, MSB-first across 8 bits:
  - bit = 0 → literal: copy next 1 byte to output
  - bit = 1 → back-reference:
    - read 2 bytes `b1`, `b2`
    - `back = ((b1 & 0xF0) << 4) | b2`  *(12-bit back-distance)*
    - if `back == 0`: **end of stream**
    - `length = b1 & 0x0F` *(low 4 bits of b1)*
    - if `length == 0`: `length = (next_byte) + 16` *(extended length)*
    - copy `length + 1` bytes from `output[output_len - back .. ]`
      *(self-overlap is intentional — RLE works through this path)*
- Repeat until end-of-stream marker (`back == 0`).

### Pseudocode

```c
void lzss_decompress(const uint8_t *in, uint8_t *out, size_t *out_len) {
    size_t op = 0;
    for (;;) {
        uint8_t ctrl = *in++;
        for (int bit = 7; bit >= 0; --bit) {
            if (!(ctrl & (1 << bit))) {
                out[op++] = *in++;
            } else {
                uint8_t b1 = *in++, b2 = *in++;
                uint16_t back = ((b1 & 0xF0u) << 4) | b2;
                if (back == 0) { *out_len = op; return; }
                size_t length = (b1 & 0x0Fu);
                if (length == 0) length = (*in++) + 16;
                size_t src = op - back;
                for (size_t i = 0; i <= length; ++i)
                    out[op++] = out[src + i];   // intentional self-overlap
            }
        }
    }
}
```

## `bmpdata.bin` — LZW update overlay

Separate format. Contains both metadata and data (self-describing index).
Compressed with **LZW** (variable-width codes; details in
`recettear-repacker/bmp_unpack.py`).

We'll add a `bmp-bin.md` spec doc when we cover that path in detail. For
now, refer to the upstream Python.

## File-tree contents

Extraction reveals (top-level dirs inside the logical archive):

| dir / file          | contents                                            |
|---------------------|-----------------------------------------------------|
| `bmp/`              | 2D art (TGA + BMP). Includes `item/`, `ivent/`, UI |
| `data/`             | gameplay text tables (`item.txt`, etc.)            |
| `ef/`               | effect data (different from on-disk `ef/effect*.dat`) |
| `fontdata.bin` / `fontidx.bin` | bitmap font system                       |
| `idx/`              | indices (TBD format)                               |
| `iv/*.ivt`          | event / cutscene data ("ivent")                    |
| `kyaku/`            | customer data (Japanese *kyaku* = customer)        |
| `xfile/`, `xfile2/` | additional 3D models bundled in archive            |

(File count: **1188**. Uncompressed total: **~440 MB**.)

## Validation

Our extractor `tools/extract/data-bin.py` produces byte-identical output
to `UnrealPowerz/recettear-repacker/lnk_unpack.py` on the current Steam
build. Verify with:

```fish
./tools/extract/data-bin.py --validate-against /opt/src/recettear-repacker
```

(`--validate-against` extracts both and diffs the file trees + hashes.)

## Cross-references

- **UnrealPowerz/recettear-repacker** — the spec source. Python.
- **ribeena/RecettearXTools** — `.x` ↔ USD converter (Blender 4.1 round-trip).
- **just-harry/FancyScreenPatchForRecettear** — runtime patcher for
  widescreen + render fixes; useful for `recettear.exe` offsets and patch
  sites.
