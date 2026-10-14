#!/usr/bin/env bash
# Build rhost.exe (+ the v3 d3d8 proxy it can load) into build/rhost/.
# Host links at 0x200000 without ASLR so retail's 0x400000.. range stays ours.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd); S=$ROOT/src; H=$ROOT/tools/rhost; B=$ROOT/build/rhost
CC=${RHOST_CC:-i686-w64-mingw32-gcc}
LDF=(-Wl,--image-base,0x200000 -Wl,--large-address-aware -Wl,--disable-dynamicbase -static-libgcc)
# port code is compiled exactly as the port is (see src/Makefile CFLAGS -std)
PORT_STD=${PORT_STD:-$(sed -n 's/^CFLAGS *?= *\(-std=[a-z0-9]*\).*/\1/p' "$S/Makefile")}
mkdir -p "$B"
$CC $PORT_STD -O2 -I"$S" -include "$S/d3d_trace_macros.h" -c -o "$B/customer_haggle.o" "$S/customer_haggle.c"
$CC $PORT_STD -O2 -I"$S" -include "$S/d3d_trace_macros.h" -c -o "$B/rng.o" "$S/rng.c"
PORT_OBJS=("$B/customer_haggle.o" "$B/rng.o")
for f in floor_layout actor_spawn audio_mci call_trace customer_seller; do
    $CC $PORT_STD -O2 -I"$S" -include "$S/d3d_trace_macros.h" -c -o "$B/$f.o" "$S/$f.c"
    PORT_OBJS+=("$B/$f.o")
done
$CC -std=gnu11 -O2 -Wall -Wno-unused-function -Wno-format-truncation -I"$S" -I"$H" "${LDF[@]}" -o "$B/rhost.exe" \
    "$H"/rhost.c "$H"/rh_heap.c "$H"/rh_files.c "$H"/rh_state.c "$H"/rh_trace.c "$H"/rh_hooks.c "$H"/rh_handles.c "$H"/rh_shot.c "$H"/rh_dinput.c \
    "$S/input_segtrace.c" "${PORT_OBJS[@]}" -luser32 -lgdi32
make -s -C "$ROOT/tools/trace_studio_v3/proxy" d3d8.dll
cp "$ROOT/tools/trace_studio_v3/proxy/d3d8.dll" "$B/v3proxy_d3d8.dll"
echo "built $B/rhost.exe (port std: $PORT_STD)"
