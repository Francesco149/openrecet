#!/usr/bin/env python3
"""bunker: rhost HEADLESS under wine on a temporary NixOS box, inside ONE bounded pool (tools/bunker/README.md).
Wired in as a third host: `rd` live sessions (RD_HOST=bunker, or `auto`: win → bunker → wine) and `rhost.py …
--target bunker` (run --headless / headless-check / snap-check / skip-check) — both through this module.

    bunker.py setup                        # work dir, nix GC roots (wine, bwrap, python, fontconfig), the prefix
    bunker.py stage                        # rhost build (if stale) + game dir + this dir's scripts (cached by signature)
    bunker.py run SCEN.. [-j N] [--extra " --x"] [--frames N] [--check golden|snap|skip] [--keep]
    bunker.py status                       # the pool: rd sessions, memory; disk
    bunker.py teardown                     # stop the pool (every bunker process), delete /var/lib/orc-bunker

ssh: ONE multiplexed connection (ControlMaster, ControlPersist 2h, socket CTL) carries every command, rsync and rd
session forward — bunker's sshd refuses connections past `MaxStartups 10:30:100`.
--check golden: the headless replay (golden loggers) vs the Windows real-device golden = rhost.py headless-check.
--check snap:   + `--snap-test A:B:2` at 30% / 70% of the golden; PASS + golden frame / RNG / save.dat.
--check skip:   two batches, `--mask-range M` and `--skip-fn 414ee2 --mask-range M` (M = drive.py's SKIP_MASK).
Host: RHOST_BUNKER (default root@10.0.10.58). Local results: RHOST_BUNKER_RUNS (/mnt/ephemeral/openrecet/bunker-runs).
"""
import argparse
import copy
import fcntl
import gzip
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "rhost"))
import rhost  # noqa: E402

HOST = os.environ.get("RHOST_BUNKER", "root@10.0.10.58")
W = "/var/lib/orc-bunker"
PY = f"{W}/gcroots/python/bin/python3"
POOL = f"{PY} {W}/bin/pool.py"
LOCAL = os.environ.get("RHOST_BUNKER_RUNS", "/mnt/ephemeral/openrecet/bunker-runs")
# nixpkgs attr → gcroots link. wine: wineWow64Packages.unstable (11.19, cache.nixos.org) — see README for the pin
NIX = {"wine-wow64-unstable": "wineWow64Packages.unstable", "bwrap": "bubblewrap", "python": "python3",
       "fontconfig": "fontconfig.out"}
WINE = "wine-wow64-unstable"
SKIP_FN = "414ee2"
SKIP_MASK = "76b750:76b7b0,64c514:64da38"      # = tools/drive/drive.py SKIP_MASK (2026-11-11)
SCRIPTS = ("pool.py", "batch.py")

# ---- the one ssh connection
CTL = os.path.join(os.environ.get("XDG_RUNTIME_DIR") or f"/tmp/orc-{os.getuid()}", f"orc-bunker-{re.sub(r'[^A-Za-z0-9.]', '_', HOST)}.ctl")
OPTS = ["-o", "BatchMode=yes", "-o", f"ControlPath={CTL}", "-o", "ControlMaster=no", "-o", "ServerAliveInterval=30",
        "-o", "ServerAliveCountMax=4"]
RSH = "ssh " + " ".join(OPTS)


def master_up():
    return subprocess.run(["ssh"] + OPTS + ["-O", "check", HOST], capture_output=True).returncode == 0


def ensure_master(timeout=8):
    """the multiplexing master (started once, under a lock: concurrent rd / rhost processes share it) → True if up"""
    if master_up():
        return True
    os.makedirs(os.path.dirname(CTL), exist_ok=True)
    with open(CTL + ".lock", "w") as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        if master_up():
            return True
        try:
            os.remove(CTL)                              # a stale socket of a dead master
        except OSError:
            pass
        r = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", f"ConnectTimeout={timeout}", "-o", f"ControlPath={CTL}",
                            "-o", "ControlMaster=yes", "-o", "ControlPersist=2h", "-o", "ServerAliveInterval=30",
                            "-o", "ServerAliveCountMax=4", "-fN", HOST],
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
                           timeout=timeout + 10)
        return r.returncode == 0 and master_up()


