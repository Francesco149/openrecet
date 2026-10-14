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
  rhost.py portshots SCEN --at F,..      retail | port screenshots of the same frames, side by side
  rhost.py portrng SCEN --window F:T     port vs retail LCG steps with callers; first diverging draw

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
# wine target: a prefix whose drive_c/orc-rhost -> WINE_ROOT, so retail sees the SAME
# Windows paths (C:\orc-rhost\game, ...\runs\<batch>\<run>) as on the Windows host
WINE_ROOT = os.environ.get("RHOST_WINE_ROOT", "/mnt/ephemeral/openrecet/rhost-wine")
WINE_PREFIX = os.environ.get("RHOST_WINEPREFIX", "/mnt/ephemeral/openrecet/prefix")
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


def rhost_stale():
    """rhost.exe missing or older than any tools/rhost source"""
    exe = os.path.join(BUILD, "rhost.exe")
    if not os.path.exists(exe):
        return True
    t = os.path.getmtime(exe)
    src = os.path.join(ROOT, "src")   # shadow hooks link port code (build.sh)
    return any(os.path.getmtime(os.path.join(d, f)) > t for d in (HERE, src) for f in os.listdir(d)
               if f.endswith((".c", ".h", ".sh")))


def ensure_staged():
    exe = os.path.join(BUILD, "rhost.exe")
    stamp = os.path.join(BUILD, ".staged")
    if rhost_stale():
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


def wine_stage():
    os.makedirs(os.path.join(WINE_ROOT, "bin"), exist_ok=True)
    for f in ("rhost.exe", "v3proxy_d3d8.dll"):
        dst = os.path.join(WINE_ROOT, "bin", f)
        src = os.path.join(BUILD, f)
        if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src):
            shutil.copy2(src, dst)
    link = os.path.join(WINE_PREFIX, "drive_c", "orc-rhost")
    if not os.path.islink(link):
        os.symlink(WINE_ROOT, link)


def run_batch_wine(a, runs, tag):
    """same batch layout, executed locally under wine + Xvfb (parallel -j)"""
    import concurrent.futures
    if rhost_stale():
        cmd_build(None)
    wine_stage()
    global LOCAL_RUNS
    saved, LOCAL_RUNS = LOCAL_RUNS, os.path.join(WINE_ROOT, "runs")
    try:
        batch, bdir = make_batch(a, runs, tag)
    finally:
        LOCAL_RUNS = saved
    go = open(os.path.join(bdir, "go.ps1")).read()
    cmds = re.findall(r"^& (\S+rhost\.exe) (.*?) \| Select-String", go, re.M)
    env = dict(os.environ, WINEPREFIX=WINE_PREFIX, WINEDEBUG="-all")
    def one(i):
        exe, args = cmds[i]
        argv = [x.strip("'").replace("''", "'") for x in re.findall(r"'(?:[^']|'')*'", args)]
        r = subprocess.run(["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", "wine", exe] + argv,
                           capture_output=True, text=True, env=env, timeout=a.wine_timeout)
        return runs[i][0], r.stdout
    t0 = time.time()
    out_lines = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for name, out in ex.map(one, range(len(cmds))):
            out_lines.append(f"=== {name}")
            out_lines += [l for l in out.splitlines() if re.search(r"RESULT|FATAL|DIVERGE|hook-result", l)]
    out = "\n".join(out_lines)
    results, cur = {}, None
    for line in out_lines:
        if line.startswith("=== "):
            cur = line[4:].strip()
        elif cur and line.startswith("RESULT"):
            results[cur] = dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv)
        elif cur and line.strip():
            results.setdefault(cur + "#notes", []).append(line.strip())
    print(f"[wine batch {batch}: {len(runs)} run(s) in {time.time() - t0:.1f}s, -j{a.jobs} → {bdir}]")
    return batch, bdir, results, out


def run_batch(a, runs, tag, fetch=("rhost.log",)):
    if getattr(a, "target", "win") == "wine":
        return run_batch_wine(a, runs, tag)
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
    fetch = ("rhost.log", "coverage.txt", "shot_*.png") + (("state_*.rhs",) if a.dump_at else ())
    batch, bdir, res, out = run_batch(a, runs, "run", fetch=fetch)
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


# every RET hook that compares a port function against retail (rh_hooks.c registry)
VERIFY_HOOKS = ["haggle_decide", "haggle_offer_up", "haggle_offer_down", "rand", "floor_layout", "actor_spawn_reset"]


