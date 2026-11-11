#!/usr/bin/env python3
"""bunker: rhost HEADLESS under wine on a temporary NixOS box, inside a bounded container (tools/bunker/README.md).
Standalone: not wired into rd / rhost.py; it imports rhost.py only for the batch layout (make_batch = the same argv
as `rhost.py run --headless --target wine`) and the golden comparison (headless_compare).

    bunker.py setup                        # work dir, nix GC roots (wine, bwrap, python, fontconfig), the prefix
    bunker.py stage                        # rhost build (if stale) + game dir + this dir's scripts
    bunker.py run SCEN.. [-j N] [--extra " --x"] [--frames N] [--check golden|snap|skip] [--keep]
    bunker.py status                       # running scopes, disk, memory
    bunker.py teardown                     # stop the scopes, delete /var/lib/orc-bunker (GC roots with it)

--check golden: the headless replay (golden loggers) vs the Windows real-device golden = rhost.py headless-check.
--check snap:   + `--snap-test A:B:2` at 30% / 70% of the golden; PASS + golden frame / RNG / save.dat.
--check skip:   two batches, `--mask-range M` and `--skip-fn 414ee2 --mask-range M` (M = drive.py's SKIP_MASK):
                equal status / frame / RNG / state_sem / save.dat = rhost.py skip-check.
Host: RHOST_BUNKER (default root@10.0.10.58). Local results: RHOST_BUNKER_RUNS (/mnt/ephemeral/openrecet/bunker-runs).
"""
import argparse
import copy
import gzip
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "rhost"))
import rhost  # noqa: E402

HOST = os.environ.get("RHOST_BUNKER", "root@10.0.10.58")
W = "/var/lib/orc-bunker"
LOCAL = os.environ.get("RHOST_BUNKER_RUNS", "/mnt/ephemeral/openrecet/bunker-runs")
# nixpkgs attr → gcroots link. wine: wineWow64Packages.unstable (11.19, cache.nixos.org) — see README for the pin
NIX = {"wine-wow64-unstable": "wineWow64Packages.unstable", "bwrap": "bubblewrap", "python": "python3",
       "fontconfig": "fontconfig.out"}
WINE = "wine-wow64-unstable"
SKIP_FN = "414ee2"
SKIP_MASK = "76b750:76b7b0,64c514:64da38"      # = tools/drive/drive.py SKIP_MASK (2026-11-11)


def ssh(cmd, check=True, capture=False):
    """CMD on the bunker (root) → its exit code (stdout with capture)"""
    r = subprocess.run(["ssh", "-o", "BatchMode=yes", HOST, cmd], text=True,
                       stdout=subprocess.PIPE if capture else None)
    if check and r.returncode:
        raise SystemExit(f"ssh {HOST}: exit {r.returncode}: {cmd[:200]}")
    return r.stdout if capture else r.returncode


def rsync(*args):
    # --chown: the container maps its uid 1000 to root; files owned by the sender's uid would be unwritable inside
    subprocess.run(["rsync", "-a", "--chown=root:root"] + list(args), check=True)


def cmd_setup(a):
    links = " && ".join(f"nix build --out-link gcroots/{k} nixpkgs#{v}" for k, v in NIX.items())
    ssh(f"set -e; mkdir -p {W}/gcroots {W}/bin {W}/root/game {W}/root/bin {W}/root/runs {W}/prefix-headless; cd {W}; "
        f"{links}; ln -sfn {WINE} gcroots/wine; readlink -f gcroots/*")
    cmd_stage_scripts()
    if ssh(f"test -e {W}/prefix-headless/system.reg", check=False):
        print("[creating the headless wine prefix (once)]")
        ssh(f"{W}/bin/container.sh setup python3 -c 'import subprocess as s; s.run([\"wineboot\", \"-i\"], capture_output=True); "
            f"s.run([\"wineserver\", \"-w\"])'")
    ssh(f"ln -sfn /orc {W}/prefix-headless/drive_c/orc-rhost")


def cmd_stage_scripts():
    rsync(os.path.join(HERE, "container.sh"), os.path.join(HERE, "batch.py"), f"{HOST}:{W}/bin/")


def bin_sha():
    return rhost.bin_dir().rsplit("\\", 1)[1]


