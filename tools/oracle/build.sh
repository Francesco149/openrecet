#!/usr/bin/env bash
# Build the tools/oracle prototypes into build/oracle/ (mingw32, host @0x200000, no ASLR).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd); S=$ROOT/src; O=$ROOT/tools/oracle; B=$ROOT/build/oracle
CC=${CC:-i686-w64-mingw32-gcc}; OBJCOPY=${OBJCOPY:-i686-w64-mingw32-objcopy}; NM=${NM:-i686-w64-mingw32-nm}
LDF=(-Wl,--image-base,0x200000 -Wl,--large-address-aware -Wl,--disable-dynamicbase -static-libgcc)
INC=(-I"$S" -I"$O" -include "$S/d3d_trace_macros.h")
mkdir -p "$B"
# P1 / P1b: port compiled with the SHIPPING flags (-std=c11) vs retail
$CC -std=c11 -O2 "${INC[@]}" "${LDF[@]}" -o "$B/p1_pure.exe" "$O/p1_pure.c" "$S/rng.c" "$S/customer_haggle.c"
$CC -std=c11 -O2 "${INC[@]}" "${LDF[@]}" -o "$B/p1b_more.exe" "$O/p1b_more.c" "$O/p1b_stubs.c" "$S/stage_gate.c" "$S/audio_fade.c"
# P2: in-process runner
$CC -std=gnu11 -O2 "${LDF[@]}" -o "$B/p2_run.exe" "$O/p2_run.c"
# P3: scenario replay + haggle_decide shadow (port built twice: c11 and gnu11)
$CC -std=c11   -O2 "${INC[@]}" -c -o "$B/hag_c11.o"   "$S/customer_haggle.c"
$CC -std=gnu11 -O2 "${INC[@]}" -c -o "$B/hag_gnu11.o" "$S/customer_haggle.c"
$OBJCOPY --redefine-sym _haggle_decide=_haggle_decide_gnu11 "$B/hag_gnu11.o"
for sym in $($NM "$B/hag_gnu11.o" | awk '$2=="T"{print $3}' | grep -v _haggle_decide_gnu11); do
    $OBJCOPY --weaken-symbol="$sym" "$B/hag_gnu11.o"; done
$CC -std=gnu11 -O2 "${INC[@]}" "${LDF[@]}" -o "$B/p3_run.exe" "$O/p3_run.c" "$S/input_segtrace.c" \
    "$B/hag_c11.o" "$B/hag_gnu11.o" "$S/rng.c" -luser32
ls -la "$B"/*.exe