def up(timeout=3):
    """bunker answers (the master is up or can be made within TIMEOUT s)"""
    try:
        return ensure_master(timeout)
    except (subprocess.TimeoutExpired, OSError):
        return False


def ssh(cmd, check=True, capture=False, input=None, timeout=None):
    """CMD on the bunker (root), over the master → its exit code (stdout with capture)"""
    if not ensure_master():
        raise SystemExit(f"bunker ({HOST}) unreachable: no ssh master")
    r = subprocess.run(["ssh"] + OPTS + [HOST, cmd], text=input is None or isinstance(input, str),
                       stdout=subprocess.PIPE if capture else None, input=input, timeout=timeout)
    if check and r.returncode:
        raise SystemExit(f"ssh {HOST}: exit {r.returncode}: {cmd[:200]}")
    return r.stdout if capture else r.returncode


def forward(lport, rport, cancel=False):
    """add (cancel) a local forward 127.0.0.1:LPORT → bunker 127.0.0.1:RPORT on the master → ok"""
    return subprocess.run(["ssh"] + OPTS + ["-O", "cancel" if cancel else "forward", "-L", f"127.0.0.1:{lport}:127.0.0.1:{rport}", HOST],
                          capture_output=True).returncode == 0


def rsync(*args):
    # --chown: the container maps its uid 1000 to root; files owned by the sender's uid would be unwritable inside
    if not ensure_master():
        raise SystemExit(f"bunker ({HOST}) unreachable: no ssh master")
    subprocess.run(["rsync", "-a", "--chown=root:root", "-e", RSH] + list(args), check=True)


def cmd_setup(a):
    links = " && ".join(f"nix build --out-link gcroots/{k} nixpkgs#{v}" for k, v in NIX.items())
    ssh(f"set -e; mkdir -p {W}/gcroots {W}/bin {W}/root/game {W}/root/bin {W}/root/runs/drive {W}/prefix-headless; cd {W}; "
        f"{links}; ln -sfn {WINE} gcroots/wine; readlink -f gcroots/*")
    stage_scripts()
    if ssh(f"test -e {W}/prefix-headless/system.reg", check=False):
        print("[creating the headless wine prefix (once)]")
        ssh(f"{POOL} run setup --rw-prefix python3 -c 'import subprocess as s; s.run([\"wineboot\", \"-i\"], capture_output=True); "
            f"s.run([\"wineserver\", \"-w\"])'")
    ssh(f"ln -sfn /orc {W}/prefix-headless/drive_c/orc-rhost")


def stage_scripts():
    rsync(*[os.path.join(HERE, f) for f in SCRIPTS], f"{HOST}:{W}/bin/")


def bin_sha():
    return rhost.bin_dir().rsplit("\\", 1)[1]


def game_sig():
    """names + sizes + mtimes of the local game dir (symlinks followed) → the stamp a staged copy carries"""
    h, g = hashlib.sha1(), rhost.WINE_ROOT + "/game"
    for d, ds, fs in os.walk(g, followlinks=True):
        ds.sort()
        for f in sorted(fs):
            st = os.stat(os.path.join(d, f))
            h.update(f"{os.path.relpath(os.path.join(d, f), g)}\0{st.st_size}\0{int(st.st_mtime)}\n".encode())
    return h.hexdigest()[:16]


def scripts_sig():
    return hashlib.sha1(b"".join(open(os.path.join(HERE, f), "rb").read() for f in SCRIPTS)).hexdigest()[:16]


_staged = {}
_stage_lock = threading.Lock()


