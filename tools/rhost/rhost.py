#!/usr/bin/env python3
"""rhost.py — driver for the in-process retail host (tools/rhost).

  rhost.py build                         build build/rhost/{rhost.exe,v3proxy_d3d8.dll}
  rhost.py stage                         upload binaries + canonical recet.ini to Windows
  rhost.py run SCEN [SCEN..] [opts]      replay tests/scenarios/<SCEN>/trace.jsonl
  rhost.py det SCEN [-n N] [--calibrate] run N times, dump at end, diff (determinism gate)
  rhost.py corpus [opts]                 every scenario; summary table
  rhost.py diff A.rhs B.rhs              classify the differences between two dumps
  rhost.py record NAME [--save SHA|PATH] HUMAN PLAY (Windows desktop, realtime) → flat trace
  rhost.py flatten SCEN                  write tests/scenarios/<SCEN>/trace.flat.jsonl

Common opts: --frames N --seed S --hook SPEC --dump-at F,..|end --hash-every N
             --coverage FILE --d3d --cap FROM:COUNT --stall-ms N --extra "raw rhost args"
Target: Windows native via `ssh win` + the interactive-session runner (see README).
Results: /mnt/ephemeral/openrecet/rhost-runs/<batch>/<run>/ (rhost.log + requested files).
"""
import argparse
import datetime
import gzip
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.join(ROOT, "tools", "rhost")
SCEN = os.path.join(ROOT, "tests", "scenarios")
BUILD = os.path.join(ROOT, "build", "rhost")
LOCAL_RUNS = os.environ.get("RHOST_RUNS", "/mnt/ephemeral/openrecet/rhost-runs")
WIN = os.environ.get("RHOST_WIN", "win")             # ssh alias of the Windows host (OpenSSH, port 2222)
WROOT = "C:\\orc-rhost"
VOLATILE = os.path.join(HERE, "volatile.txt")
sys.path.insert(0, HERE)
import rhs  # noqa: E402

SSH_NOISE = re.compile(r"post-quantum|pq\.html|may need to be upgraded|store now, decrypt later")


def sh(cmd, check=True, capture=False, timeout=None):
    r = subprocess.run(cmd, shell=isinstance(cmd, str), check=False, text=True,
                       capture_output=capture, timeout=timeout)
    if check and r.returncode != 0:
        raise SystemExit(f"command failed ({r.returncode}): {cmd}\n{r.stderr if capture else ''}")
    return r


def ssh_win(ps, timeout=3600):
    """run a PowerShell command in the sshd (session-0) context — no GUI here"""
    r = subprocess.run(["ssh", "-o", "BatchMode=yes", WIN, ps], text=True, capture_output=True, timeout=timeout)
    err = "\n".join(l for l in r.stderr.splitlines() if not SSH_NOISE.search(l))
    if r.returncode != 0:
        raise SystemExit(f"ssh win failed ({r.returncode}): {ps[:200]}\n{err}")
    return r.stdout


def scp(src, dst, recursive=False):
    cmd = ["scp", "-q"] + (["-r"] if recursive else []) + [src, dst]
    r = subprocess.run(cmd, text=True, capture_output=True)
    if r.returncode != 0:
        raise SystemExit(f"scp failed: {' '.join(cmd)}\n{r.stderr}")


def win_job(job_id, ps_path_win, timeout=7200, stream=True):
    """run C:\\...\\x.ps1 in the INTERACTIVE session through the queue runner"""
    q = WROOT + "\\queue"
    ps = (f"Copy-Item '{ps_path_win}' '{q}\\{job_id}.tmp'; Move-Item '{q}\\{job_id}.tmp' '{q}\\{job_id}.job.ps1'; "
          f"while (!(Test-Path '{q}\\{job_id}.done')) {{ Start-Sleep -Milliseconds 200 }}; "
          f"Get-Content '{q}\\{job_id}.out'; Remove-Item '{q}\\{job_id}.*'")
    return ssh_win(ps, timeout=timeout)


# ---------------------------------------------------------------- build/stage
def cmd_build(a):
    sh(["nix", "develop", "--command", "bash", os.path.join(HERE, "build.sh")])


