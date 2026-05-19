# OpenRecet

An open-source reimplementation of **Recettear: An Item Shop's Tale**
(EasyGameStation, 2007 / Carpe Fulgur, 2010).

This is an **educational reverse-engineering and game-preservation project**.
The goal is a drop-in replacement for `recettear.exe` that behaves
indistinguishably from the original for users who own a legitimate copy of
the game.

**Not distributed:** no game assets, no decompiled binary code, no copyrighted
content of any kind. You supply your own copy of the game.

## Status

Planning phase — see [`docs/PLAN.md`](docs/PLAN.md) for the roadmap and
[`docs/PROGRESS.md`](docs/PROGRESS.md) for the changelog.

## Getting started (NixOS)

```fish
nix develop
./tools/setup.sh        # symlinks game into vendor/, runs Steamless DRM unpacker
```

This enters a dev shell with the full RE toolchain (Ghidra, radare2, retdec,
mingw-w64 32-bit cross compiler, wine, frida, contact-sheet tooling, …) and
prepares the game files for analysis.

## Layout

```
docs/         design notes, file-format specs, progress log
src/          our C reimplementation (cross-compiled with mingw32)
tests/        smoke tests, golden-frame diffs, format-extractor unit tests
tools/        setup, build, extract, capture, contact-sheet, ghidra-headless
vendor/       game files (gitignored) — symlinks into Steam install
ghidra/       Ghidra projects (gitignored — derived from the original binary)
```

## License

MIT. See [`LICENSE`](LICENSE). The license covers OpenRecet's own code only;
no rights are granted to the original game.