def cmd_stage(a=None):
    if rhost.rhost_stale():
        subprocess.run(["bash", os.path.join(ROOT, "tools", "rhost", "build.sh")], check=True)
    sha = bin_sha()
    ssh(f"mkdir -p {W}/root/bin/{sha}")
    rsync(os.path.join(rhost.BUILD, "rhost.exe"), os.path.join(rhost.BUILD, "v3proxy_d3d8.dll"), f"{HOST}:{W}/root/bin/{sha}/")
    rsync("-L", rhost.WINE_ROOT + "/game/", f"{HOST}:{W}/root/game/")     # symlinks into the Steam dir, dereferenced
    cmd_stage_scripts()
    return sha


def run_ns(a, extra):
    b = rhost.replay_args(target="wine", headless=True, flatten_out=True, frames=a.frames, extra=extra)
    b.jobs = a.j
    return b


def remote_batch(a, runs, tag, extra):
    """make the batch locally (rhost.make_batch → argv.json per run), push, run in the container, fetch → (bdir, {name: RESULT})"""
    sha = cmd_stage()
    os.makedirs(LOCAL, exist_ok=True)
    saved, rhost.LOCAL_RUNS = rhost.LOCAL_RUNS, LOCAL
    os.environ["RHOST_BIN_DIR"] = rhost.WROOT + "\\bin\\" + sha
    try:
        batch, bdir = rhost.make_batch(run_ns(a, extra), runs, tag)
    finally:
        rhost.LOCAL_RUNS = saved
    os.remove(os.path.join(bdir, "go.ps1"))       # the Windows runner script: not used here
    rsync(bdir + "/", f"{HOST}:{W}/root/runs/{batch}/")
    t0 = time.time()
    rc = ssh(f"{W}/bin/container.sh {batch} python3 /bunker/batch.py /orc/runs/{batch} "
             f"'C:\\orc-rhost\\bin\\{sha}\\rhost.exe' -j {a.j}", check=False)
    wall = time.time() - t0
    rsync("--exclude=save.dat.*", f"{HOST}:{W}/root/runs/{batch}/", bdir + "/")
    if not a.keep:
        ssh(f"rm -rf {W}/root/runs/{batch}")
    res = {}
    for n, *_ in runs:
        log = os.path.join(bdir, n, "rhost.log")
        lines = [l for l in open(log, errors="replace") if l.startswith("RESULT")] if os.path.exists(log) else []
        if lines:
            res[n] = dict(kv.split("=", 1) for kv in lines[-1].split()[1:] if "=" in kv)
    print(f"[bunker batch {batch}: {len(runs)} run(s) in {wall:.0f}s, -j{a.j}, container rc {rc} → {bdir}]")
    return bdir, res


def bstat(bdir, n):
    try:
        return json.load(open(os.path.join(bdir, n, "bunker.json")))
    except OSError:
        return {}