def cmd_stage(a):
    ssh_win(f"New-Item -ItemType Directory -Force {WROOT}\\bin,{WROOT}\\runs,{WROOT}\\queue | Out-Null; "
            f"Get-Process rhost -ErrorAction SilentlyContinue | Stop-Process -Force; exit 0")
    for f in ("rhost.exe", "v3proxy_d3d8.dll"):
        scp(os.path.join(BUILD, f), f"{WIN}:C:/orc-rhost/bin/{f}")
    scp(os.path.join(HERE, "recet.ini"), f"{WIN}:C:/orc-rhost/recet.ini")
    for f in ("runner.ps1", "install-runner.ps1"):
        scp(os.path.join(HERE, "win", f), f"{WIN}:C:/orc-rhost/{f}")
    print("staged", BUILD, "->", WROOT)


def ensure_staged():
    exe = os.path.join(BUILD, "rhost.exe")
    stamp = os.path.join(BUILD, ".staged")
    if not os.path.exists(exe):
        cmd_build(None)
    if not os.path.exists(stamp) or os.path.getmtime(stamp) < os.path.getmtime(exe):
        cmd_stage(None)
        open(stamp, "w").close()


# ---------------------------------------------------------------- scenarios
def scenario_inputs(name):
    d = os.path.join(SCEN, name)
    tr = os.path.join(d, "trace.jsonl")
    if not os.path.exists(tr):
        raise SystemExit(f"no trace: {tr}")
    save = None
    for line in open(tr):
        line = line.strip()
        if line.startswith("{") and '"savefile"' in line:
            ref = json.loads(line)["savefile"]
            save = None if ref == "@fresh" else os.path.normpath(os.path.join(d, ref))   # @fresh = boot with no save
            break
    return tr, save


def save_bytes(path):
    if path.endswith(".gz"):
        return gzip.open(path, "rb").read()
    return open(path, "rb").read()


def rhost_args(a, run_win, inputs_win):
    args = ["--exe", f"{WROOT}\\game\\recettear.unpacked.exe", "--game", f"{WROOT}\\game", "--run", run_win]
    args += inputs_win
    if a.frames:
        args += ["--frames", str(a.frames)]
    if a.seed is not None:
        args += ["--seed", str(a.seed)]
    if a.hook:
        args += ["--hook", a.hook]
    if a.dump_at:
        args += ["--dump-at", a.dump_at]
    if a.hash_every:
        args += ["--hash-every", str(a.hash_every)]
    if a.coverage:
        args += ["--coverage", f"{run_win}\\coverage_in.txt"]
    if a.d3d:
        args += ["--d3d-proxy", f"{WROOT}\\bin\\v3proxy_d3d8.dll"]
        if a.cap:
            args += ["--cap", a.cap]
    if a.stall_ms:
        args += ["--stall-ms", str(a.stall_ms)]
    if getattr(a, "shot_at", None):
        args += ["--shot-at", a.shot_at]
    if getattr(a, "shot_every", None):
        args += ["--shot-every", str(a.shot_every)]
    if not getattr(a, "show", False):
        args += ["--quiet"]
    if a.extra:
        args += shlex.split(a.extra)
    return args