def stage(build=True):
    """rhost build (if stale, unless a parent prebuilt it) + bin\\<sha> + game + scripts on the bunker, each only when its
    stamp differs (one round trip when all are current) → sha"""
    with _stage_lock:
        if build and not rhost.PREBUILT and rhost.rhost_stale():
            rhost.cmd_build(None)
        sha = bin_sha()
        if _staged.get("sha") == sha:
            return sha
        gs, ss = game_sig(), scripts_sig()
        have = ssh(f"cat {W}/root/game/.orc-staged 2>/dev/null; echo; cat {W}/bin/.orc-staged 2>/dev/null; echo; "
                   f"test -f {W}/root/bin/{sha}/rhost.exe && echo BIN || true", capture=True)     # no BIN yet = exit 1: not an error.split("\n")
        if "BIN" not in have:
            ssh(f"mkdir -p {W}/root/bin/{sha}")
            rsync(os.path.join(rhost.BUILD, "rhost.exe"), os.path.join(rhost.BUILD, "v3proxy_d3d8.dll"), f"{HOST}:{W}/root/bin/{sha}/")
        if have[0].strip() != gs:
            rsync("-L", rhost.WINE_ROOT + "/game/", f"{HOST}:{W}/root/game/")     # symlinks into the Steam dir, dereferenced
            ssh(f"echo {gs} > {W}/root/game/.orc-staged")
        if len(have) < 2 or have[1].strip() != ss:
            stage_scripts()
            ssh(f"echo {ss} > {W}/bin/.orc-staged; mkdir -p {W}/root/runs/drive")
        _staged["sha"] = sha
        return sha


def cmd_stage(a=None):
    return stage()


def run_ns(a, extra):
    b = rhost.replay_args(target="wine", headless=True, flatten_out=True, frames=a.frames, extra=extra)
    b.jobs = a.j
    return b


def run_batch(b, runs, tag, fetch=None, keep=False):
    """rhost.run_batch's contract for --target bunker: B = rhost's options (headless only), RUNS = (name, trace, masks,
    save) → (batch, local bdir, {name: RESULT}, out). make_batch locally (the identical argv.json as `rhost.py run
    --headless --target wine`), push, run every run in one container in the pool (-j at a time), fetch the run dirs."""
    if not getattr(b, "headless", False):
        raise SystemExit("--target bunker runs headless only (no X server / GPU there): add --headless, or use win / wine")
    if getattr(b, "d3d", False) or getattr(b, "shot_at", None) or getattr(b, "shot_every", None):
        raise SystemExit("--target bunker: no real device (captures / shots need win or wine)")
    sha = stage()
    os.makedirs(LOCAL, exist_ok=True)
    saved, rhost.LOCAL_RUNS = rhost.LOCAL_RUNS, LOCAL
    try:
        batch, bdir = rhost.make_batch(b, runs, tag)       # (its go.ps1 names bin_dir(); the exe goes to batch.py below)
    finally:
        rhost.LOCAL_RUNS = saved
    os.remove(os.path.join(bdir, "go.ps1"))       # the Windows runner script: not used here
    rsync(bdir + "/", f"{HOST}:{W}/root/runs/{batch}/")
    t0 = time.time()
    jobs = max(1, getattr(b, "jobs", 1) or 1)
    to = int(getattr(b, "wine_timeout", 0) or 4 * 3600)
    rc = ssh(f"{POOL} run {batch} python3 /bunker/batch.py /orc/runs/{batch} 'C:\\orc-rhost\\bin\\{sha}\\rhost.exe' "
             f"-j {jobs} --timeout {to}", check=False)
    wall = time.time() - t0
    rsync("--exclude=save.dat.*", f"{HOST}:{W}/root/runs/{batch}/", bdir + "/")
    if not keep:
        ssh(f"rm -rf {W}/root/runs/{batch}")
    res, out_lines = {}, []
    for n, *_ in runs:
        out_lines.append(f"=== {n}")
        log = os.path.join(bdir, n, "rhost.log")
        lines = [l.rstrip("\n") for l in open(log, errors="replace") if re.search(r"^(RESULT|FATAL|DIVERGE|hook-result)", l)] \
            if os.path.exists(log) else []
        out_lines += lines
        rl = [l for l in lines if l.startswith("RESULT")]
        if rl:
            res[n] = dict(kv.split("=", 1) for kv in rl[-1].split()[1:] if "=" in kv)
        notes = [l for l in lines if not l.startswith("RESULT")]
        if notes:
            res[n + "#notes"] = notes
    print(f"[bunker batch {batch}: {len(runs)} run(s) in {wall:.0f}s, -j{jobs}, container rc {rc} → {bdir}]")
    return batch, bdir, res, "\n".join(out_lines)