def cmd_verify(a):
    """re-verification dashboard: all scenarios × all shadow hooks → per-function evidence"""
    names = sorted(d for d in os.listdir(SCEN) if os.path.exists(os.path.join(SCEN, d, "trace.jsonl")))
    if a.only:
        names = [n for n in names if re.search(a.only, n)]
    a.hook = ",".join("shadow:" + h for h in VERIFY_HOOKS)
    a.flatten_out = False
    runs = resolve_runs(names)
    frames = {"day1-tutorial-to-day2": 40000}
    batch, bdir, res, out = run_batch(a, runs, "verify")
    tot = {}
    for n in names:
        for note in res.get(n + "#notes", []):
            m = re.match(r"hook-result shadow (\S+) calls=(\d+) diverge=(\d+) skipped=(\d+)(?: first: (.*))?", note)
            if m:
                t = tot.setdefault(m.group(1), [0, 0, 0, set(), ""])
                t[0] += int(m.group(2)); t[1] += int(m.group(3)); t[2] += int(m.group(4))
                if int(m.group(2)): t[3].add(n)
                if m.group(5) and not t[4]: t[4] = f"{n}: {m.group(5)}"
    done = sum(1 for n in names if res.get(n, {}).get("complete") == "1")
    print(f"\nVERIFY ({done}/{len(names)} scenarios complete)")
    print(f"{'function':22s} {'calls':>8s} {'diverge':>8s} {'skipped':>8s} scenarios")
    for h in VERIFY_HOOKS:
        c, d, k, sc, first = tot.get(h, [0, 0, 0, set(), ""])
        print(f"{h:22s} {c:8d} {d:8d} {k:8d} {len(sc)}" + (f"  FIRST: {first}" if d else ""))
    return 0 if all(tot.get(h, [0, 0])[1] == 0 for h in VERIFY_HOOKS) else 1


def run_port(a, batch, bdir, name, save, flat, frames, extra=(), fetch=()):
    """the port (--det-loads, turbo) on rhost's flat trace of the same run, on Windows.
    Returns the local port dir; `fetch` names files the port wrote into it."""
    pdir_l = os.path.join(bdir, "port")
    os.makedirs(pdir_l)
    shutil.copy(flat, os.path.join(pdir_l, "trace.jsonl"))
    if save:
        open(os.path.join(pdir_l, "seed.dat"), "wb").write(save_bytes(save))
    pw = f"{WROOT}\\runs\\{batch}\\port"
    args = ["--save-write-dir", pw, "--input-segtrace", pw + "\\trace.jsonl", "--anchor-trace-record", pw + "\\anchors.jsonl",
            "--max-frames", str(frames), "--turbo", "--rng-seed", "19937", "--hidden", "--det-loads", *extra]
    if not os.environ.get("PORT_AUDIO"):
        args.insert(args.index("--turbo") + 1, "--silent-audio")
    args = (["--save-override", pw + "\\seed.dat"] if save else ["--save-fresh"]) + args
    args = [x.replace("{pw}", pw) for x in args]
    ps = ["$ErrorActionPreference = 'Continue'",
          # the port reads game data from its cwd and writes call traces there: give it a cwd of links
          f"if (!(Test-Path {WROOT}\\portcwd\\bin)) {{ New-Item -ItemType Directory -Force {WROOT}\\portcwd | Out-Null; "
          f"Get-ChildItem {WROOT}\\game | % {{ if ($_.PSIsContainer) {{ cmd /c mklink /J \"{WROOT}\\portcwd\\$($_.Name)\" \"$($_.FullName)\" | Out-Null }} "
          f"else {{ cmd /c mklink /H \"{WROOT}\\portcwd\\$($_.Name)\" \"$($_.FullName)\" | Out-Null }} }} }}",
          f"Set-Location {WROOT}\\portcwd",
          "$p = Start-Process -FilePath " + WROOT + "\\port\\openrecet.exe -ArgumentList @(" + ",".join("'" + x + "'" for x in args) + ") -Wait -PassThru",
          "\"port exit=$($p.ExitCode)\"",
          f"Remove-Item {WROOT}\\portcwd\\openrecet-calltrace-*.jsonl -ErrorAction SilentlyContinue"]
    open(os.path.join(pdir_l, "go.ps1"), "w").write("\r\n".join(ps) + "\r\n")
    ssh_win(f"New-Item -ItemType Directory -Force {WROOT}\\port | Out-Null; exit 0")
    scp(os.path.join(ROOT, "build", "openrecet.exe"), f"{WIN}:C:/orc-rhost/port/openrecet.exe")
    scp(pdir_l, f"{WIN}:C:/orc-rhost/runs/{batch}/", recursive=True)
    print(win_job(batch + "-port", f"{pw}\\go.ps1").strip())
    for f in ("anchors.jsonl", *fetch):
        scp(f"{WIN}:C:/orc-rhost/runs/{batch}/port/{f}", os.path.join(pdir_l, f))
    return pdir_l