def make_batch(a, runs, tag):
    """runs: list of (run_name, trace_path|None, masks_path|None, save_path|None)"""
    batch = datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + tag
    bdir = os.path.join(LOCAL_RUNS, batch)
    os.makedirs(bdir)
    lines = ["$ErrorActionPreference = 'Continue'"]
    for name, trace, masks, save in runs:
        rdir = os.path.join(bdir, name)
        os.makedirs(rdir)
        shutil.copy(os.path.join(HERE, "recet.ini"), rdir)
        if os.path.exists(VOLATILE):
            shutil.copy(VOLATILE, rdir)
        if save:
            open(os.path.join(rdir, "save.dat"), "wb").write(save_bytes(save))
        run_win = f"{WROOT}\\runs\\{batch}\\{name}"
        inputs = []
        if trace:
            shutil.copy(trace, os.path.join(rdir, "trace.jsonl"))
            inputs = ["--trace", f"{run_win}\\trace.jsonl"]
        elif masks:
            shutil.copy(masks, os.path.join(rdir, "masks.txt"))
            inputs = ["--masks", f"{run_win}\\masks.txt"]
        if getattr(a, "flatten_out", None):
            inputs += ["--flatten", "trace.flat.jsonl"]
        if a.coverage:
            shutil.copy(a.coverage, os.path.join(rdir, "coverage_in.txt"))
        args = rhost_args(a, run_win, inputs)
        ps_args = " ".join("'" + x.replace("'", "''") + "'" for x in args)
        lines.append(f"Write-Output '=== {name}'")
        lines.append(f"& {WROOT}\\bin\\rhost.exe {ps_args} | Select-String -Pattern 'RESULT|FATAL|DIVERGE|hook-result' | ForEach-Object {{ $_.Line }}")
    open(os.path.join(bdir, "go.ps1"), "w").write("\r\n".join(lines) + "\r\n")
    return batch, bdir


def run_batch(a, runs, tag, fetch=("rhost.log",)):
    ensure_staged()
    batch, bdir = make_batch(a, runs, tag)
    scp(bdir, f"{WIN}:C:/orc-rhost/runs/", recursive=True)
    t0 = time.time()
    out = win_job(batch, f"{WROOT}\\runs\\{batch}\\go.ps1")
    results = {}
    cur = None
    for line in out.splitlines():
        if line.startswith("=== "):
            cur = line[4:].strip()
        elif cur and line.startswith("RESULT"):
            results[cur] = dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv)
        elif cur and line.strip():
            results.setdefault(cur + "#notes", []).append(line.strip())
    for name, *_ in runs:
        for f in fetch:
            dst = os.path.join(bdir, name) if "*" in f else os.path.join(bdir, name, f)
            subprocess.run(["scp", "-q", f"{WIN}:C:/orc-rhost/runs/{batch}/{name}/{f}", dst], capture_output=True, text=True)
    print(f"[batch {batch}: {len(runs)} run(s) in {time.time() - t0:.1f}s → {bdir}]")
    return batch, bdir, results, out


def resolve_runs(names):
    runs = []
    for n in names:
        tr, save = scenario_inputs(n)
        runs.append((n, tr, None, save))
    return runs


# ---------------------------------------------------------------- commands
def print_results(results, names):
    for n in names:
        r = results.get(n)
        if not r:
            print(f"{n:45s} NO RESULT  {' | '.join(results.get(n + '#notes', [])[:3])}")
            continue
        print(f"{n:45s} {r.get('status'):5s} f={r.get('frame'):>6s} rng={r.get('rng')} sem={r.get('state_sem')} "
              f"complete={r.get('complete', '?')} {r.get('wall_ms', '?')}ms")
        for note in results.get(n + "#notes", [])[:6]:
            print("    " + note)


def cmd_run(a):
    if a.masks:
        runs = [("masks", None, a.masks, a.save)]
    else:
        runs = resolve_runs(a.scenarios)
    a.flatten_out = False
    batch, bdir, res, out = run_batch(a, runs, "run", fetch=("rhost.log", "coverage.txt", "shot_*.png"))
    print_results(res, [r[0] for r in runs])