def cmd_run(a):
    names = a.scenarios
    runs = []
    for n in names:
        tr, save = rhost.scenario_inputs(n)
        runs.append((n, tr, None, save))
    ref = argparse.Namespace(target="win")
    gold = {n: rhost.golden_dir(ref, n, tr, save) for n, tr, _, save in runs}
    extra = a.extra or None
    if a.check in ("golden", "snap"):
        miss = [n for n in names if not os.path.exists(os.path.join(gold[n], "meta.json"))]
        if miss:
            raise SystemExit(f"no Windows golden for {miss}: `rhost.py golden {' '.join(miss)}` first")
    if a.check == "golden":
        extra = (rhost.GOLDEN_EXTRA + " " + (a.extra or "")).strip()
    ok = 0
    if a.check == "snap":            # A / B differ per scenario: one batch each
        for n, tr, m, save in runs:
            fr = int(json.load(open(os.path.join(gold[n], "meta.json")))["result"]["frame"])
            bd, res = remote_batch(a, [(n, tr, m, save)], "bunker-snap", f"--snap-test {int(fr * 0.3)}:{int(fr * 0.7)}:2 {a.extra or ''}".strip())
            r, g = res.get(n, {}), json.load(open(os.path.join(gold[n], "meta.json")))["result"]
            bad = [f"{k} {g.get(k)}→{r.get(k)}" for k in ("frame", "rng", "complete") if g.get(k) != r.get(k)]
            if r.get("snap_test") != "PASS":
                bad.append(f"snap_test={r.get('snap_test')}")
            gs, hs = os.path.join(gold[n], "save.dat.gz"), os.path.join(bd, n, "save.dat")
            if os.path.exists(gs) and (not os.path.exists(hs) or gzip.open(gs, "rb").read() != open(hs, "rb").read()):
                bad.append("save.dat differs from the golden's")
            st = next((l.strip() for l in open(os.path.join(bd, n, "rhost.log"), errors="replace") if l.startswith("snap:")), "")
            ok += not bad
            print(f"{n:32s} f={fr} [{int(fr * 0.3)}..{int(fr * 0.7)}] {'PASS' if not bad else 'FAIL ' + '; '.join(bad)}  {st[:160]}")
        print(f"bunker snap: {ok}/{len(runs)} PASS")
        return 0 if ok == len(runs) else 1
    if a.check == "skip":
        mask = f"--mask-range {SKIP_MASK}"
        a2 = copy.copy(a); a2.frames = a.frames
        bp, rp = remote_batch(a2, runs, "bunker-skip-plain", f"{mask} {a.extra or ''}".strip())
        bs, rs = remote_batch(a2, runs, "bunker-skip-skip", f"--skip-fn {SKIP_FN} {mask} {a.extra or ''}".strip())
        for n, *_ in runs:
            p, s = rp.get(n, {}), rs.get(n, {})
            bad = [f"{k} {p.get(k)}→{s.get(k)}" for k in ("status", "frame", "rng", "state_sem", "complete") if p.get(k) != s.get(k)]
            fp, fs = os.path.join(bp, n, "save.dat"), os.path.join(bs, n, "save.dat")
            if os.path.exists(fp) != os.path.exists(fs) or (os.path.exists(fp) and open(fp, "rb").read() != open(fs, "rb").read()):
                bad.append("save.dat differs")
            if not p or not s:
                bad = ["no result"]
            ok += not bad
            print(f"{n:32s} f={p.get('frame')} plain {p.get('wall_ms')}ms skip {s.get('wall_ms')}ms sem={p.get('state_sem')} "
                  + ("EQUAL" if not bad else "DIFF " + "; ".join(bad)))
        print(f"bunker skip {SKIP_FN}: {ok}/{len(runs)} equal (frame, RNG, state_sem, save.dat)")
        return 0 if ok == len(runs) else 1
    bd, res = remote_batch(a, runs, "bunker-run", extra)
    for n, *_ in runs:
        r, st = res.get(n), bstat(bd, n)
        if not r:
            print(f"{n:32s} NO RESULT  → {os.path.join(bd, n)}")
            continue
        fr, ms = int(r.get("frame", "0") or 0), float(r.get("wall_ms", "0") or 0)
        line = (f"{n:32s} f={fr:>7d} {ms / 1000:7.1f}s {fr / max(ms / 1000, 1e-3):7.0f} fps  "
                f"peak rss {st.get('peak_rss_kb', 0) // 1024:5d} MB  rng={r.get('rng')}")
        if a.check == "golden":
            bad, na = rhost.headless_compare(gold[n], os.path.join(bd, n), r)
            ok += not bad
            line += f"  anchors={na}  " + ("IDENTICAL" if not bad else "DIFF " + "; ".join(bad))
        print(line)
    if a.check == "golden":
        print(f"bunker golden check: {ok}/{len(runs)} IDENTICAL to the Windows real-device goldens")
        return 0 if ok == len(runs) else 1
    return 0


def cmd_status(a):
    ssh(f"systemctl list-units --no-legend 'orc-bunker-*' ; du -sh {W} 2>/dev/null; free -m | head -2")


def cmd_teardown(a):
    ssh(f"for u in $(systemctl list-units --no-legend --plain 'orc-bunker-*' | cut -d' ' -f1); do systemctl stop $u; done; "
        f"rm -rf {W}; find /nix/var/nix/gcroots/auto -lname '{W}/*' -print -delete")
    print(f"[torn down: {W} removed; its GC roots go with it — `nix-collect-garbage` on {HOST} frees the store paths]")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = p.add_subparsers(dest="cmd", required=True)
    sp.add_parser("setup").set_defaults(fn=cmd_setup)
    sp.add_parser("stage").set_defaults(fn=cmd_stage)
    sp.add_parser("status").set_defaults(fn=cmd_status)
    sp.add_parser("teardown").set_defaults(fn=cmd_teardown)
    r = sp.add_parser("run")
    r.add_argument("scenarios", nargs="+")
    r.add_argument("-j", type=int, default=4)
    r.add_argument("--frames", type=int)
    r.add_argument("--extra", default="")
    r.add_argument("--check", choices=("golden", "snap", "skip"))
    r.add_argument("--keep", action="store_true", help="keep the run dirs on the bunker")
    r.set_defaults(fn=cmd_run)
    a = p.parse_args()
    sys.exit(a.fn(a) or 0)


if __name__ == "__main__":
    main()
