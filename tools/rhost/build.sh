#!/usr/bin/env bash
# Build rhost.exe (+ the v3 d3d8 proxy it can load) into build/rhost/.
# Host links at 0x200000 without ASLR so retail's 0x400000.. range stays ours.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd); S=$ROOT/src; H=$ROOT/tools/rhost; B=$ROOT/build/rhost
CC=${RHOST_CC:-i686-w64-mingw32-gcc}
# --no-insert-timestamp: the same sources give the same bytes, so a rebuild keeps the content-addressed bin\<sha>
# (and the live sessions that run from it)
LDF=(-Wl,--image-base,0x200000 -Wl,--large-address-aware -Wl,--disable-dynamicbase -Wl,--no-insert-timestamp -static-libgcc)
# port code is compiled exactly as the port is (see src/Makefile CFLAGS -std)
PORT_STD=${PORT_STD:-$(sed -n 's/^CFLAGS *?= *\(-std=[a-z0-9]*\).*/\1/p' "$S/Makefile")}
mkdir -p "$B"
# one build at a time, and nothing half-written is ever visible: several agents call rhost.py / rd at once, and each
# rebuilds when a source is newer than rhost.exe (2026-11-10: two builds raced on rh_dev_gen.h)
exec 9>"$B/.build.lock"; flock 9
$CC $PORT_STD -O2 -I"$S" -include "$S/d3d_trace_macros.h" -c -o "$B/customer_haggle.o" "$S/customer_haggle.c"
$CC $PORT_STD -O2 -I"$S" -include "$S/d3d_trace_macros.h" -c -o "$B/rng.o" "$S/rng.c"
PORT_OBJS=("$B/customer_haggle.o" "$B/rng.o")
for f in floor_layout actor_spawn audio_mci call_trace customer_seller map_collision math3d actor_table enemy_place party; do
    $CC $PORT_STD -O2 -I"$S" -include "$S/d3d_trace_macros.h" -c -o "$B/$f.o" "$S/$f.c"
    PORT_OBJS+=("$B/$f.o")
done
python3 "$H/gen_dev.py" "$H/rh_dev.c" > "$B/rh_dev_gen.h.tmp" && mv "$B/rh_dev_gen.h.tmp" "$B/rh_dev_gen.h"
$CC -std=gnu11 -O2 -Wall -Wno-unused-function -Wno-format-truncation -I"$S" -I"$H" -I"$B" "${LDF[@]}" -o "$B/rhost.exe.tmp" \
    "$H"/rhost.c "$H"/rh_heap.c "$H"/rh_files.c "$H"/rh_state.c "$H"/rh_trace.c "$H"/rh_hooks.c "$H"/rh_handles.c "$H"/rh_shot.c "$H"/rh_dinput.c "$H"/rh_play.c "$H"/rh_d3dprobe.c "$H"/rh_dev.c "$H"/rh_snap.c "$H"/rh_live.c \
    "$S/input_segtrace.c" "${PORT_OBJS[@]}" -luser32 -lgdi32 -lole32 -lws2_32 -lwinmm
mv "$B/rhost.exe.tmp" "$B/rhost.exe"
make -s -C "$ROOT/tools/trace_studio_v3/proxy" d3d8.dll
cp "$ROOT/tools/trace_studio_v3/proxy/d3d8.dll" "$B/v3proxy_d3d8.dll"
echo "built $B/rhost.exe (port std: $PORT_STD)"