def cmd_det(a):
    tr, save = scenario_inputs(a.scenario)
    a.dump_at = (a.dump_at + ",end") if a.dump_at else "end"
    runs = [(f"r{i}", tr, None, save) for i in range(a.n)]
    batch, bdir, res, out = run_batch(a, runs, "det-" + a.scenario, fetch=("rhost.log", "state_end.rhs"))
    print_results(res, [r[0] for r in runs])
    base = os.path.join(bdir, "r0", "state_end.rhs")
    bad = 0
    learned = set()
    for i in range(1, a.n):
        other = os.path.join(bdir, f"r{i}", "state_end.rhs")
        print(f"--- r0 vs r{i}")
        rows = rhs.diff(base, other, show=20)
        real = [r for r in rows if r[3] not in ("ext-ptr(both)", "ext-ptr(stale)", "volatile-addr")]
        bad += len(real)
        learned |= {r[0] for r in rows if r[3] in ("ext-ptr(both)", "ext-ptr(stale)")}
    sems = {res.get(r[0], {}).get("state_sem") for r in runs}
    print(f"DETERMINISM {'PASS' if not bad else 'FAIL'}: {bad} non-pointer differing dwords; "
          f"semantic hashes: {len(sems)} distinct; external-pointer slots seen: {len(learned)}")
    if a.calibrate and learned:
        old = set()
        if os.path.exists(VOLATILE):
            old = {int(l.split()[0], 16) for l in open(VOLATILE) if l.strip() and not l.startswith("#")}
        new = sorted(old | learned)
        with open(VOLATILE, "w") as f:
            f.write("# rhost volatile addresses: slots that hold system-DLL/COM pointers (masked in state_sem).\n"
                    "# Learned by `rhost.py det SCEN --calibrate` (only diffs that are external pointers in BOTH runs).\n")
            for v in new:
                f.write(f"{v:08x}\n")
        print(f"calibrated {VOLATILE}: {len(old)} → {len(new)} addresses")
    return 0 if not bad else 1


def cmd_corpus(a):
    names = sorted(d for d in os.listdir(SCEN) if os.path.exists(os.path.join(SCEN, d, "trace.jsonl")))
    if a.only:
        names = [n for n in names if re.search(a.only, n)]
    runs = resolve_runs(names)
    a.flatten_out = a.flatten
    fetch = ("rhost.log",) + (("trace.flat.jsonl",) if a.flatten else ()) + (("coverage.txt",) if a.coverage else ())
    batch, bdir, res, out = run_batch(a, runs, "corpus", fetch=fetch)
    print_results(res, names)
    done = sum(1 for n in names if res.get(n, {}).get("complete") == "1")
    print(f"CORPUS: {done}/{len(names)} complete; results in {bdir}")
    with open(os.path.join(bdir, "summary.tsv"), "w") as f:
        f.write("scenario\tstatus\tframe\tcomplete\trng\tstate_sem\twall_ms\n")
        for n in names:
            r = res.get(n, {})
            f.write(f"{n}\t{r.get('status', '-')}\t{r.get('frame', '-')}\t{r.get('complete', '-')}\t{r.get('rng', '-')}\t{r.get('state_sem', '-')}\t{r.get('wall_ms', '-')}\n")
    if a.flatten:
        for n in names:
            src = os.path.join(bdir, n, "trace.flat.jsonl")
            if res.get(n, {}).get("complete") == "1" and os.path.exists(src):
                ref = next((l for l in open(os.path.join(SCEN, n, "trace.jsonl")) if '"savefile"' in l), "")
                open(os.path.join(SCEN, n, "trace.flat.jsonl"), "w").write(ref + open(src).read())
        print("flat traces written next to each completed scenario's trace.jsonl")


def cmd_flatten(a):
    a.flatten = True
    a.only = "^(" + "|".join(re.escape(s) for s in a.scenarios) + ")$"
    cmd_corpus(a)


def cmd_diff(a):
    rhs.diff(a.a, a.b, a.show)