def cmd_portsave(a):
    """whole-flow port↔retail check: retail (rhost) vs port (--det-loads, flat trace) committed save.dat"""
    name = a.scenario
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = None
    batch, bdir, res, _ = run_batch(a, [(name, tr, None, save)], "portsave-retail", fetch=("rhost.log", "save.dat", "trace.flat.jsonl"))
    r = res.get(name, {})
    retail_save = os.path.join(bdir, name, "save.dat")
    if not os.path.exists(retail_save):
        print("retail run wrote no save.dat — scenario must commit a save"); return 2
    frames = int(r.get("frame", "0")) or 120000
    pdir_l = run_port(a, batch, bdir, name, save, os.path.join(bdir, name, "trace.flat.jsonl"), frames, fetch=("save.dat",))
    out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "save_compare.py"), os.path.join(pdir_l, "save.dat"), retail_save],
                         capture_output=True, text=True).stdout
    print(out.strip())
    return 0 if "IDENTICAL" in out else 1


def cmd_portanchors(a):
    """port vs retail anchor timeline on the same flat trace (port --det-loads): first divergence"""
    name = a.scenario
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = None
    a.extra = ((a.extra or "") + " --log-anchors").strip()
    batch, bdir, res, _ = run_batch(a, [(name, tr, None, save)], "portanchors-retail", fetch=("rhost.log", "trace.flat.jsonl"))
    frames = int(res.get(name, {}).get("frame", "0")) or 120000
    retail = [(m.group(1), int(m.group(2))) for m in re.finditer(r"^anchor (\S+) @(\d+)", open(os.path.join(bdir, name, "rhost.log")).read(), re.M)]
    pdir_l = run_port(a, batch, bdir, name, save, os.path.join(bdir, name, "trace.flat.jsonl"), frames)
    port = [(json.loads(l)["anchor"], int(json.loads(l)["frame"])) for l in open(os.path.join(pdir_l, "anchors.jsonl")) if l.strip()]
    skip = {"BOOT", "CONV_POSE_BLINK", "PAUSE_OPEN", "PAUSE_CLOSE",   # differently-defined / cosmetic on the two sides
            "FREEROAM_START"}                                         # port-only (no rhost predicate)
    # the two sides emit same-frame anchors in different orders: canonical order = (frame, name)
    R = sorted(x for x in retail if x[0] not in skip)
    P = sorted(x for x in port if x[0] not in skip)
    R = sorted(R, key=lambda x: (x[1], x[0])); P = sorted(P, key=lambda x: (x[1], x[0]))
    n = min(len(R), len(P))
    first_name = next((i for i in range(n) if R[i][0] != P[i][0]), None)
    offs = [P[i][1] - R[i][1] for i in range(first_name if first_name is not None else n)]
    from collections import Counter
    print(f"anchors: retail {len(R)} / port {len(P)}; same names for the first {first_name if first_name is not None else n}; "
          f"frame offset (port-retail) histogram: {dict(Counter(offs).most_common(6))}")
    i0 = next((i for i, o in enumerate(offs) if o != 0), None)
    if i0 is not None:
        print(f"first frame mismatch: #{i0} {R[i0][0]} retail f{R[i0][1]} / port f{P[i0][1]}")
    if first_name is not None:
        print(f"first NAME mismatch: #{first_name} retail {R[first_name]} / port {P[first_name]}")
    return 0 if i0 is None and first_name is None and len(R) == len(P) else 1


