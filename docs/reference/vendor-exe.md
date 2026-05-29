# Reference binary — the exact Recettear exe this port targets

All reverse-engineering in this repo is against **one specific build** of
Recettear. This file pins it down so future version-diffing has a stable
anchor, and so anyone reproducing the work knows precisely which binary
to obtain (their own legitimate copy — we ship no game bytes).

> Hashes here are reproducible with `sha256sum` against the files under
> `vendor/` (gitignored; `vendor/original` is a symlink to the local
> Steam install). The unpacked hash is also recorded in
> `vendor/unpacked/.unpacked.sha256`.

## Game

| field | value |
|---|---|
| Title | Recettear: An Item Shop's Tale |
| Developer | EasyGameStation (2007) |
| Localisation | Carpe Fulgur (English) |
| Window title | `RECETTEAR Ver 1.108` (`src/main.c` `AZUMANGA_TITLE`) |
| Steam App ID | **70400** (confirmed against `appmanifest_70400.acf` in the local install) |
| Distribution | Steam, English build |

## Packed binary (retail, as shipped on Steam)

This is the file the public port reads SE audio from at runtime
(`docs/formats/se-pack.md`) — it is **not** redistributed.

| field | value |
|---|---|
| path | `<Steam>/steamapps/common/Recettear/recettear.exe` |
| size | 5,629,440 bytes |
| sha256 | `079b5b679f1d363ea3dcfe4ec931ceb6d9e4b4a288926ffc8abb5814e80392b4` |
| PE timestamp | `1286439830` = 2010-10-07 08:23:50 UTC |
| machine | `0x14c` (i386, 32-bit) |
| sections | `.text .rdata .data .data1 .rsrc .extra .bind` |
| protection | SteamStub (lives in `.bind`); encrypts **`.text` only** |

## Unpacked binary (what we disassemble / hex-inspect)

Produced locally with Steamless; the source for `docs/decompiled/`.
Disassembly and hex spelunking always use this file, never the packed
one (`.text` is still encrypted in the packed exe).

| field | value |
|---|---|
| path | `vendor/unpacked/recettear.unpacked.exe` |
| size | 5,268,992 bytes |
| sha256 | `ab7d1d952150f3a5a5f1acf0c230d438a6882e5f5be88bb79972d4100ca7c27d` |
| PE timestamp | `1286439830` (unchanged) |
| sections | `.text .rdata .data .data1 .rsrc .extra` (`.bind` stripped) |

## Steamless

| field | value |
|---|---|
| tool | Steamless v3.1.0.5 (by atom0s) |
| source | <https://github.com/atom0s/Steamless/releases> |
| how | run via WSLInterop; see `tools/setup.sh` + `flake.nix` (`OPENRECET_STEAMLESS_DIR`) |

## SteamStub encryption map (per-section, this exe)

Per-section sha256 of packed vs unpacked shows SteamStub encrypts only
the code section:

| section | packed vs unpacked |
|---|---|
| `.text`  | **DIFFERS** (encrypted) |
| `.rdata` | identical |
| `.data`  | identical |
| `.data1` | identical |
| `.rsrc`  | identical |
| `.extra` | identical |

**Consequence:** the game's resources (`.rsrc`, custom type `WAVE`) and
its data tables (`.data`, e.g. the SE id table at VA `0x005d1584`) are
byte-identical in the *packed* retail exe. So the port reads SE audio
directly from the packed `recettear.exe` via
`LoadLibraryEx(...AS_DATAFILE)` + `FindResource` — no Steamless
replication or decryption needed at runtime. This is the basis for the
runtime SE extraction (`docs/formats/se-pack.md`, public-release detour
Task 2 — `docs/plans/public-release-detour.md`).
</content>
</invoke>
