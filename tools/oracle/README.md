# tools/oracle — retail-as-a-library / in-process retail host (PROTOTYPE)

Prototype from the 2026-10-13 methodology audit
(`docs/audits/2026-10-13-methodology-reset.md`). Findings:
`docs/findings/in-process-retail-host.md`. Not wired into any harness yet.

Idea: the host process maps the user's own `recettear.unpacked.exe` at its
fixed base 0x400000 (relocs stripped) and either calls retail functions
directly (**P1**, offline oracle) or jumps to retail's own CRT entry so the
whole game runs *inside* the host (**P2**). The host owns the process ⇒ hooks
are plain call-site rewrites / IAT swaps, snapshots are `memcpy`, no Frida, no
DLL injection, no cross-process sync. Same exe runs on Windows and wine.

| file | what |
|---|---|
| `retail_map.h` | PE mapper + `RETAIL_HOLE_DEFINE` (bss hole reserving 0x400000..) |
| `p1_pure.c` | P1: retail `rand`/`haggle_decide` vs port C, 3 x87 PC modes |
| `p1b_more.c` (+`p1b_stubs.c`) | P1b: `boss_id_allowed`, `floor_is_checkpoint`, `audio_fade` (fake COM AudioPath records `SetVolume`) |
| `p2_run.c` | P2: run retail in-process; virtual clock, scripted input, deferred loader, virtual time, seed pin, shadow exec, `rand` replace, snapshot |
| `p3_run.c` | P3 = P2 + replay of existing `tests/scenarios/*/trace.jsonl` (reuses `src/input_segtrace.c`; anchors ported from the Frida agent) + live `haggle_decide` shadow (port c11 vs gnu11 vs retail) |
| `build.sh` | builds all of the above into `build/oracle/` |

## Build (mingw32; host must sit at 0x200000, no ASLR)

```sh
LDF="-Wl,--image-base,0x200000 -Wl,--large-address-aware -Wl,--disable-dynamicbase"
i686-w64-mingw32-gcc -std=gnu11 -O2 $LDF -o p2_run.exe tools/oracle/p2_run.c -static-libgcc
S=src; i686-w64-mingw32-gcc -std=c11 -O2 -I$S -include $S/d3d_trace_macros.h $LDF \
  -o p1_pure.exe tools/oracle/p1_pure.c $S/rng.c $S/customer_haggle.c -static-libgcc
```

P1b: `... -o p1b.exe tools/oracle/p1b_more.c tools/oracle/p1b_stubs.c src/stage_gate.c src/audio_fade.c -Itools/oracle`

## Run

P2 needs cwd = a game dir (data files) containing `recettear.unpacked.exe`;
use a scratch COPY (the game writes `save.dat`/`recet.ini` into cwd).

```sh
# Linux, headless (wine 11 + Xvfb, software GL ~115 fps)
printf '0 0\n45 10\n48 0\n240 10\n243 0\n' > load.txt      # A, A: Load Game -> slot
WINEPREFIX=$PWD/wpfx WINEDEBUG=-all DEFER_THREADS=1 VTIME=1 RNG_SEED=19937 \
  xvfb-run -a -s "-screen 0 1024x768x24" wine p2_run.exe recettear.unpacked.exe 1500 99999 x.bin load.txt
# Windows native via wslop interop (~1000 fps): same args, env via WSLENV.
```

Expected tail: `done at frame 1500; ... semantic_hash=<same every run> rng=7b2df9f6`.