def cmd_record(a):
    """human play on the Windows desktop, realtime, real input; writes a flat trace"""
    ensure_staged()
    save = None
    if a.save:
        save = a.save if os.path.exists(a.save) else os.path.join(SCEN, "_saves", a.save + ".sav.gz")
    a.frames = a.frames or 0
    a.flatten_out = False
    a.show = True
    a.dump_at = "end"
    a.extra = (a.extra or "") + " --record trace.flat.jsonl"
    batch, bdir = make_batch(a, [("rec", None, None, save)], "record-" + a.name)
    scp(bdir, f"{WIN}:C:/orc-rhost/runs/", recursive=True)
    print("recording: play in the game window on the Windows desktop; close the window to finish")
    out = win_job(batch, f"{WROOT}\\runs\\{batch}\\go.ps1", timeout=6 * 3600)
    print(out)
    dst = os.path.join(SCEN, a.name)
    os.makedirs(dst, exist_ok=True)
    scp(f"{WIN}:C:/orc-rhost/runs/{batch}/rec/trace.flat.jsonl", os.path.join(dst, "trace.jsonl"))
    scp(f"{WIN}:C:/orc-rhost/runs/{batch}/rec/state_end.rhs", os.path.join(bdir, "rec", "state_end.rhs"))
    if save:
        # keep the scenario's save reference
        body = open(os.path.join(dst, "trace.jsonl")).read()
        rel = os.path.relpath(save, dst)
        open(os.path.join(dst, "trace.jsonl"), "w").write(json.dumps({"savefile": rel}) + "\n" + body)
    print("wrote", os.path.join(dst, "trace.jsonl"))
    # self-check: replaying the recording must reproduce the recorded final state
    m = re.search(r"RESULT .*?frame=(\d+).*?state_sem=([0-9a-f]+)", out)
    if not m:
        print("record: no RESULT line — cannot self-check"); return 1
    a2 = argparse.Namespace(**{k: None for k in ("seed", "hook", "hash_every", "coverage", "cap", "stall_ms", "extra", "shot_at", "shot_every")})
    a2.d3d = False; a2.show = False; a2.frames = int(m.group(1)); a2.flatten_out = False; a2.dump_at = "end"
    _, rb, res, _ = run_batch(a2, [(a.name, os.path.join(dst, "trace.jsonl"), None, save)], "recheck", fetch=("rhost.log", "state_end.rhs"))
    rep = res.get(a.name, {})
    rows = rhs.diff(os.path.join(bdir, "rec", "state_end.rhs"), os.path.join(rb, a.name, "state_end.rhs"), show=20)
    real = [r for r in rows if r[3] not in ("ext-ptr(both)", "ext-ptr(stale)", "volatile-addr")]
    ok = not real and rep.get("frame") == m.group(1)
    print(f"record self-check: recorded f={m.group(1)} | replay f={rep.get('frame')} | non-pointer diffs={len(real)} → {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def common(p):
    p.add_argument("--frames", type=int)
    p.add_argument("--seed", type=int)
    p.add_argument("--hook")
    p.add_argument("--dump-at")
    p.add_argument("--hash-every", type=int)
    p.add_argument("--coverage")
    p.add_argument("--d3d", action="store_true")
    p.add_argument("--cap")
    p.add_argument("--stall-ms", type=int)
    p.add_argument("--extra")
    p.add_argument("--shot-at", help="F[,F..|end]: PNG of the game window at those frames")
    p.add_argument("--shot-every", type=int)
    p.add_argument("--show", action="store_true", help="normal on-screen window (default: --quiet off-screen)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("build").set_defaults(fn=cmd_build)
    sp.add_parser("stage").set_defaults(fn=cmd_stage)
    p = sp.add_parser("run"); p.add_argument("scenarios", nargs="*"); p.add_argument("--masks"); p.add_argument("--save"); common(p); p.set_defaults(fn=cmd_run)
    p = sp.add_parser("det"); p.add_argument("scenario"); p.add_argument("-n", type=int, default=2); p.add_argument("--calibrate", action="store_true"); common(p); p.set_defaults(fn=cmd_det)
    p = sp.add_parser("corpus"); p.add_argument("--only"); p.add_argument("--flatten", action="store_true"); common(p); p.set_defaults(fn=cmd_corpus)
    p = sp.add_parser("flatten"); p.add_argument("scenarios", nargs="+"); common(p); p.set_defaults(fn=cmd_flatten)
    p = sp.add_parser("diff"); p.add_argument("a"); p.add_argument("b"); p.add_argument("--show", type=int, default=40); p.set_defaults(fn=cmd_diff)
    p = sp.add_parser("record"); p.add_argument("name"); p.add_argument("--save"); common(p); p.set_defaults(fn=cmd_record)
    a = ap.parse_args()
    r = a.fn(a)
    sys.exit(r or 0)


if __name__ == "__main__":
    main()
