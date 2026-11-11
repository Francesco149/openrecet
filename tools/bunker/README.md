# bunker — rhost headless under wine in ONE bounded pool (temporary NixOS box)

The third rhost host, next to Windows and maximus-wine. Host `bunker` (NixOS 26.11, i5-10600K 6c/12t, 16 GB incl.
~5–8 GB ZFS ARC, the owner's desktop session). `RHOST_BUNKER` = ssh target (default `root@10.0.10.58`). **Temporary:
access is lost when it reboots** — then every bunker path fails fast (`rd` under `auto` skips it).

Used by:
- **`rd`** live sessions: `RD_HOST=bunker`, or `auto` (win → bunker → wine); `search -j N` workers spill onto it
  (`tools/drive/README.md` "Where sessions run").
- **`rhost.py … --target bunker`**: `run --headless`, `headless-check`, `snap-check`, `skip-check`, and `rd verify` of
  a bunker session (`rhost.run_batch` → `bunker.run_batch`). Headless only; not the port / gates (the port builds on
  maximus and runs on Windows).
- the standalone CLI:

```sh
python3 tools/bunker/bunker.py setup                         # once: work dir, nix GC roots, wine prefix
python3 tools/bunker/bunker.py run SCEN.. --check golden -j 8 # headless replays vs the Windows real-device goldens
python3 tools/bunker/bunker.py run SCEN.. --check snap        # --snap-test 30%:70%:2 + golden frame / RNG / save.dat
python3 tools/bunker/bunker.py run SCEN.. --check skip -j 4   # plain vs --skip-fn 414ee2, masked (drive.py SKIP_MASK)
python3 tools/bunker/bunker.py run SCEN [--frames N] [--extra " --dump-at 100"] [--keep]   # any run
python3 tools/bunker/bunker.py status | teardown
```

A batch stages first — rhost build if stale → `root/bin/<sha12>`, the game dir, the scripts, each only when its stamp
differs (one ssh round trip when all are current) — builds the batch with `rhost.make_batch` (the identical argv.json
as `rhost.py run --headless --target wine`), pushes it, runs it in one container in the pool (`batch.py`, -j at a
time), fetches the run dirs to `/mnt/ephemeral/openrecet/bunker-runs/<batch>/`; `--check golden` compares with
`rhost.headless_compare` (= `headless-check`).

## ssh: one multiplexed connection
bunker's sshd refuses past `MaxStartups 10:30:100`. Every command, rsync (`-e ssh -o ControlPath=…`) and rd session
forward rides ONE master: `ControlPath=$XDG_RUNTIME_DIR/orc-bunker-<host>.ctl`, started once under a lock
(`ssh -fN -o ControlMaster=yes -o ControlPersist=2h`), checked with `ssh -O check` (local, ~5 ms); sessions add
`ssh -O forward -L lp:127.0.0.1:port` and remove it with `-O cancel`. Clients use `ControlMaster=no`.

## The pool (`pool.py`, runs on bunker as root)
- One transient unit **`orc-bunker-pool.service`** (`sleep infinity` holding a cgroup): `MemoryMax=6G MemorySwapMax=0`
  (OOM-kill inside, never swap the desktop), `CPUQuota=800% AllowedCPUs=0-3,6-9` (physical cores 4 and 5 = CPUs
  4,5,10,11 stay free). Env overrides on the box: `ORC_MEM ORC_CPUS ORC_QUOTA ORC_WINE`. Created on first use
  (`pool.py ensure`), `--collect`.
- **Everything joins it**: each launcher writes its pid to the pool's `cgroup.procs` before it execs bwrap — batches
  (`pool.py run NAME CMD..`) and rd sessions (`pool.py spawn`). The 6 G / 8-CPU bound is over ALL bunker processes
  together, not per container (per-scope bounds would sum above it).
- **Admission** under one lock (`root/.admit.lock`, shared with `batch.py` inside the containers, which sees the pool's
  cgroup read-only at `/pool`): free in the bound = `memory.max − (memory.current − (file − shmem))`.
  rd session: ≤ `--max` (`RD_BUNKER_MAX_SESSIONS`, 6) sessions and ≥ `--floor-mb` (`RD_BUNKER_MEM_FLOOR_MB`, 1024)
  free after reserving 700 MB per session still replaying (no `live.port` yet), and host MemAvailable ≥ 1 GB
  (`ORC_HOST_FLOOR_MB`); else `NOADMIT` after `--wait` s. Batch run: ≥ 600 MB free (+ 350 MB per run it started in
  the last 5 s) and host ≥ 1 GB, else it waits.
- `pool.py stat` (= `bunker.py status`): sessions, free in the bound, `memory.peak`.

## Container (bwrap per batch / per session)
- `bwrap --unshare-all --share-net --uid 1000 --gid 1000 --cap-drop ALL --die-with-parent --new-session`:
  `/nix/store` read-only, `/proc` `/dev` `/tmp`(tmpfs), writable only `/orc`; the prefix is a **copy-on-write overlay**
  (`--overlay-src prefix-headless --tmp-overlay /prefix`): each container has its own wineserver over a private view,
  so concurrent containers never write one prefix; scripts read-only at `/bunker`; `--clearenv`, PATH = wine + python
  store paths, `WINEDEBUG=-all`, no DISPLAY / WAYLAND_DISPLAY (headless = wine's null driver), `WINEDLLOVERRIDES=mscoree,mshtml=`.
  Loopback TCP works (shared net: the live port is on the box's 127.0.0.1).
- A batch = one container: all its runs share one wineserver; `batch.py` = rhost.py's run_batch_wine loop (RESULT in
  rhost.log → 20 s → kill the process group), `wineserver -k` at the end.
- A session = its own container: `sh -c '"$@"; :' sh bwrap …` started detached (setsid) — the `sh` stays bwrap's
  parent, so `--die-with-parent` does not end the session when the spawning ssh command returns, and killing the
  `sh` (`pool.py kill PID NAME`, only when the PID's argv names that session's run dir) takes bwrap's pid namespace
  (wine, wineserver) with it.
- `setup` makes the prefix with `pool.py run setup --rw-prefix …` (the one writable use of it).
- rsync `--chown=root:root` / tar extraction chowns to root: the container's uid 1000 is host root; files owned by
  another host uid are unwritable inside.

## Layout on bunker — everything under `/var/lib/orc-bunker/`
| path | what |
|---|---|
| `gcroots/{wine-wow64-unstable,bwrap,python,fontconfig}` + `wine` → the first | `nix build --out-link` (cache.nixos.org, no source builds) |
| `bin/{pool.py,batch.py}` | the pool / container launcher (host side), the in-container batch runner |
| `root/` = `C:\orc-rhost` (`/orc` inside) | `game/` (617 MB, the user's own files, dereferenced), `bin/<sha12>/rhost.exe`, `runs/<batch>/` (deleted after fetch unless `--keep`), `runs/drive/<session>/` (rd sessions; removed by `rd stop`), `.admit.lock` |
| `prefix-headless/` (`/prefix` inside, overlaid) | WINEPREFIX made by `wineboot -i` in the container (582 MB); `drive_c/orc-rhost → /orc` |

Outside it: only nix's own bookkeeping — the store paths and `/nix/var/nix/gcroots/auto/*` links to the gcroots
above (teardown deletes them), and the transient `orc-bunker-pool.service` (`--collect`; `teardown` stops it with every process in it).
No `nixos-rebuild`, no services, no global installs, no `/etc` changes.

## Wine (pinned)
`wineWow64Packages.unstable` of the system nixpkgs (rev `1c89083e451e891152365b799595cf7790a43325`):
**`/nix/store/zjxh9vbxp2h12a018i0krpswfidbjh1k-wine-wow64-11.19`** — cache hit. maximus reference = wine 11.20.
The new wow64 mode runs rhost.exe (32-bit PE, image base 0x200000, LAA) correctly here: the flake.nix kernel32
failure did not reproduce with 11.19. Other cache hits: `wineWow64Packages.stable` 11.0, `.staging` 11.19,
`wine` (32-bit) 11.0 `/nix/store/31xmsymmjihim7sg40m56nn0i582vjn6-wine-11.0` (untried). `wineWowPackages.*` = cache miss.

(The first cut, ec4c4c04, ran each batch in its own `orc-bunker-<batch>.scope` with its own 6 G bound and the prefix
writable by every container; replaced by the pool and the overlay above.)

## Measured (2026-11-11, rhost bin `d418228b2e88`)
**Golden check, `-j 8`: 11/11 IDENTICAL** (batch 223 s wall, scope memory.peak 2.86 GB):

| scenario | frames | s | fps @ -j8 | peak RSS | anchors |
|---|---:|---:|---:|---:|---:|
| day2-shop | 4,708 | 7.5 | 632 | 322 MB | 24 |
| shop-session-week3 | 9,290 | 14.7 | 633 | 347 MB | 202 |
| buy-lowball-walkout | 6,533 | 10.2 | 637 | 322 MB | 36 |
| intro-opening | 703 | 1.6 | 432 | 262 MB | 23 |
| day2-save-commit | 33,900 | 26.1 | 1,297 | 329 MB | 1,029 |
| dungeon0-full | 44,741 | 42.9 | 1,043 | 326 MB | 300 |
| dungeon1-deep | 77,136 | 67.6 | 1,142 | 331 MB | 385 |
| hire-griff | 4,576 | 7.6 | 604 | 307 MB | 24 |
| obsidian-16f-20f-golem | 15,477 | 23.4 | 661 | 348 MB | 79 |
| obsidian-21f-30f-griff | 39,976 | 39.2 | 1,018 | 333 MB | 237 |
| dungeon3-deep | 294,568 | 222.2 | 1,326 | 352 MB | 1,010 |

- `-j 1` (box otherwise idle): dungeon1-deep 1,459 fps, day2-shop 1,231 fps. 8 at once ≈ 0.5–0.9× per process.
- **snap** (`--snap-test 30%:70%:2`): hire-griff PASS (dirty 4,027 pages, peak RSS 592 MB), obsidian-16f-20f-golem
  PASS (dirty 23,981, 63 MB slabs, 16 decommits, peak RSS 680 MB); both end on the golden's frame / RNG / save.dat.
- **skip** (`--skip-fn 414ee2 --mask-range 76b750:76b7b0,64c514:64da38`, -j4): day2-shop, hire-griff,
  obsidian-16f-20f-golem, dungeon1-deep 4/4 EQUAL (frame, RNG, state_sem, save.dat); wall 4.1→2.5 s,
  4.5→2.6, 14.8→9.0, 54.9→29.0 (×1.6–1.9).
- **Processes per 6 GB bound:** plain replay ≈ 0.33–0.35 GB each → ~16 by memory; CPU (8 threads) caps useful
  parallelism at 8 (2.86 GB at -j8). With savestates (snap-test / live-style) ≈ 0.6–0.7 GB each → ~8; a live `rd`
  session's slab pool grows with savestates kept (up to 960 MB of slabs), budget ~1 GB each → ~5–6.

## Measured with the pool (2026-11-11, bin `d418228b2e88`)
- `rhost.py headless-check --target bunker -j 8` on 10 scenarios (all above but dungeon3-deep): **10/10 IDENTICAL**,
  237,040 frames, 67 s batch, pool peak 2.5 GB.
- `rhost.py snap-check --target bunker -j 6` (same 10): **10/10 PASS**, 112 s (six child batches = six containers in one pool).
- `rhost.py skip-check 414ee2 --mask 76b750:76b7b0,64c514:64da38 --target bunker -j 8` (same 10): **10/10 equal**,
  wall plain 217 s → skip 115 s (×1.89).
- rd sessions, the auto spread and `state_sem` vs the other hosts: `tools/drive/README.md` "Where sessions run".
  bunker's game state = maximus wine's (one DLL-pointer slot `0x964c448` differs, so `state_sem` differs);
  Windows differs by float ulps.

## Teardown
```sh
python3 tools/bunker/bunker.py teardown      # stops orc-bunker-* units (the pool: every bunker process), rm -rf /var/lib/orc-bunker,
                                             # its gcroots/auto links; closes the ssh master
ssh root@10.0.10.58 nix-collect-garbage      # optional: frees the store paths (wine 755 MB + deps)
```
