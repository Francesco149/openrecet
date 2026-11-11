#!/bin/sh
# Runs on bunker as root: CMD.. inside the orc-bunker container — a transient systemd scope (memory / CPU bound)
# around a bubblewrap sandbox (own user / pid / ipc / uts / mount namespaces, network shared; /nix/store read-only;
# only $W/root (= C:\orc-rhost) and $W/prefix-headless writable). The scope's memory.peak is printed at the end.
#   container.sh NAME CMD..      env: ORC_MEM (6G) ORC_CPUS (0-3,6-9) ORC_QUOTA (800%) ORC_WINE (gcroots link name)
set -eu
W=/var/lib/orc-bunker
NAME=$1; shift
WP=$(readlink -f "$W/gcroots/${ORC_WINE:-wine}")
BW=$(readlink -f "$W/gcroots/bwrap")/bin/bwrap
PY=$(readlink -f "$W/gcroots/python")/bin/python3
FC=$(readlink -f "$W/gcroots/fontconfig")/etc/fonts/fonts.conf
exec systemd-run --quiet --scope --collect --unit="orc-bunker-$NAME" \
  -p MemoryMax="${ORC_MEM:-6G}" -p MemorySwapMax=0 -p CPUQuota="${ORC_QUOTA:-800%}" -p AllowedCPUs="${ORC_CPUS:-0-3,6-9}" \
  /bin/sh -c '
    "$@"; rc=$?
    cg=/sys/fs/cgroup$(cut -d: -f3 /proc/self/cgroup)
    echo "[bunker] scope memory.peak $(cat $cg/memory.peak) bytes (MemoryMax $(cat $cg/memory.max))"
    exit $rc' sh \
  "$BW" --unshare-all --share-net --uid 1000 --gid 1000 --die-with-parent --new-session --cap-drop ALL \
    --ro-bind /nix/store /nix/store --proc /proc --dev /dev --tmpfs /tmp --dir /home/orc \
    --bind "$W/root" /orc --bind "$W/prefix-headless" /prefix --ro-bind "$W/bin" /bunker \
    --clearenv --setenv PATH "$WP/bin:$(dirname "$PY")" --setenv HOME /home/orc --setenv USER orc \
    --setenv XDG_RUNTIME_DIR /tmp --setenv FONTCONFIG_FILE "$FC" \
    --setenv WINEPREFIX /prefix --setenv WINEDEBUG -all --setenv WINEDLLOVERRIDES "mscoree,mshtml=" \
    "$@"
