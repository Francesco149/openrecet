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
# P1 floor layout: retail FUN_0047df4d / FUN_0041f252 vs src/floor_layout.c / src/actor_spawn.c (port flags)
PSTD=$(sed -n 's/^CFLAGS *?= *\(-std=[a-z0-9]*\).*/\1/p' "$S/Makefile")
$CC $PSTD -O2 "${INC[@]}" "${LDF[@]}" -o "$B/p1_floor_layout.exe" "$O/p1_floor_layout.c" "$S/floor_layout.c" \
    "$S/actor_spawn.c" "$S/audio_mci.c" "$S/call_trace.c" "$S/rng.c"
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
# P1 haggle offers: retail FUN_00460161/FUN_004603cf vs port built c11 AND gnu11
$CC -std=c11 -O2 "${INC[@]}" -c -o "$B/hag_c11.o" "$S/customer_haggle.c"
$CC -std=gnu11 -O2 "${INC[@]}" -Dhaggle_offer_up=haggle_offer_up_gnu11 -Dhaggle_offer_down=haggle_offer_down_gnu11 \
    -Dhaggle_decide=haggle_decide_gnu11 -Dhaggle_budget_ceiling=haggle_budget_ceiling_gnu11 -c -o "$B/hag_gnu11r.o" "$S/customer_haggle.c"
$CC -std=c11 -O2 "${INC[@]}" "${LDF[@]}" -o "$B/p1_haggle_offers.exe" "$O/p1_haggle_offers.c" "$B/hag_c11.o" "$B/hag_gnu11r.o" "$S/rng.c"
# P1 math3d: retail D3DX8 (dispatched variant, fast table @0x5fda48) vs port math3d.c built c11 AND gnu11
M3REN=(); for f in vec3_normalize mat4_lookat_rh mat4_perspective_fov_rh mat4_mul mat4_identity mat4_translation \
    mat4_scaling mat4_rotation_x mat4_rotation_y mat4_rotation_z plane_from_point_normal mat4_shadow mat4_inverse; do
    M3REN+=("-D$f=${f}_gnu11"); done
$CC -std=c11   -O2 "${INC[@]}" -c -o "$B/m3_c11.o"   "$S/math3d.c"
$CC -std=gnu11 -O2 "${INC[@]}" "${M3REN[@]}" -c -o "$B/m3_gnu11.o" "$S/math3d.c"
$CC -std=c11 -O2 "${INC[@]}" "${LDF[@]}" -o "$B/p1_math3d.exe" "$O/p1_math3d.c" "$B/m3_c11.o" "$B/m3_gnu11.o"
ls -la "$B"/*.exe