def cmd_portshots(a):
    """visual port↔retail check: retail (rhost --shot-at) and port (--capture-frames) screenshots of the
    same frames on the same flat trace, joined side by side (retail | port) in the batch dir"""
    name = a.scenario
    fs = [int(x) for x in a.at.split(",")]
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = None
    a.shot_at = ",".join(str(f) for f in fs)
    batch, bdir, res, _ = run_batch(a, [(name, tr, None, save)], "portshots-retail", fetch=("rhost.log", "trace.flat.jsonl", "shot_*.png"))
    run_port(a, batch, bdir, name, save, os.path.join(bdir, name, "trace.flat.jsonl"), max(fs) + 2,
             extra=("--capture-to", "{pw}", "--capture-frames", a.shot_at))
    pdir_l = os.path.join(bdir, "port")
    subprocess.run(["scp", "-q", f"{WIN}:C:/orc-rhost/runs/{batch}/port/frame_*.bmp", pdir_l], capture_output=True, text=True)
    out = []
    for f in fs:
        r = os.path.join(bdir, name, f"shot_f{f:06d}.png")
        p = os.path.join(pdir_l, f"frame_{f:05d}.bmp")
        dst = os.path.join(bdir, f"cmp_f{f:06d}.png")
        if not (os.path.exists(r) and os.path.exists(p)):
            print(f"f{f}: missing {'retail' if not os.path.exists(r) else 'port'} shot"); continue
        if shutil.which("magick"):
            sh(["magick", r, "-resize", "1024x768!", p, "-resize", "1024x768!", "+append", dst])
            out.append(dst)
    print("\n".join(out) if out else f"shots in {bdir}")
    return 0 if len(out) == len(fs) else 1


IDA_TSV = "/mnt/ephemeral/openrecet/decompiled-ida/functions_ida.tsv"


def retail_fn_of(addrs):
    """retail VA → 'FUN_xxxxxx+off' via the IDA function table (start, size)"""
    fns = []
    if os.path.exists(IDA_TSV):
        for l in open(IDA_TSV):
            f = l.split("\t")
            try:
                fns.append((int(f[0], 16), int(f[1])))
            except (ValueError, IndexError):
                pass
    fns.sort()
    import bisect
    starts = [f[0] for f in fns]
    out = {}
    for a in addrs:
        i = bisect.bisect_right(starts, a) - 1
        out[a] = f"FUN_{fns[i][0]:08x}+{a - fns[i][0]:x}" if i >= 0 and a < fns[i][0] + fns[i][1] else f"{a:08x}"
    return out


def port_fn_of(rvas):
    """port caller RVA → function (file:line) via addr2line on build/openrecet.exe (image base 0x400000)"""
    exe = os.path.join(ROOT, "build", "openrecet.exe")
    rvas = sorted(set(rvas))
    if not rvas or not shutil.which("i686-w64-mingw32-addr2line"):
        return {r: f"@{r:06x}" for r in rvas}
    r = subprocess.run(["i686-w64-mingw32-addr2line", "-f", "-s", "-e", exe] + [hex(0x400000 + v) for v in rvas],
                       capture_output=True, text=True).stdout.splitlines()
    return {v: f"{r[2 * i]} ({r[2 * i + 1]})" for i, v in enumerate(rvas) if 2 * i + 1 < len(r)}


