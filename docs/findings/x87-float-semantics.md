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