def remote_batch(a, runs, tag, extra):
    _, bdir, res, _ = run_batch(run_ns(a, extra), runs, tag, keep=a.keep)
    return bdir, {k: v for k, v in res.items() if "#" not in k}


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
                f"peak rss {st.get('peak_rss_kb', 0) // 1024:5d} MB  rng={r.get('rng')} sem={r.get('state_sem')}")
        if a.check == "golden":
            bad, na = rhost.headless_compare(gold[n], os.path.join(bd, n), r)
            ok += not bad
            line += f"  anchors={na}  " + ("IDENTICAL" if not bad else "DIFF " + "; ".join(bad))
        print(line)
    if a.check == "golden":
        print(f"bunker golden check: {ok}/{len(runs)} IDENTICAL to the Windows real-device goldens")
        return 0 if ok == len(runs) else 1
    return 0


# ---- rd live sessions (tools/drive/drive.py calls these)
def spawn_session(name, files, exe_win, args, wait, cap, floor_mb):
    """FILES {name: bytes} → C:\\orc-rhost\\runs\\drive\\NAME on the bunker; admitted + started detached in the pool;
    → ("LIVE", pid, port) | ("NOADMIT", msg) | ("NOSTART", msg). One ssh round trip: the dir travels as a tar on stdin."""
    files = dict(files, **{"spawn.json": json.dumps({"exe": exe_win, "args": args}).encode()})
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as t:
        for n, data in files.items():
            ti = tarfile.TarInfo(n)
            ti.size, ti.mtime, ti.mode = len(data), int(time.time()), 0o644
            t.addfile(ti, io.BytesIO(data))
    out = ssh(f"{POOL} spawn {name} --wait {wait} --max {cap} --floor-mb {floor_mb}", check=False, capture=True,
              input=buf.getvalue(), timeout=wait + 420)
    out = out.decode(errors="replace") if isinstance(out, bytes) else (out or "")
    m = re.search(r"^LIVE (\d+) (\d+)", out, re.M)
    if m:
        return "LIVE", int(m.group(1)), int(m.group(2))
    if "NOADMIT" in out:
        return "NOADMIT", out.split("NOADMIT", 1)[1].strip()
    return "NOSTART", out[-800:]


def session_alive(pid, name):
    try:
        return ssh(f"{POOL} alive {int(pid)} {name}", check=False, capture=True, timeout=30).strip() == "ALIVE"
    except (subprocess.TimeoutExpired, SystemExit):
        return False


def kill_session(pid, name):
    ssh(f"{POOL} kill {int(pid)} {name}", check=False, capture=True, timeout=60)


def session_file(name, f):
    """bytes of file F in the session's run dir, None when absent"""
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name) or not re.fullmatch(r"[A-Za-z0-9._-]+", f):
        return None
    r = subprocess.run(["ssh"] + OPTS + [HOST, f"cat {W}/root/runs/drive/{name}/{f}"], capture_output=True)
    return r.stdout if r.returncode == 0 else None


def cmd_status(a):
    ssh(f"{POOL} stat; systemctl list-units --no-legend 'orc-bunker-*'; du -sh {W} 2>/dev/null; free -m | head -2")


def cmd_teardown(a):
    ssh(f"for u in $(systemctl list-units --no-legend --plain 'orc-bunker-*' | cut -d' ' -f1); do systemctl stop $u; done; "
        f"rm -rf {W}; find /nix/var/nix/gcroots/auto -lname '{W}/*' -print -delete")
    subprocess.run(["ssh"] + OPTS + ["-O", "exit", HOST], capture_output=True)
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
    r = a.fn(a)
    sys.exit(r if isinstance(r, int) else 0)


if __name__ == "__main__":
    main()