def cmd_portrng(a):
    """RNG consumers, port vs retail on the same flat trace: every LCG step in a frame window with its caller
    (retail rhost --log-rand stack | port --rng-log); per-tick draw counts + the consumer runs of the first differing ticks"""
    name = a.scenario
    lo, hi = (int(x) for x in a.window.split(":"))
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = None
    a.extra = ((a.extra or "") + f" --log-rand {lo - 1}:{hi}").strip()
    batch, bdir, res, _ = run_batch(a, [(name, tr, None, save)], "portrng-retail", fetch=("rhost.log", "trace.flat.jsonl"))
    pdir_l = run_port(a, batch, bdir, name, save, os.path.join(bdir, name, "trace.flat.jsonl"), hi + 2,
                      extra=("--rng-log", "{pw}\\rng.log", f"{lo}:{hi}"), fetch=("rng.log",))
    # rhost labels tick N's draws fN (= its `post N`, = the port's label); a deferred loader's draws at
    # the boundary before tick N are logged "fN-1 … worker" — the port labels those N too
    R = [(int(m.group(1)) + (1 if m.group(5) else 0), "rand", m.group(2), m.group(3), [x.strip() for x in m.group(4).split(",") if x.strip()])
         for m in re.finditer(r"^rand f(\d+) seed ([0-9a-f]+) -> ([0-9a-f]+) \[(.*?)\]( worker)?\s*$", open(os.path.join(bdir, name, "rhost.log"), errors="replace").read(), re.M)]
    R = [r for r in R if lo <= r[0] <= hi]
    P = [(int(m.group(2)), m.group(1), m.group(3), m.group(4), int(m.group(5), 16))
         for m in re.finditer(r"^(rand|seed) f(\d+) ([0-9a-f]+) -> ([0-9a-f]+) @([0-9a-f]+)", open(os.path.join(pdir_l, "rng.log")).read(), re.M)]
    rmap = retail_fn_of({int(x, 16) for r in R for x in r[4][:4] if re.fullmatch(r"[0-9a-f]+", x)})
    pmap = port_fn_of({p[4] for p in P})
    from collections import Counter
    rc, pc = Counter(r[0] for r in R), Counter(p[0] for p in P if p[1] == "rand")
    print(f"window f{lo}..f{hi}: retail {len(R)} draws / port {sum(pc.values())} draws, {sum(1 for p in P if p[1] == 'seed')} port reseeds")
    # the LCG chain is the same whoever draws, so compare per tick: draw counts, then the consumers
    if R and P and R[0][2] != P[0][2]:
        print(f"  LCG state differs at the window start: retail {R[0][2]} / port {P[0][2]} (diverged earlier)")
    bad = [f for f in range(lo, hi + 1) if rc.get(f, 0) != pc.get(f, 0)]
    def runs(keys):
        out = []
        for k in keys:
            if out and out[-1][0] == k: out[-1][1] += 1
            else: out.append([k, 1])
        return out
    def rkey(r):   # consumer chain without offsets / the rand wrapper / CRT (stale stack slots)
        names = [rmap.get(int(x, 16), x).split("+")[0] for x in r[4][:4] if re.fullmatch(r"[0-9a-f]+", x)]
        names = [n for n in names if n.startswith("FUN_") and n not in ("FUN_00471084", "FUN_00471089")]
        return " < ".join(dict.fromkeys(names[:3]))
    for f in bad[:3]:
        print(f"f{f}: retail {rc.get(f, 0)} draws / port {pc.get(f, 0)} draws")
        for k, n in runs([rkey(r) for r in R if r[0] == f]):
            print(f"   retail {n:4d}x {k}")
        for k, n in runs([pmap.get(p[4], hex(p[4])).split(" (")[0] for p in P if p[0] == f and p[1] == "rand"]):
            print(f"   port   {n:4d}x {k}")
    if not bad:
        print("IDENTICAL per-tick draw counts in the window")
    else:
        print("frames with different draw counts (retail/port): " + ", ".join(f"f{f} {rc.get(f, 0)}/{pc.get(f, 0)}" for f in bad[:12]))
    return 0 if not bad else 1


def parse_post(path):
    """`post N va=val ..` lines (rhost --watch / port --state-va) → {frame: {va: int}}"""
    rows = {}
    for m in re.finditer(r"^post (\d+)((?: [0-9a-f]+=-?\d+)+)\s*$", open(path, errors="replace").read(), re.M):
        rows[int(m.group(1))] = {k: int(v) for k, v in (kv.split("=") for kv in m.group(2).split())}
    return rows


def cmd_portstate(a):
    """port vs retail, frame by frame: retail VAs (rhost --watch) vs their port mirrors (--state-va)"""
    name = a.scenario
    vas = [format(int(v, 16), "x") for v in a.va.split(",")]
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = None
    a.extra = ((a.extra or "") + f" --watch {','.join(vas)} --log-every 1").strip()
    batch, bdir, res, _ = run_batch(a, [(name, tr, None, save)], "portstate-retail", fetch=("rhost.log", "trace.flat.jsonl"))
    frames = int(res.get(name, {}).get("frame", "0")) or 120000
    pdir_l = run_port(a, batch, bdir, name, save, os.path.join(bdir, name, "trace.flat.jsonl"), frames,
                      extra=("--state-va", ",".join(vas), "--state-log", "{pw}\\state.log"), fetch=("state.log",))
    R = parse_post(os.path.join(bdir, name, "rhost.log"))
    P = parse_post(os.path.join(pdir_l, "state.log"))
    common_f = sorted(set(R) & set(P))
    print(f"frames: retail {len(R)} / port {len(P)} / both {len(common_f)}")
    rcw = sorted(set(re.findall(r"^frame \d+ .*? cw=([0-9a-f]{4})", open(os.path.join(bdir, name, "rhost.log"), errors="replace").read(), re.M)))
    pcw = re.findall(r"^cw (\d+) ([0-9a-f]{4})", open(os.path.join(pdir_l, "state.log")).read(), re.M)
    print(f"x87 CW: retail {','.join(rcw) or '?'} / port " + (", ".join(f"{c}@f{f}" for f, c in pcw) or "?"))
    firsts = []
    for va in vas:
        diff = [f for f in common_f if R[f].get(va) != P[f].get(va)]
        if diff:
            f = diff[0]; firsts.append(f)
            print(f"  {va:>8s}  first diff f{f}: retail {R[f].get(va)} / port {P[f].get(va)}  ({len(diff)} frames differ)")
        else:
            print(f"  {va:>8s}  IDENTICAL")
    if firsts:
        f0 = min(firsts)
        print(f"around f{f0} (retail | port):")
        for f in range(max(0, f0 - a.context), f0 + a.context + 1):
            if f in R and f in P:
                mark = " *" if any(R[f].get(v) != P[f].get(v) for v in vas) else ""
                print(f"  f{f:<6d} " + "  ".join(f"{v}={R[f].get(v)}|{P[f].get(v)}" for v in vas) + mark)
    return 1 if firsts else 0


