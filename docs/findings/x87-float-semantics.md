# x87 float semantics: port vs retail (2026-10-13 sweep)

Owner request: "re-check that floats behave the same" before switching `-std`.

## Facts
- Retail (MSVC6) in-game x87 CW = **0x027F (53-bit)** (rhost, Win11 + wine).
- Retail keeps intermediates in ST0, rounds only at spills (`fstp m32/m64`), uses m32
  (sometimes m64) constant operands, `__ftol` truncates the unrounded ST0. Example
  FUN_00460161 @0x4602bc: `fild rate; fstp m32; fild work; fstp m32; fld; fmul; fdiv m32
  100.0; fstp m32; fld; call __ftol`.
- Port on mingw x87:
  - `-std=c11` (`-fexcess-precision=standard`) evaluates `0.1f` literals as long double
    (≠ the m32 constant) and rounds at every float assignment or parameter.
  - `-std=gnu11` (fast) keeps literal float values but elides float casts/assignments.

## Sweep
- Every `src/*.c` compiled both ways, disassembly diffed per function: **284 / 1500
  functions differ in 75 files** (`x87-affected-functions.tsv`).
- Classes:

  | class | count | meaning |
  |---|---|---|
  | ldbl-literal | 175 | contains widened literals |
  | float-round / double-round only | 68 | c11 rounds at assignment, gnu11 keeps ST0 |
  | other | 41 | scheduling only |

- Different code ≠ different results: `audio_fade`, `boss_id_allowed` and
  `floor_is_checkpoint` match retail under both flags (P1).

## Oracle evidence (P1: retail function called in-process vs port)

| fn | c11 before | gnu11 before | after explicit precision (both flags, PC53/24/64) |
|---|---|---|---|
| haggle_decide FUN_00460672 | mismatches (ref 60 ask 63) | 484,605/484,605 | 484,605/484,605 |
| haggle_offer_up FUN_00460161 | 197,924/200k (round 0) | 199,654/200k (rounds ≥1) | 300k/300k |
| haggle_offer_down FUN_004603cf | 198,305/200k | 199,413/200k | 300k/300k |

**Neither flag is retail-exact in general.** gnu11 is less wrong on this sample (literal
class is the larger error source), so the port builds `-std=gnu11` from 2026-10-13.

## Rule (src/x87.h)
- ST0 chain → `long double`;
- spill → `spill32`/`spill64` (a volatile round-trip; rounds under any flag);
- m32 constant → a `static const float` object;
- `__ftol` → `x87_ftol`.

Code written this way is identical under c11/gnu11 at any CW. Convert affected functions
flow by flow (re-verification program, FRONT); verify each with a P1 oracle
(`tools/oracle/p1_*.c`) or an rhost shadow hook.

## D3DX8 math (src/math3d.c): retail runs the CPU-dispatched SSE2 table (2026-10-13)

**Retail's D3DX math is NOT x87 on any modern CPU, and its results differ between
Intel and AMD.**

### Dispatch (disasm + in-process, `tools/oracle/p1_math3d.c`)
- D3DX8 (statically linked) routes math through a 57-slot pointer table @0x5fda48.
  At load every slot = init thunk `push 1; call D3DXCpuOptimizations@0x4cdd9f; jmp [slot]`,
  `DAT_005fe908 = 0xffff`. The first math call fills it: defaults (c_*, copied from
  0x5fdb30) → x86_* overrides (slots 0–7) → `HKLM\Software\Microsoft\Direct3D\DisableD3DXPSGP==1`
  ⇒ stop (mode 0) → `IsProcessorFeaturePresent(PF_3DNOW)` ⇒ x3d_* (mode 1) → CPUID.1:EDX.26
  SSE2 (no vendor check; the "GenuineIntel" string is copied, never compared) ⇒ SSE2 table
  0x4e27c0 (mode 2) → PF_XMMI ⇒ SSE table (mode 3). All 57 callers pass 1; nothing resets it.
- Measured (oracle calls D3DXMatrixMultiply 0x4a2a10 through the untouched init thunk):
  **mode 2** on i9-10900K / wine 11 AND Ryzen 9 5900X / Win11 native (3DNow! absent on both).
  MXCSR 0x1F80 (wine) / 0x1FA0 (Win11, PE sticky bit only). Mode 1 = pre-Bulldozer AMD only.
- Every game call site uses the dispatch entry, never a concrete variant: MatrixMultiply
  0x4a2a10 ×343, Translation 0x4a34b0 ×146, Scaling 0x4a3400 ×135, RotationY 0x4a35ef ×54,
  RotationX 0x4a3553 ×49, RotationZ 0x4a368c ×42, PerspectiveFovRH 0x4a3ee8 ×18 (direct
  export), LookAtRH 0x4a3b52 ×8 (direct), Shadow 0x4a4461 ×6, PlaneFromPointNormal 0x4a4f5f ×6,
  Inverse 0x4a2f42 ×3, Identity 0x404bb8 ×16 (inline helper). D3DXVec3Normalize 0x4a1f74: 0 game
  sites, 16 D3DX-internal (LookAtRH ×2 …).

