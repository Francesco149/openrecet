# Texture loader (`FUN_0047193c`)

**Status:** identified, not yet ported. OpenRecet currently uses a
standalone TGA parser + `IDirect3DDevice8::CreateTexture` (see
`src/tga.c`, `src/sprite.c`) — engine-accurate behavior is a later
milestone.

## What the original does

`FUN_0047193c(blend_mode, slot_ptr, path, expected_w, expected_h)` is the
texture-loading entry point. Callers include `FUN_004733d5` ("read
titletex ok") and the per-system initialisers.

Sequence:

1. Stash `expected_w`/`expected_h` and `blend_mode` into the texture slot.
2. Allocate a working buffer of `expected_w * expected_h * 8` bytes
   (suggests room for mip generation; D3DX uses this for its scratch).
3. **Scan the path string for a `.b` / `.B` extension** — if present, the
   blend-mode arg gets overwritten with `0xff00ff00` (D3DCOLOR magenta-
   like: A=0xff, R=0x00, G=0xff, B=0x00). This is the **green color key
   for BMP assets**. TGAs skip this and use their alpha channel directly.
4. Look up the asset in the lnkdatas index (`FUN_005038b0`).
5. If found: read the decompressed bytes (`FUN_00503734`) and hand them
   to `FUN_004cd30e(...)` — **`D3DXCreateTextureFromFileInMemoryEx`**
   (identified by the 15-arg signature and the error-code switch:
   `D3DXERR_INVALIDDATA = 0x88760B59`,
   `D3DERR_OUTOFVIDEOMEMORY = 0x8876086C`, etc.).
6. Fallback: try `FUN_004346bf(path, buf)` (raw disk read) for paths not
   in lnkdatas, then call the same `D3DXCreateTextureFromFileInMemoryEx`.

The `D3DXCreateTextureFromFileInMemoryEx` call args:

```
pDevice          DAT_073dfcbc                  (global IDirect3DDevice8 *)
pSrcData         the lnkdatas/disk buffer
SrcDataSize      its size
Width            expected_w
Height           expected_h
MipLevels        1
Usage            0
Format           D3DFMT_UNKNOWN (0)
Pool             D3DPOOL_MANAGED (1)
Filter           D3DX_DEFAULT (0xFFFFFFFF)
MipFilter        D3DX_DEFAULT (0xFFFFFFFF)
ColorKey         blend_mode (0x00000000 for TGA, 0xFF00FF00 for BMP)
pSrcInfo         NULL
pPalette         NULL
ppTexture        slot_ptr (output)
```

## Implications for the port

- We must accept BMP with a magenta-like green key (`0xFF00FF00`) and
  TGA with native alpha. The current `tga.c` covers Type 2 only — adding
  RLE + a BMP-with-color-key path is the next loader task.
- `d3dx8.dll` is not in nixpkgs and is a deprecated Microsoft package.
  We will not link against it; instead we'll grow our own decoders.
  Pixel-identical output requires matching D3DX's filter chain when
  resampling to `expected_w`/`expected_h` — but most assets in the audit
  so far ship at native resolution, so resampling is rarely exercised.
- Global `DAT_073dfcbc` is the `IDirect3DDevice8 *`, distinct from
  `DAT_073dfcb8` (the `IDirect3D8 *`). Both already noted in
  `winmain-and-bootstrap.md`; record here for cross-reference.

## Open questions

- Does the engine ever pass paths *not* in lnkdatas at runtime, or is the
  disk fallback dead code (left in from development)? Worth grepping for
  callers that use literal paths missing from the index.
- The 8× working-buffer size suggests mip levels are generated even
  though `MipLevels=1` is passed — possibly a sentinel meaning "auto" in
  some D3DX revisions. Confirm by hooking the real d3dx8 call.