def cmd_flatten(a):
    a.flatten = True
    a.only = "^(" + "|".join(re.escape(s) for s in a.scenarios) + ")$"
    cmd_corpus(a)


def cmd_diff(a):
    rhs.diff(a.a, a.b, a.show)


def cmd_record(a):
    """human play on the Windows desktop, realtime, real input; writes a flat trace"""
    if os.path.exists(os.path.join(SCEN, a.name, "trace.jsonl")) and not a.force:
        print(f"record: scenario {a.name} already exists — pick a new name (or --force to overwrite)"); return 2
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
    p.add_argument("--target", choices=("win", "wine"), default=os.environ.get("RHOST_TARGET", "win"))
    p.add_argument("-j", "--jobs", type=int, default=max(1, (os.cpu_count() or 4) // 2), help="wine: parallel runs")
    p.add_argument("--wine-timeout", type=int, default=3600)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("build").set_defaults(fn=cmd_build)
    sp.add_parser("stage").set_defaults(fn=cmd_stage)
    p = sp.add_parser("run"); p.add_argument("scenarios", nargs="*"); p.add_argument("--masks"); p.add_argument("--save"); common(p); p.set_defaults(fn=cmd_run)
    p = sp.add_parser("det"); p.add_argument("scenario"); p.add_argument("-n", type=int, default=2); p.add_argument("--calibrate", action="store_true"); common(p); p.set_defaults(fn=cmd_det)
    p = sp.add_parser("corpus"); p.add_argument("--only"); p.add_argument("--flatten", action="store_true"); common(p); p.set_defaults(fn=cmd_corpus)
    p = sp.add_parser("portsave"); p.add_argument("scenario"); common(p); p.set_defaults(fn=cmd_portsave)
    p = sp.add_parser("portanchors"); p.add_argument("scenario"); common(p); p.set_defaults(fn=cmd_portanchors)
    p = sp.add_parser("portstate"); p.add_argument("scenario"); p.add_argument("--va", required=True, help="hex VAs, e.g. 6023a0,438cc08")
    p.add_argument("--context", type=int, default=4); common(p); p.set_defaults(fn=cmd_portstate)
    p = sp.add_parser("portrng"); p.add_argument("scenario"); p.add_argument("--window", required=True, help="FROM:TO frames")
    p.add_argument("--context", type=int, default=3); common(p); p.set_defaults(fn=cmd_portrng)
    p = sp.add_parser("portshots"); p.add_argument("scenario"); p.add_argument("--at", required=True, help="frames F,F,..")
    common(p); p.set_defaults(fn=cmd_portshots)
    p = sp.add_parser("verify"); p.add_argument("--only"); common(p); p.set_defaults(fn=cmd_verify)
    p = sp.add_parser("flatten"); p.add_argument("scenarios", nargs="+"); common(p); p.set_defaults(fn=cmd_flatten)
    p = sp.add_parser("diff"); p.add_argument("a"); p.add_argument("b"); p.add_argument("--show", type=int, default=40); p.set_defaults(fn=cmd_diff)
    p = sp.add_parser("record"); p.add_argument("name"); p.add_argument("--save"); p.add_argument("--force", action="store_true", help="overwrite an existing scenario")
    common(p); p.set_defaults(fn=cmd_record)
    a = ap.parse_args()
    r = a.fn(a)
    sys.exit(r or 0)


if __name__ == "__main__":
    main()