| port fn | slot | mode-2 impl | arithmetic |
|---|---|---|---|
| vec3_normalize | 7 | 0x4ecb60 SSE | len²=(x²+y²)+z²; <2^-46 → 0; s=(0.5r)(3−(len²r)r), r=**rsqrtss** |
| mat4_mul | 3 | 0x4eca10 SSE | row=((a0B0+a1B1)+a2B2)+a3B3, float/op |
| mat4_inverse | 18 | 0x4ecdb0 SSE | Intel AP-928 Cramer; 1/det=**rcpss**+Newton; det==0/NaN → NULL, out untouched |
| mat4_shadow | 27 | 0x4ed550 SSE | inline plane normalize: len²=(a²+c²)+b², ==0 → P=0, else rsqrt; off-diag +(+0) |
| mat4_rotation_x/y | 30/31 | 0x4ed280/0x4ed300 SSE | polynomial `sse2_sincos` 0x4f8730 (Cody–Waite π/2) |
| mat4_rotation_z | 32 | c_ 0x4a3692 x87 | fsincos, spill m32 |
| plane_from_point_normal | 12 | c_ 0x4a4f65 x87 | d=−((p.y n.y+p.z n.z)+p.x n.x), ST |
| mat4_translation/scaling | 26/33 | c_ | copies |
| mat4_lookat_rh | — | 0x4a3b52 x87 | z,x via slot-7 normalize; y in ST; T=−((a0e0+a1e1)+a2e2) |
| mat4_perspective_fov_rh | — | 0x4a3ee8 x87 | fsincos(spill32(fovy·0.5)); h=cos/sin ST; q=zf/(zn−zf) ST |

Port (`src/math3d.c`): SSE functions transcribed instruction-for-instruction with intrinsics
(`target("sse2")`, `force_align_arg_pointer`) — hardware rsqrtss/rcpss ⇒ port == retail on the
SAME CPU, whatever the vendor; x87 functions per x87.h (+ new `x87_fsincos`). Requires SSE2.
`PORT-DEBT(simplified, FUN_004cdd9f)`: modes 0/1/3 not mirrored. CI x87 gate
(`tools/ci/no_sse_math.py`) now allowlists exactly these SSE functions.

### Results — bit-exact cases / 100,000 (identity /1,000), every output float + inverse NULL
| fn | before PC53 c11 / gnu11 (Intel) | before PC53 c11 / gnu11 (AMD) | before PC24 (Intel, both flags) | after |
|---|---|---|---|---|
| vec3_normalize | 28,183 / 28,644 | 31,638 / 32,149 | 27,576 | 100k |
| mat4_mul | 389 / 389 | 389 / 389 | 100,000 | 100k |
| mat4_inverse | 7,873 / 7,553 | 7,874 / 7,582 | 8,979 | 100k |
| mat4_shadow | 4,424 / 1,122 | 11,334 / 4,693 | 6,367 | 100k |
| plane_from_point_normal | 100k / 100k | 100k / 100k | 74,051 | 100k |
| mat4_rotation_x | 59,528 / 59,528 | 59,528 / 59,528 | 47,419 | 100k |
| mat4_rotation_y | 59,587 / 59,587 | 59,587 / 59,587 | 47,440 | 100k |
| mat4_rotation_z | 100k / 100k | 100k / 100k | 47,925 | 100k |
| mat4_lookat_rh | 4,777 / 526 | 5,756 / 581 | 3,459 | 100k |
| mat4_perspective_fov_rh | 56,181 / 100k | 56,181 / 100k | 84,302 | 100k |
| translation / scaling / identity | exact | exact | exact | exact |

**After = c11 AND gnu11, PC53/PC24/PC64, Intel+wine AND AMD+Win11: 7,206,000 comparisons per
machine, 0 mismatches** (before: 3,348,379 Intel, 3,091,692 AMD). Also 0 with the shipping `src/math3d.o` (nix GCC 15.2, port CFLAGS)
renamed into the oracle. PC64 "before" ≈ PC53. Old-port errors beyond precision: libm
sinf/cosf vs fsincos / sse2_sincos; zero vector → NaN (retail 0); singular |det|≤1e-7 (retail
det==0); lookat translation summed z,y,x with y rounded (retail (x+y)+z, y in ST);
plane d summed x,y,z.

### Retail is CPU-vendor dependent (same inputs, PC53, Intel digest vs AMD run)
| D3DX fn | identical Intel↔AMD |
|---|---|
| Vec3Normalize | 73,579 / 100k |
| MatrixInverse | 80,941 / 100k |
| MatrixShadow | 50,415 / 100k |
| MatrixLookAtRH | 56,010 / 100k |
| all others (Multiply, Rotation X/Y/Z, PerspectiveFovRH, PlaneFromPointNormal, …) | 100% |

Cause: rsqrtss/rcpss approximation tables differ (e.g. rsqrtss(1.0) = 0x3f7ff000 Intel,
0x3f7ff800 AMD). Shop camera eye (−1,22.2,15)→(−1,1.2,1): view m[5] 0x3f0e00d4 vs …d5,
m[13] 0x3e2a6748 vs 0x3e2a6768 (32 ulp), m[14] 0xc1d6560d vs …0f; its inverse m[0]/m[14] =
1.0/15.0 (Intel) vs 0.99999994/14.999997 (AMD). ⇒ port↔retail float comparisons are valid
only on the SAME CPU vendor; a trace recorded on one vendor and compared on the other has an
extra "cpu" pillar (low-bit view/shadow/inverse drift) — not a port bug.

Variant agreement with the dispatched one (PC53, Intel): c_ normalize 31,968, x86_ 4,884;
c_/x86_ multiply 389; c_ inverse 7,565; c_ shadow 7,062; c_ rotX/Y 59,528/59,587; every sse_
variant 100k. ⇒ mode 3 (SSE1) == mode 2 except RotationX/Y (mode 3 keeps c_ fsincos).

Repro: `tools/oracle/build.sh`; `cd staged_retail && WINEPREFIX=… wine build/oracle/p1_math3d.exe
recettear.unpacked.exe 100000 [dump.bin|-] [ref.bin]` (dump/ref = per-case retail digests for
the cross-CPU compare); Win11: `ssh wslop 'cd /mnt/c/orc-oracle/game && ./p1_math3d.exe …'`.
