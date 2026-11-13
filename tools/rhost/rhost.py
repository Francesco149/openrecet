#!/usr/bin/env python3
"""rhost.py — driver for the in-process retail host (tools/rhost).

  rhost.py build                         build build/rhost/{rhost.exe,v3proxy_d3d8.dll}
  rhost.py stage                         upload binaries + canonical recet.ini to Windows
  rhost.py run SCEN [SCEN..] [opts]      replay tests/scenarios/<SCEN>/trace.jsonl
  rhost.py det SCEN [-n N] [--calibrate] run N times, dump at end, diff (determinism gate)
  rhost.py corpus [opts]                 every scenario; summary table
  rhost.py diff A.rhs B.rhs              classify the differences between two dumps
  rhost.py tables SCEN --at F,F          retail's live effect / object / actor type histograms
  rhost.py endsave SCEN [--refresh]      replay SCEN, store the save.dat it commits in _saves → its path
                                         (cached by the trace's sha256 in _saves/endsaves.json)
  rhost.py record NAME [--save S|--from SCEN]  HUMAN PLAY (Windows desktop, realtime) → flat trace;
                                         --save SHA[prefix]|PATH|SCEN (the save SCEN starts from),
                                         --from SCEN = the save SCEN ends with (cached, else replayed);
                                         the self-check replay caches NAME's own end save
  rhost.py flatten SCEN                  write tests/scenarios/<SCEN>/trace.flat.jsonl
  rhost.py portshots SCEN --at F,..      retail | port screenshots of the same frames, side by side
  rhost.py portrng SCEN --window F:T     port vs retail LCG steps with callers; first diverging draw
  rhost.py portblock SCEN --at F         port vs retail, every dword of the layout blocks + work bank at frame F
  rhost.py gates [--only RE] [-j N]      the regression gate: portsave / portanchors / portrng, one line each (N rows at once)
  rhost.py golden [SCEN..|--only RE]     cache retail's side of the port commands (rand / anchors / flat / save), once
  rhost.py triage [SCEN..|--only RE]     every scenario: the port's first RNG split vs its golden, clustered by signature
  rhost.py restart-runner [--slots N]    restart the Windows queue runner (N parallel jobs)
  rhost.py session-install [--save S]    free-play capture (FPC-4): freeze this rhost build for the desktop
                                         launcher (C:/orc-rhost/play/play.cmd + a Desktop shortcut)
  rhost.py session-status                the capture pieces on Windows: cut, frames, self-check
  rhost.py session-sync                  pull finished pieces → tests/playthrough/<session>/<NNN>/ (scenario
                                         name pt-<session>-<NNN>; saves → _saves, end saves cached)

Common opts: --frames N --seed S --hook SPEC --dump-at F,..|end --hash-every N
             --coverage FILE --d3d --cap FROM:COUNT --stall-ms N --extra "raw rhost args"
Target: Windows native via `ssh win` + the interactive-session runner (see README).
Results: /mnt/ephemeral/openrecet/rhost-runs/<batch>/<run>/ (rhost.log + requested files).
"""
import argparse
import glob
import datetime
import gzip
import json
import os
import re
import shlex
import shutil
import struct
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
# --headless runs get their OWN prefix: they start wine without an X display, and the first process of a
# wineserver decides the desktop's driver for everyone — a display-less desktop made every Xvfb run in the
# same prefix fail at CreateWindowExA (err 1400) for as long as one headless session was alive.
WINE_PREFIX_HL = os.environ.get("RHOST_WINEPREFIX_HL", "/mnt/ephemeral/openrecet/prefix-headless")


def wine_prefix(headless):
    """the prefix for this kind of run; the headless one is made on first use"""
    if not headless:
        return WINE_PREFIX
    if not os.path.exists(os.path.join(WINE_PREFIX_HL, "system.reg")):
        print(f"[creating the headless wine prefix {WINE_PREFIX_HL} (once)]")
        env = {k: v for k, v in dict(os.environ, WINEPREFIX=WINE_PREFIX_HL, WINEDEBUG="-all", WINEDLLOVERRIDES="mscoree,mshtml=").items()
               if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
        subprocess.run(["wineboot", "-i"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
        subprocess.run(["wineserver", "-w"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    link = os.path.join(WINE_PREFIX_HL, "drive_c", "orc-rhost")
    if not os.path.islink(link):
        os.symlink(WINE_ROOT, link)
    return WINE_PREFIX_HL
WROOT = "C:\\orc-rhost"
VOLATILE = os.path.join(HERE, "volatile.txt")
sys.path.insert(0, HERE)
sys.path.insert(1, os.path.join(ROOT, "tools"))
import rhs  # noqa: E402
import trace_save  # noqa: E402

SSH_NOISE = re.compile(r"post-quantum|pq\.html|may need to be upgraded|store now, decrypt later")


def sh(cmd, check=True, capture=False, timeout=None):
    r = subprocess.run(cmd, shell=isinstance(cmd, str), check=False, text=True,
                       capture_output=capture, timeout=timeout)
    if check and r.returncode != 0:
        raise SystemExit(f"command failed ({r.returncode}): {cmd}\n{r.stderr if capture else ''}")
    return r


# a dropped / refused connection (parallel jobs open many; Windows sshd throttles startups) is retried
SSH_TRANSIENT = re.compile(r"(?i)connection (reset|closed|refused|timed out)|kex_exchange|banner exchange|broken pipe")


def ssh_win(ps, timeout=3600):
    """run a PowerShell command in the sshd (session-0) context — no GUI here"""
    for attempt in range(4):
        r = subprocess.run(["ssh", "-o", "BatchMode=yes", WIN, ps], text=True, capture_output=True, timeout=timeout)
        err = "\n".join(l for l in r.stderr.splitlines() if not SSH_NOISE.search(l))
        if r.returncode == 255 and SSH_TRANSIENT.search(err) and attempt < 3:
            time.sleep(2 + 3 * attempt)
            continue
        break
    if r.returncode != 0:
        raise SystemExit(f"ssh win failed ({r.returncode}): {ps[:200]}\n{err}")
    return r.stdout


def scp(src, dst, recursive=False):
    cmd = ["scp", "-q"] + (["-r"] if recursive else []) + [src, dst]
    for attempt in range(4):
        r = subprocess.run(cmd, text=True, capture_output=True)
        if r.returncode != 0 and SSH_TRANSIENT.search(r.stderr) and attempt < 3:
            time.sleep(2 + 3 * attempt)
            continue
        break
    if r.returncode != 0:
        raise SystemExit(f"scp failed: {' '.join(cmd)}\n{r.stderr}")


def file_sha12(*paths):
    import hashlib
    h = hashlib.sha256()
    for p in paths:
        h.update(open(p, "rb").read())
    return h.hexdigest()[:12]


# Binaries live content-addressed on the run host (bin\<sha>\, port\<sha>\): a stage never
# overwrites or kills a running image, so batches (and a free-play session) run side by side.
def bin_dir():
    """WROOT-relative Windows dir of the current rhost build (bin\\<sha12>); a gates child takes its parent's"""
    if os.environ.get("RHOST_BIN_DIR"):
        return os.environ["RHOST_BIN_DIR"]
    return f"{WROOT}\\bin\\" + file_sha12(os.path.join(BUILD, "rhost.exe"), os.path.join(BUILD, "v3proxy_d3d8.dll"))


# Every rhost / port process a job starts waits here first: at most queue\procs.txt (default 12) of OUR
# processes (C:\orc-rhost\...) at once, and only with >= 1.5 GB RAM free (16 GB box, sibling projects too).
# The mutex makes check + launch atomic across the runner's parallel jobs and `rd`'s live sessions (tools/drive/drive.py
# win_spawn: started from sshd's session 0, hence Global\, not Local\).
PS_ADMIT = (
    "function Wait-Admit { $cap = 12; try { $cap = [int](Get-Content 'C:\\orc-rhost\\queue\\procs.txt' -TotalCount 1 "
    "-ErrorAction Stop) } catch {}; while ($true) { $n = @(Get-Process rhost,openrecet -ErrorAction SilentlyContinue | "
    "Where-Object { $_.Path -like 'C:\\orc-rhost\\*' }).Count; $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory; "
    "if ($n -eq 0 -or ($n -lt $cap -and $free -gt 1500000)) { return }; Start-Sleep -Milliseconds 500 } }\r\n"
    "$admit = New-Object System.Threading.Mutex($false, 'Global\\orc-rhost-admit')\r\n"
    "function Start-Admitted([string]$exe, [string[]]$argv, [string]$so, [string]$se) { "
    "try { [void]$admit.WaitOne() } catch [System.Threading.AbandonedMutexException] {}; "
    "try { Wait-Admit; $p = Start-Process -FilePath $exe -ArgumentList $argv -NoNewWindow -PassThru "
    "-RedirectStandardOutput $so -RedirectStandardError $se; $null = $p.Handle; Start-Sleep -Milliseconds 300; return $p } "
    "finally { $admit.ReleaseMutex() } }")


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
    exe = os.path.join(BUILD, "rhost.exe")       # a no-op rebuild (another process built it meanwhile) must not stay "stale"
    if os.path.exists(exe):
        os.utime(exe, None)


def cmd_stage(a):
    # content-addressed: bin\<sha12>\ is written once and never replaced, so nothing running is killed
    # or overwritten (the old in-place bin\rhost.exe upload had to kill in-flight batches first)
    bd = bin_dir()
    present = ssh_win(f"New-Item -ItemType Directory -Force {WROOT}\\runs,{WROOT}\\queue | Out-Null; "
                      f"if ((Test-Path '{bd}\\v3proxy_d3d8.dll') -and (Test-Path '{bd}\\rhost.exe')) {{ 'present' }}; exit 0").strip()
    if present != "present":
        ssh_win(f"New-Item -ItemType Directory -Force '{bd}.up' | Out-Null; exit 0")
        for f in ("rhost.exe", "v3proxy_d3d8.dll"):
            scp(os.path.join(BUILD, f), f"{WIN}:{bd.replace(chr(92), '/')}.up/{f}")
        # rename into place: a concurrent stage of the same build sees all or nothing
        ssh_win(f"if (!(Test-Path '{bd}')) {{ Rename-Item '{bd}.up' (Split-Path '{bd}' -Leaf) }} "
                f"else {{ Copy-Item '{bd}.up\\*' '{bd}' -Force -ErrorAction SilentlyContinue; Remove-Item '{bd}.up' -Recurse -Force }}; exit 0")
    scp(os.path.join(HERE, "recet.ini"), f"{WIN}:C:/orc-rhost/recet.ini")
    for f in ("runner.ps1", "install-runner.ps1"):
        scp(os.path.join(HERE, "win", f), f"{WIN}:C:/orc-rhost/{f}")
    print("staged", BUILD, "->", bd)


def cmd_restart_runner(a):
    """restart the Windows queue runner (scheduled task) so a staged runner.ps1 takes effect;
    refuses while jobs run (their waiting ssh sides would get a 'job lost')"""
    ensure_staged()
    live = ssh_win(f"@(Get-ChildItem {WROOT}\\queue -Filter '*.running.ps1' -ErrorAction SilentlyContinue).Count").strip()
    if live not in ("", "0") and not a.force:
        print(f"restart-runner: {live} job(s) running — wait, or --force"); return 1
    if a.slots:
        ssh_win(f"Set-Content {WROOT}\\queue\\slots.txt {a.slots}; exit 0")
    print(ssh_win("Stop-ScheduledTask -TaskName orc-rhost-runner; Start-Sleep 1; Start-ScheduledTask -TaskName orc-rhost-runner; "
                  f"Start-Sleep 2; Get-Content {WROOT}\\queue\\runner.pid; "
                  f"if (Test-Path {WROOT}\\queue\\slots.txt) {{ 'slots ' + (Get-Content {WROOT}\\queue\\slots.txt) }}").strip())
    return 0


# ---- admission on maximus: every local rhost process (a wine batch run, an `rd` live session, a search worker)
# starts through admit_local(). 2026-11-11: seven API drivers, each with `rd search -j 4`, next to a wine batch, two
# 200k-frame replays and a port build ran the 31 GB box out of memory (the kernel killed the desktop's processes).
# A process is ~0.65 GB; the cap bounds the total, the floor keeps room for whatever else the box runs.
LOCAL_MAX_PROCS = int(os.environ.get("RHOST_MAX_PROCS", "10"))
LOCAL_MEM_FLOOR_GB = float(os.environ.get("RHOST_MEM_FLOOR_GB", "8"))


def local_rhost_procs():
    n = 0
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:                    # the game process itself (~0.75 GB), not wine's start.exe / xvfb-run wrappers —
                c = open(f"/proc/{d}/comm").read().strip()      # nor yet a wine loader still starting one (comm "wine*"
                n += c == "rhost.exe" or (c.startswith("wine") and c != "wineserver" and   # for a second or two: parallel
                                          any(x.endswith(b"rhost.exe") for x in open(f"/proc/{d}/cmdline", "rb").read().split(b"\0")))
            except OSError:         # admissions all saw 0 of them, 2026-11-11)
                continue
    return n


def mem_avail_gb():
    for l in open("/proc/meminfo"):
        if l.startswith("MemAvailable:"):
            return int(l.split()[1]) / 1048576
    return 0.0


class admit_local:
    """`with admit_local(wait): Popen(..)` — holds a machine-wide lock while the caller starts ONE rhost process, after
    waiting (up to WAIT seconds) for a free slot and for MemAvailable above the floor; SystemExit when neither comes"""
    def __init__(self, wait=600, what="rhost"):
        self.wait, self.what = wait, what

    def __enter__(self):
        import fcntl
        os.makedirs(WINE_ROOT, exist_ok=True)
        self.f = open(os.path.join(WINE_ROOT, "admit.lock"), "w")
        t0 = time.time()
        while True:
            fcntl.flock(self.f, fcntl.LOCK_EX)
            n, gb = local_rhost_procs(), mem_avail_gb()
            if n < LOCAL_MAX_PROCS and gb >= LOCAL_MEM_FLOOR_GB:
                return self
            fcntl.flock(self.f, fcntl.LOCK_UN)
            if time.time() - t0 >= self.wait:
                self.f.close()
                raise SystemExit(f"{self.what}: not admitted — {n} rhost processes on this box (cap {LOCAL_MAX_PROCS}), {gb:.1f} GB available "
                                 f"(floor {LOCAL_MEM_FLOOR_GB:g}). Stop idle sessions (`./rd ls`, `./rd stop R`) or wait; "
                                 f"RHOST_MAX_PROCS / RHOST_MEM_FLOOR_GB change the limits (do not raise them to get past this).")
            time.sleep(2)

    def __exit__(self, *exc):
        time.sleep(0.3)             # the new process is in /proc before the next admission counts
        self.f.close()
        return False


def rhost_stale():
    """rhost.exe missing or older than any tools/rhost source"""
    exe = os.path.join(BUILD, "rhost.exe")
    if not os.path.exists(exe):
        return True
    t = os.path.getmtime(exe)
    src = os.path.join(ROOT, "src")   # shadow hooks link port code: the .c files build.sh names + any header
    if any(os.path.getmtime(os.path.join(HERE, f)) > t for f in os.listdir(HERE) if f.endswith((".c", ".h", ".sh")) or f == "gen_dev.py"):
        return True
    return any(os.path.getmtime(os.path.join(src, f)) > t for f in os.listdir(src)
               if f.endswith(".h") or f in PORT_LINKED)


# the port sources rhost.exe links (tools/rhost/build.sh): an edit elsewhere in src/ does not make rhost stale —
# with several agents at work, a port fix in progress otherwise rebuilt rhost under every rd / rhost.py call
PORT_LINKED = {f + ".c" for f in ("customer_haggle", "rng", "floor_layout", "actor_spawn", "audio_mci", "call_trace", "customer_seller",
                                  "map_collision", "math3d", "actor_table", "enemy_place", "party", "input_segtrace")}



PREBUILT = os.environ.get("RHOST_PREBUILT") == "1"   # a parent (gates -j) built + staged everything once


def ensure_staged():
    if PREBUILT:
        return
    stamp = os.path.join(BUILD, ".staged")
    if rhost_stale():
        cmd_build(None)
    bd = bin_dir()
    if not os.path.exists(stamp) or open(stamp).read().strip() != bd:
        cmd_stage(None)
        open(stamp, "w").write(bd)


# ---------------------------------------------------------------- scenarios
# free-play capture pieces (session-sync): tests/playthrough/<session>/<NNN>/trace.jsonl, named pt-<session>-<NNN>
PLAYTHROUGH = os.path.join(ROOT, "tests", "playthrough")
PT_RE = re.compile(r"^pt-(\d{8}-\d{6})-(\d{3})$")


def scen_dir(name):
    m = PT_RE.match(name)
    return os.path.join(PLAYTHROUGH, m.group(1), m.group(2)) if m else os.path.join(SCEN, name)


def scenario_names(pieces=False):
    """tests/scenarios/* with a trace (+ the playthrough pieces)"""
    names = sorted(d for d in os.listdir(SCEN) if os.path.exists(os.path.join(SCEN, d, "trace.jsonl")))
    if pieces:
        names += sorted(f"pt-{os.path.basename(os.path.dirname(t))}-{os.path.basename(t)}"
                        for t in (os.path.dirname(x) for x in glob.glob(os.path.join(PLAYTHROUGH, "*", "*", "trace.jsonl"))))
    return names


def scenario_inputs(name):
    d = scen_dir(name)
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


def save_arg(s):
    """--save S: a path, a _saves sha (a unique prefix will do), or a scenario name (the save it starts from)"""
    if os.path.exists(s):
        return s
    if os.path.exists(os.path.join(scen_dir(s), "trace.jsonl")):
        save = scenario_inputs(s)[1]
        if not save:
            raise SystemExit(f"--save {s}: that scenario starts fresh (@fresh)")
        return save
    store = os.path.join(SCEN, "_saves")
    hits = sorted(f for f in os.listdir(store) if f.startswith(s.lower()) and f.endswith(".sav.gz")) if os.path.isdir(store) else []
    if len(hits) != 1:
        raise SystemExit(f"--save {s}: not a path, scenario or _saves sha" + (f" ({len(hits)} shas match)" if hits else ""))
    return os.path.join(store, hits[0])


def replay_args(**over):
    """options for a plain retail replay: no hooks / dumps / shots, off-screen, Windows (= RNG truth)"""
    a = argparse.Namespace(**{k: None for k in ("frames", "seed", "hook", "dump_at", "hash_every", "coverage", "cap",
                                                "stall_ms", "extra", "shot_at", "shot_every")})
    a.d3d = a.show = a.flatten_out = False
    a.target = "win"
    vars(a).update(over)
    return a


# end-save cache: the sha256 of a trace file (its savefile line included, so the start save too) → the save
# its retail replay ends with. Replays are deterministic, so a hit skips the replay. Local like the blobs.
ENDSAVES = os.path.join(SCEN, "_saves", "endsaves.json")


def endsave_cache(trace, put=None):
    """the cached end-save blob of TRACE (None on a miss); put=(sha, frame, scenario) stores one"""
    key = trace_save.sha256_file(trace)
    try:
        db = json.load(open(ENDSAVES))
    except (OSError, ValueError):
        db = {}
    if put:
        db[key] = {"save": put[0], "frame": put[1], "scenario": put[2]}
        os.makedirs(os.path.dirname(ENDSAVES), exist_ok=True)
        json.dump(db, open(ENDSAVES + ".tmp", "w"), indent=1, sort_keys=True)
        os.replace(ENDSAVES + ".tmp", ENDSAVES)
        return None
    e = db.get(key)
    blob = e and os.path.join(SCEN, "_saves", e["save"] + trace_save.SAVE_SUFFIX)
    return (blob, e) if blob and os.path.exists(blob) else None


def store_end_save(name, trace, start, raw, frame):
    """RAW = the save.dat a replay of TRACE left: stored in _saves + the cache → its blob, None if it commits none"""
    if not os.path.exists(raw) or (start and open(raw, "rb").read() == save_bytes(start)):
        return None
    sha, blob = trace_save.store_save(raw, os.path.join(SCEN, "_saves"))
    endsave_cache(trace, put=(sha, frame, name))
    return str(blob)


def end_save(name, refresh=False):
    """the save.dat scenario NAME ends with: replayed in rhost (or cached), stored content-addressed in _saves → its path"""
    tr, start = scenario_inputs(name)
    hit = None if refresh else endsave_cache(tr)
    if hit:
        print(f"[{name} ends with save {hit[1]['save']} (f{hit[1]['frame']}, cached: {os.path.relpath(ENDSAVES, ROOT)})]")
        return hit[0]
    print(f"[replaying {name} for the save it ends with]")
    _, bdir, res, out = run_batch(replay_args(), [(name, tr, None, start)], "endsave-" + name, fetch=("rhost.log", "save.dat"))
    r = res.get(name, {})
    if r.get("complete") != "1":
        raise SystemExit(f"replaying {name} did not complete: {r or out[-2000:]}")
    blob = store_end_save(name, tr, start, os.path.join(bdir, name, "save.dat"), r.get("frame"))
    if not blob:
        raise SystemExit(f"{name} commits no save: nothing to start from")
    print(f"[{name} ends with save {os.path.basename(blob).split('.')[0]} (f{r.get('frame')})]")
    return blob


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
        args += ["--d3d-proxy", f"{bin_dir()}\\v3proxy_d3d8.dll"]
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
    if getattr(a, "headless", False):
        args += ["--headless"]
    if a.extra:
        args += shlex.split(a.extra)
    return args


def make_batch(a, runs, tag):
    """runs: list of (run_name, trace_path|None, masks_path|None, save_path|None)"""
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    for n in range(1000):   # concurrent commands in the same second get -2, -3, ..
        batch = f"{stamp}-{tag}" if n == 0 else f"{stamp}-{n + 1}-{tag}"
        bdir = os.path.join(LOCAL_RUNS, batch)
        try:
            os.makedirs(bdir)
            break
        except FileExistsError:
            continue
    lines = ["$ErrorActionPreference = 'Continue'"]
    argv = []
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
        json.dump(args, open(os.path.join(rdir, "argv.json"), "w"))
        argv.append((name, run_win, args))
    # the batch's runs in parallel (at most -j of them; Start-Admitted caps the whole box), then their
    # result lines in run order
    lines += [PS_ADMIT, f"$J = {max(1, getattr(a, 'jobs', 1) or 1)}", "$procs = @()"]
    exe = f"{bin_dir()}\\rhost.exe"
    for name, run_win, args in argv:
        # Start-Process joins -ArgumentList with spaces: quote the elements that hold one
        ps_args = ",".join("'" + (f'"{x}"' if " " in x else x).replace("'", "''") + "'" for x in args)
        lines.append("while (@($procs | Where-Object { !$_.HasExited }).Count -ge $J) { Start-Sleep -Milliseconds 100 }")
        lines.append(f"$procs += ,(Start-Admitted '{exe}' @({ps_args}) '{run_win}\\stdout.txt' '{run_win}\\stderr.txt')")
    lines.append("$procs | ForEach-Object { $_.WaitForExit() }")
    for name, run_win, _ in argv:
        lines.append(f"Write-Output '=== {name}'")
        # rh_log copies every log line to stdout: once read, drop the copy (rhost.log keeps it)
        lines.append(f"if (Test-Path '{run_win}\\stdout.txt') {{ Select-String -Path '{run_win}\\stdout.txt' "
                     f"-Pattern 'RESULT|FATAL|DIVERGE|hook-result' | ForEach-Object {{ $_.Line }}; "
                     f"if (Test-Path '{run_win}\\rhost.log') {{ Remove-Item '{run_win}\\stdout.txt' }} }}")
        lines.append(f"if (Test-Path '{run_win}\\stderr.txt') {{ Get-Content '{run_win}\\stderr.txt' -TotalCount 5 }}")
    open(os.path.join(bdir, "go.ps1"), "w").write("\r\n".join(lines) + "\r\n")
    return batch, bdir


def wine_stage():
    d = os.path.join(WINE_ROOT, "bin", bin_dir().rsplit("\\", 1)[1])   # content-addressed like Windows
    os.makedirs(d, exist_ok=True)
    for f in ("rhost.exe", "v3proxy_d3d8.dll"):
        dst = os.path.join(d, f)
        if not os.path.exists(dst):
            shutil.copy2(os.path.join(BUILD, f), dst + ".new")
            os.replace(dst + ".new", dst)
    link = os.path.join(WINE_PREFIX, "drive_c", "orc-rhost")
    if not os.path.islink(link):
        os.symlink(WINE_ROOT, link)


def run_batch_wine(a, runs, tag):
    """same batch layout, executed locally under wine + Xvfb (parallel -j)"""
    import concurrent.futures
    if rhost_stale() and not PREBUILT:
        cmd_build(None)
    wine_stage()
    global LOCAL_RUNS
    saved, LOCAL_RUNS = LOCAL_RUNS, os.path.join(WINE_ROOT, "runs")
    try:
        batch, bdir = make_batch(a, runs, tag)
    finally:
        LOCAL_RUNS = saved
    exe = f"{bin_dir()}\\rhost.exe"
    env = dict(os.environ, WINEPREFIX=wine_prefix(getattr(a, "headless", False)), WINEDEBUG="-all")
    def one(i):
        argv = json.load(open(os.path.join(bdir, runs[i][0], "argv.json")))
        log = os.path.join(bdir, runs[i][0], "rhost.log")
        so_path = os.path.join(bdir, runs[i][0], "rhost.stdout")
        so = open(so_path, "w")
        with admit_local(wait=4 * 3600, what=f"wine run {runs[i][0]}"):
            if getattr(a, "headless", False):   # fake devices: no X server at all (wine's null driver)
                henv = {k: v for k, v in env.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
                p = subprocess.Popen(["wine", exe] + argv, stdout=so, stderr=subprocess.DEVNULL, text=True, env=henv,
                                     start_new_session=True)
            else:
                p = subprocess.Popen(["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", "wine", exe] + argv,
                                     stdout=so, stderr=subprocess.DEVNULL, text=True, env=env,
                                     start_new_session=True)
        # a finished run can hang in wine's process teardown: the RESULT line in its log is
        # the end — give the exit 20 s, then kill the session (xvfb-run, wine, rhost)
        t0, done_at = time.time(), None
        while p.poll() is None:
            time.sleep(2)
            if done_at is None and os.path.exists(log) and "\nRESULT " in open(log, errors="replace").read()[-4096:]:
                done_at = time.time()
            if (done_at and time.time() - done_at > 20) or time.time() - t0 > a.wine_timeout:
                os.killpg(p.pid, 9)
                break
        so.close()
        out = open(so_path, errors="replace").read()
        if "RESULT" not in out and os.path.exists(log):   # killed before printing: take it from the log
            out += "\n" + "\n".join(l for l in open(log, errors="replace").read().splitlines() if l.startswith("RESULT"))
        return runs[i][0], out
    t0 = time.time()
    out_lines = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for name, out in ex.map(one, range(len(runs))):
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


PRUNE_HOURS = float(os.environ.get("RHOST_PRUNE_HOURS", "12"))
_pruned = False


def prune_win_runs(hours=None):
    """delete the Windows run dirs older than `hours` (C:\\orc-rhost\\runs: each batch leaves
    its save.dat copy, captures and state dumps there — 200 GB in four days); the
    `record` runs (the human's raw recordings) are kept. Every result worth keeping
    was fetched to the local batch dir (rhost-runs/), and the rest replays deterministically."""
    h = PRUNE_HOURS if hours is None else hours
    keep = []   # the current builds' dirs, whatever their age
    for f in (bin_dir, port_dir):
        try:
            keep.append("'" + f().rsplit("\\", 1)[1] + "'")
        except OSError:
            pass
    keep = ",".join(keep) or "''"
    # + the content-addressed binaries unused for 3 days (an image still running is locked: skipped)
    out = ssh_win(
        f"$n = 0; $cut = (Get-Date).AddHours(-{h}); "
        f"Get-ChildItem {WROOT}\\runs -Directory | Where-Object {{ $_.Name -notmatch '-record-' -and $_.Name -ne 'drive' "
        f"-and $_.LastWriteTime -lt $cut }} | ForEach-Object {{ Remove-Item $_.FullName -Recurse "
        f"-Force -ErrorAction SilentlyContinue; $n++ }}; \"pruned $n\"; "
        # rd's live-session dirs (runs\drive\*: a session removes its own; a crashed one's stays) — never a running one's
        f"$busy = @(Get-CimInstance Win32_Process -Filter \"Name='rhost.exe'\" | ForEach-Object {{ $_.CommandLine }}) -join '|'; "
        f"Get-ChildItem {WROOT}\\runs\\drive -Directory -ErrorAction SilentlyContinue | Where-Object {{ $_.LastWriteTime -lt $cut -and "
        f"-not $busy.Contains($_.FullName) }} | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue; "
        f"$old = (Get-Date).AddDays(-3); Get-ChildItem {WROOT}\\bin,{WROOT}\\port -Directory -ErrorAction SilentlyContinue | "
        f"Where-Object {{ $_.Name -match '^[0-9a-f]{{12}}$' -and $_.Name -notin @({keep}) -and $_.LastWriteTime -lt $old }} | "
        f"ForEach-Object {{ Remove-Item $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }}", timeout=1800)
    return out.strip()


ENV_VA = 0x9643078   # DAT_09643038[16], a DirectMusic segment pointer (engine quirk §163)


def run_batch(a, runs, tag, fetch=("rhost.log",)):
    global _pruned
    if tag.endswith("-retail") and f"{ENV_VA:x}" not in (a.extra or ""):
        # a port comparison: log the run's heap-dependent dword the port must reproduce
        # (in rhost.log's `frame N` lines; run_port reads it)
        a.extra = ((a.extra or "") + f" --watch {ENV_VA:x}").strip()
    if getattr(a, "target", "win") == "wine":
        return run_batch_wine(a, runs, tag)
    if getattr(a, "target", "win") == "bunker":
        return bunker().run_batch(a, runs, tag, fetch)
    if not _pruned and PRUNE_HOURS > 0 and not PREBUILT:   # once per command: keep C: from filling up
        _pruned = True
        prune_win_runs()
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
    import concurrent.futures

    def get(job):                  # absent files are fine: the caller checks
        name, f = job
        dst = os.path.join(bdir, name) if "*" in f else os.path.join(bdir, name, f)
        for attempt in range(4):   # a dropped connection is retried (snap-check -j 6, 2026-11-11: one rhost.log lost)
            r = subprocess.run(["scp", "-q", f"{WIN}:C:/orc-rhost/runs/{batch}/{name}/{f}", dst], capture_output=True, text=True)
            if r.returncode == 0 or not SSH_TRANSIENT.search(r.stderr) or attempt == 3:
                break
            time.sleep(2 + 3 * attempt)
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:   # Windows sshd throttles startups past ~10
        list(ex.map(get, [(name, f) for name, *_ in runs for f in fetch]))
    print(f"[batch {batch}: {len(runs)} run(s) in {time.time() - t0:.1f}s → {bdir}]")
    return batch, bdir, results, out


def bunker():
    """tools/bunker/bunker.py: --target bunker (headless under wine on the temporary NixOS box, one bounded pool)"""
    sys.path.insert(0, os.path.join(ROOT, "tools", "bunker"))
    import bunker as bk
    return bk


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
        runs = [("masks", None, a.masks, a.save and save_arg(a.save))]
    else:
        runs = resolve_runs(a.scenarios)
    a.flatten_out = False
    # save.dat: snap-check --target win compares it with the golden's (under wine the run dir is local anyway)
    fetch = ("rhost.log", "coverage.txt", "shot_*.png", "watchwrite.log", "save.dat") + (("state_*.rhs",) if a.dump_at else ())
    batch, bdir, res, out = run_batch(a, runs, "run", fetch=fetch)
    if a.d3d:
        # the proxy's capture (v3cap.bin + census) stayed on Windows: fetch it next to the run
        for name, *_ in runs:
            if getattr(a, "target", "win") == "win":    # under wine the run dir is already local (bunker: no --d3d)
                subprocess.run(["scp", "-q", "-r", f"{WIN}:C:/orc-rhost/runs/{batch}/{name}/v3", os.path.join(bdir, name)],
                               capture_output=True, text=True)
            cap = os.path.join(bdir, name, "v3", "v3cap.bin")
            print(f"d3d capture: {cap}" if os.path.exists(cap) else f"d3d capture: none for {name}")
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
VERIFY_HOOKS = ["haggle_decide", "haggle_offer_up", "haggle_offer_down", "rand", "floor_layout", "actor_spawn_reset",
                "seller_select", "mapcol_build", "mapcol_ground", "mapcol_ray", "enemy_place",
                "enemy_place_boss"]


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


_port_built = False


def build_port():
    """make the port exe once per invocation: a port run staged whatever build/openrecet.exe
    last held, so an edit without a manual make compared a stale port (once, so a long batch
    never picks up edits made while it runs)"""
    global _port_built
    if not _port_built and not PREBUILT:
        sh(["nix", "develop", "--command", "make", "-s", "-C", os.path.join(ROOT, "src"), "-j16"])
        _port_built = True


def run_port(a, batch, bdir, name, save, flat, frames, extra=(), fetch=(), files=()):
    """the port (--det-loads, turbo) on rhost's flat trace of the same run, on Windows.
    Returns the local port dir; `fetch` names files the port wrote into it; `files` (local paths)
    go into it first (the port sees them as {pw}\\<basename>)."""
    if getattr(a, "target", "win") == "bunker":
        raise SystemExit("--target bunker is retail-only (headless batches): the port builds here and runs on win / wine")
    build_port()
    pdir_l = os.path.join(bdir, "port")
    os.makedirs(pdir_l)
    shutil.copy(flat, os.path.join(pdir_l, "trace.jsonl"))
    for f in files:
        shutil.copy(f, os.path.join(pdir_l, os.path.basename(f)))
    if save:
        open(os.path.join(pdir_l, "seed.dat"), "wb").write(save_bytes(save))
    pw = f"{WROOT}\\runs\\{batch}\\port"
    args = ["--save-write-dir", pw, "--input-segtrace", pw + "\\trace.jsonl", "--anchor-trace-record", pw + "\\anchors.jsonl",
            "--max-frames", str(frames), "--turbo", "--rng-seed", "19937", "--hidden", "--det-loads", *extra]
    if not os.environ.get("PORT_AUDIO"):
        args.insert(args.index("--turbo") + 1, "--silent-audio")
    args = (["--save-override", pw + "\\seed.dat"] if save else ["--save-fresh"]) + args
    log = os.path.join(bdir, name, "rhost.log")
    m = re.search(rf"^frame (\d{{3,}}) .* {ENV_VA:x}=(-?\d+)", open(log, errors="replace").read(), re.M) \
        if os.path.exists(log) else None
    if m:   # the segment pointer's low byte in this retail run (loaded at boot; read at f100+)
        args = ["--retail-dmusic-ptr", f"{int(m.group(2)) & 0xff:02x}"] + args
    # experiments (e.g. a forced divergence to test triage): appended last, so a repeated flag wins
    args += shlex.split(os.environ.get("RHOST_PORT_EXTRA", ""))
    args = [x.replace("{pw}", pw) for x in args]
    ps = ["$ErrorActionPreference = 'Continue'",
          # the port reads game data from its cwd and writes call traces there: give it a cwd of links
          f"if (!(Test-Path {WROOT}\\portcwd\\bin)) {{ New-Item -ItemType Directory -Force {WROOT}\\portcwd | Out-Null; "
          f"Get-ChildItem {WROOT}\\game | % {{ if ($_.PSIsContainer) {{ cmd /c mklink /J \"{WROOT}\\portcwd\\$($_.Name)\" \"$($_.FullName)\" | Out-Null }} "
          f"else {{ cmd /c mklink /H \"{WROOT}\\portcwd\\$($_.Name)\" \"$($_.FullName)\" | Out-Null }} }} }}",
          f"Set-Location {WROOT}\\portcwd",
          # content-addressed exe (port\<sha>\, recet.ini beside it) + no singleton: port runs side by side
          PS_ADMIT, "$env:OPENRECET_NO_SINGLETON = '1'",
          f"$p = Start-Admitted '{port_dir()}\\openrecet.exe' @(" + ",".join("'" + (f'"{x}"' if " " in x else x) + "'" for x in args) + ")"
          f" '{pw}\\port.out' '{pw}\\port.err'",
          "$p.WaitForExit()",
          f"if (Test-Path '{pw}\\port.out') {{ Get-Content '{pw}\\port.out' }}",
          "\"port exit=$($p.ExitCode)\"",
          f"Remove-Item {WROOT}\\portcwd\\openrecet-calltrace-*.jsonl -ErrorAction SilentlyContinue"]
    open(os.path.join(pdir_l, "go.ps1"), "w").write("\r\n".join(ps) + "\r\n")
    if getattr(a, "target", "win") == "wine":
        run_port_wine(a, pdir_l, args)
    else:
        run_port_win(batch, pdir_l, pw, fetch)
    # the port's stderr: surface its failure lines (a failed load / thread shows up here, not in the state)
    err = os.path.join(pdir_l, "port.err")
    bad = [l.rstrip() for l in open(err, errors="replace")
           if re.search(r"(?i)fail|error|overflow|cannot|missing", l)] if os.path.exists(err) else []
    for l in bad[:8]:
        print("port stderr: " + l[:200])
    if len(bad) > 8:
        print(f"port stderr: … {len(bad) - 8} more failure lines in {err}")
    return pdir_l


def run_port_wine(a, pdir_l, args):
    """the port under wine + Xvfb on this host: the batch dir already sits under the wine root
    (C:\\orc-rhost → WINE_ROOT), so the same Windows paths resolve; its cwd is a dir of links
    to the game data, like portcwd on Windows"""
    cwd = os.path.join(WINE_ROOT, "portcwd")
    if not os.path.isdir(os.path.join(cwd, "bin")):
        os.makedirs(cwd, exist_ok=True)
        for n in os.listdir(os.path.join(WINE_ROOT, "game")):
            if not os.path.lexists(os.path.join(cwd, n)):
                os.symlink(os.path.join(WINE_ROOT, "game", n), os.path.join(cwd, n))
    # a copy of the exe per batch: concurrent local runs never overwrite a running image
    batch = os.path.basename(os.path.dirname(pdir_l))
    edir = os.path.join(WINE_ROOT, "port", batch)
    os.makedirs(edir, exist_ok=True)
    shutil.copy2(os.path.join(ROOT, "build", "openrecet.exe"), os.path.join(edir, "openrecet.exe"))
    shutil.copy2(os.path.join(HERE, "recet.ini"), os.path.join(edir, "recet.ini"))
    # concurrent local runs share one wineserver: the port's singleton mutex would refuse the second
    env = dict(os.environ, WINEPREFIX=WINE_PREFIX, WINEDEBUG="-all", OPENRECET_NO_SINGLETON="1")
    with open(os.path.join(pdir_l, "port.err"), "w") as err:
        r = subprocess.run(["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", "wine",
                            f"{WROOT}\\port\\{batch}\\openrecet.exe", *args],
                           cwd=cwd, env=env, stdout=subprocess.DEVNULL, stderr=err, timeout=a.wine_timeout)
    print(f"port exit={r.returncode}")
    shutil.rmtree(edir, ignore_errors=True)


def port_dir():
    """Windows dir of the current port build + recet.ini (port\\<sha12>); a gates child takes its parent's"""
    if os.environ.get("RHOST_PORT_DIR"):
        return os.environ["RHOST_PORT_DIR"]
    return f"{WROOT}\\port\\" + file_sha12(os.path.join(ROOT, "build", "openrecet.exe"), os.path.join(HERE, "recet.ini"))


def stage_port():
    """upload build/openrecet.exe + recet.ini to port\\<sha12>\\ once (never over a running image).
    The port reads recet.ini next to its exe: retail's (rhost's canonical) one — without it the port
    ran on its defaults (screen 0 = 640x480) and portshots compared an upscaled frame."""
    pd = port_dir()
    if ssh_win(f"if (Test-Path '{pd}\\recet.ini') {{ 'present' }}; exit 0").strip() == "present":
        return
    ssh_win(f"New-Item -ItemType Directory -Force '{pd}.up' | Out-Null; exit 0")
    scp(os.path.join(ROOT, "build", "openrecet.exe"), f"{WIN}:{pd.replace(chr(92), '/')}.up/openrecet.exe")
    scp(os.path.join(HERE, "recet.ini"), f"{WIN}:{pd.replace(chr(92), '/')}.up/recet.ini")
    ssh_win(f"if (!(Test-Path '{pd}')) {{ Rename-Item '{pd}.up' (Split-Path '{pd}' -Leaf) }} "
            f"else {{ Copy-Item '{pd}.up\\*' '{pd}' -Force -ErrorAction SilentlyContinue; Remove-Item '{pd}.up' -Recurse -Force }}; exit 0")


def run_port_win(batch, pdir_l, pw, fetch):
    if not PREBUILT:
        stage_port()
    # a golden stands in for the retail run: then runs\<batch> exists only here
    ssh_win(f"New-Item -ItemType Directory -Force {WROOT}\\runs\\{batch} | Out-Null; exit 0")
    scp(pdir_l, f"{WIN}:C:/orc-rhost/runs/{batch}/", recursive=True)
    print(win_job(batch + "-port", f"{pw}\\go.ps1").strip())
    for f in ("anchors.jsonl", "port.err", *fetch):    # absent = not written (no save, no split …): the caller checks
        subprocess.run(["scp", "-q", f"{WIN}:C:/orc-rhost/runs/{batch}/port/{f}", os.path.join(pdir_l, f)],
                       capture_output=True, text=True)


# ---------------------------------------------------------------- retail goldens (docs/plans/free-play-capture.md FPC-2)
# Retail under rhost is a pure function of (trace, start save), so the retail side of a port comparison is run
# once per (target, trace, save) with every logger the port commands read, and cached. Keyed on behaviour, never
# on state hashes: those carry rhost-image pointers (0x964d330 differs across rhost builds, 2026-11-04).
# GOLDEN_VERSION: bump when rhost's deterministic core changes what retail does for a trace (virtual time,
# DEFER, the seed, the input path) — then every golden re-runs.
GOLDENS = os.environ.get("RHOST_GOLDENS", "/mnt/ephemeral/openrecet/goldens")
GOLDEN_VERSION = 1
GOLDEN_FILES = ("rhost.log", "trace.flat.jsonl", "save.dat")
RAND_RE = re.compile(r"^rand f(\d+) seed ([0-9a-f]+) -> ([0-9a-f]+) \[(.*?)\]( worker)?\s*$", re.M)


def golden_dir(a, name, tr, save):
    key = trace_save.sha256_file(tr)[:16] + "-" + (trace_save.sha256_file(save)[:8] if save else "fresh")
    return os.path.join(GOLDENS, getattr(a, "target", "win"), f"v{GOLDEN_VERSION}", f"{name}-{key}")


def golden_usable(a):
    """a plain replay: nothing the caller asked for changes or extends what retail runs / logs"""
    return not (a.extra or a.hook or a.frames or a.dump_at or a.coverage or a.d3d or getattr(a, "shot_at", None)
                or getattr(a, "no_golden", False))


def rand_rows(log):
    """retail --log-rand lines → (tick, seed, after, [stack]) — a deferred worker's draws count in the next tick,
    as the port labels them"""
    return [(int(m.group(1)) + (1 if m.group(5) else 0), m.group(2), m.group(3),
             [x.strip() for x in m.group(4).split(",") if x.strip()])
            for m in RAND_RE.finditer(open(log, errors="replace").read())]


def rng_golden_text(rows, frames):
    """the port's --rng-golden file (src/rng_golden.h): per tick the draw count + the LCG state after its last draw"""
    cnt, end = {}, {}
    for f, _, after, _ in rows:
        cnt[f] = cnt.get(f, 0) + 1
        end[f] = after
    out = ["# retail per-tick LCG draws (rhost --log-rand); src/rng_golden.h", f"frames {frames}"]
    out += [f"{f} {cnt[f]} {end[f]}" for f in sorted(cnt) if f <= frames]
    return "\n".join(out) + "\n"


GOLDEN_EXTRA = "--log-rand 0:100000000 --log-anchors"


def golden_store(a, name, tr, save, g, rd, r):
    """the retail run in RD (its RESULT r) → golden dir G: logs gzipped, the port's rng.golden, meta.json"""
    tmp = g + f".tmp{os.getpid()}"
    os.makedirs(tmp, exist_ok=True)
    for f in GOLDEN_FILES:
        if os.path.exists(os.path.join(rd, f)):
            with open(os.path.join(rd, f), "rb") as i, gzip.open(os.path.join(tmp, f + ".gz"), "wb", compresslevel=6) as o:
                shutil.copyfileobj(i, o)
    frames = int(r.get("frame", "0") or 0)
    rows = rand_rows(os.path.join(rd, "rhost.log"))
    with gzip.open(os.path.join(tmp, "rng.golden.gz"), "wt", compresslevel=6) as o:
        o.write(rng_golden_text(rows, frames))
    json.dump({"scenario": name, "result": r, "draws": len(rows), "rhost_bin": bin_dir(),
               "target": getattr(a, "target", "win"), "trace_sha256": trace_save.sha256_file(tr),
               "start_save": os.path.basename(save) if save else None,
               "created": datetime.datetime.now().isoformat(timespec="seconds")},
              open(os.path.join(tmp, "meta.json"), "w"), indent=1)
    if os.path.exists(g):          # a concurrent maker won: theirs is the same run
        shutil.rmtree(tmp)
    else:
        os.makedirs(os.path.dirname(g), exist_ok=True)
        os.replace(tmp, g)
    return frames, len(rows)


def golden_runs(a, todo, tag="golden-retail"):
    """retail for every (name, trace, save, golden dir) in one batch (-j at a time on the run host) → stored"""
    import copy
    b = copy.copy(a)
    b.flatten_out, b.dump_at, b.hook, b.frames, b.extra = True, None, None, None, GOLDEN_EXTRA
    _, bd, res, out = run_batch(b, [(n, tr, None, save) for n, tr, save, _ in todo], tag, fetch=GOLDEN_FILES)
    made = {}
    for n, tr, save, g in todo:
        r = res.get(n)
        if r:
            made[n] = (r,) + golden_store(a, n, tr, save, g, os.path.join(bd, n), r)
    shutil.rmtree(bd, ignore_errors=True)
    return made, out


def new_batch_dir(tag, base):
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    for n in range(1000):
        batch = f"{stamp}-{tag}" if n == 0 else f"{stamp}-{n + 1}-{tag}"
        try:
            os.makedirs(os.path.join(base, batch))
            return batch, os.path.join(base, batch)
        except FileExistsError:
            continue
    raise SystemExit("no batch dir")


def golden_get(a, name, tr, save, tag):
    """the golden as a local batch dir shaped like a fresh retail run (<bdir>/<name>/{rhost.log, trace.flat.jsonl,
    save.dat}) → (batch, bdir, {name: RESULT}); made on a miss"""
    g = golden_dir(a, name, tr, save)
    if not os.path.exists(os.path.join(g, "meta.json")):
        print(f"[retail golden {name}: none yet — running retail once]")
        made, out = golden_runs(a, [(name, tr, save, g)])
        if name not in made:
            raise SystemExit(f"golden {name}: retail gave no RESULT\n{out[-1500:]}")
    m = json.load(open(os.path.join(g, "meta.json")))
    base = os.path.join(WINE_ROOT, "runs") if getattr(a, "target", "win") == "wine" else LOCAL_RUNS
    batch, bdir = new_batch_dir(tag, base)
    rd = os.path.join(bdir, name)
    os.makedirs(rd)
    for f in GOLDEN_FILES:
        if os.path.exists(os.path.join(g, f + ".gz")):
            with gzip.open(os.path.join(g, f + ".gz"), "rb") as i, open(os.path.join(rd, f), "wb") as o:
                shutil.copyfileobj(i, o)
    if os.path.exists(os.path.join(g, "rng.golden.gz")):
        with gzip.open(os.path.join(g, "rng.golden.gz"), "rb") as i, open(os.path.join(rd, "rng.golden"), "wb") as o:
            shutil.copyfileobj(i, o)
    else:                          # goldens made before it was gzipped (2026-11-04)
        shutil.copy(os.path.join(g, "rng.golden"), os.path.join(rd, "rng.golden"))
    print(f"[retail golden {os.path.relpath(g, GOLDENS)}: f{m['result'].get('frame')}]")
    return batch, bdir, {name: m["result"]}


def retail_side(a, name, tr, save, tag, extra, fetch):
    """the retail run of a port comparison → (batch, bdir, results): the cached golden for a plain replay, else a
    fresh run with `extra` (the command's own loggers) on top of the caller's options"""
    if golden_usable(a):
        return golden_get(a, name, tr, save, tag)
    a.flatten_out, a.dump_at = True, None
    a.extra = ((a.extra or "") + " " + extra).strip()
    b, bd, res, _ = run_batch(a, [(name, tr, None, save)], tag, fetch=fetch)
    return b, bd, res


def cmd_golden(a):
    """make (or --refresh) the retail goldens of SCEN.. / --only RE / every scenario + playthrough piece, in one
    parallel batch"""
    names = a.scenarios or scenario_names(pieces=True)
    if a.only:
        names = [n for n in names if re.search(a.only, n)]
    todo = []
    for n in names:
        tr, save = scenario_inputs(n)
        g = golden_dir(a, n, tr, save)
        if a.refresh and os.path.exists(g):
            shutil.rmtree(g)
        if not os.path.exists(os.path.join(g, "meta.json")):
            todo.append((n, tr, save, g))
    print(f"goldens: {len(names) - len(todo)} cached, {len(todo)} to make")
    if not todo:
        return 0
    t0 = time.time()
    made, _ = golden_runs(a, todo)
    for n, *_ in todo:
        if n not in made:
            print(f"{n:40s} NO RESULT"); continue
        r, frames, draws = made[n]
        print(f"{n:40s} f={frames:>7d} draws={draws:>8d} {r.get('wall_ms')}ms complete={r.get('complete', '?')}")
    print(f"goldens: {len(made)}/{len(todo)} made in {time.time() - t0:.0f}s")
    return 0 if len(made) == len(todo) else 1


# ---------------------------------------------------------------- headless-check
# The gate of rh_dev.c: retail on the fake devices must play out exactly as on the real ones. Per scenario a
# headless replay (the golden's own loggers) is compared with the Windows real-device golden: end frame + RNG,
# every tick's draw count + LCG state, every anchor's frame, the resolved input (flat trace), the save.dat bytes.
ANCHOR_RE = re.compile(r"^anchor (\S+) @(\d+)", re.M)


def headless_compare(g, rd, r):
    """golden dir G vs the headless run in RD (RESULT r) → [] or the list of differences"""
    m = json.load(open(os.path.join(g, "meta.json")))["result"]
    bad = []
    for k in ("frame", "rng", "complete", "status", "scene", "threads"):
        if m.get(k) != r.get(k):
            bad.append(f"{k} {m.get(k)}→{r.get(k)}")
    gz = lambda f: gzip.open(os.path.join(g, f + ".gz"), "rt", errors="replace").read()
    want = gz("rng.golden") if os.path.exists(os.path.join(g, "rng.golden.gz")) else open(os.path.join(g, "rng.golden")).read()
    got = rng_golden_text(rand_rows(os.path.join(rd, "rhost.log")), int(r.get("frame", "0") or 0))
    if want != got:
        wl, gl = want.splitlines(), got.splitlines()
        i = next((i for i, (x, y) in enumerate(zip(wl, gl)) if x != y), min(len(wl), len(gl)))
        bad.append(f"rng tick: golden '{wl[i] if i < len(wl) else '-'}' headless '{gl[i] if i < len(gl) else '-'}'")
    wa = ANCHOR_RE.findall(gz("rhost.log"))
    ga = ANCHOR_RE.findall(open(os.path.join(rd, "rhost.log"), errors="replace").read())
    if wa != ga:
        i = next((i for i, (x, y) in enumerate(zip(wa, ga)) if x != y), min(len(wa), len(ga)))
        bad.append(f"anchor #{i}: golden {wa[i] if i < len(wa) else '-'} headless {ga[i] if i < len(ga) else '-'}")
    fl = os.path.join(rd, "trace.flat.jsonl")
    ops = lambda t: [l for l in t.splitlines() if not l.startswith("#")]   # the header comment names the run dir
    if os.path.exists(os.path.join(g, "trace.flat.jsonl.gz")) and (not os.path.exists(fl) or ops(gz("trace.flat.jsonl")) != ops(open(fl).read())):
        bad.append("flat trace differs")
    gs, hs = os.path.join(g, "save.dat.gz"), os.path.join(rd, "save.dat")
    if os.path.exists(gs) != os.path.exists(hs):
        bad.append(f"save.dat: golden {'has' if os.path.exists(gs) else 'none'}, headless {'has' if os.path.exists(hs) else 'none'}")
    elif os.path.exists(gs) and gzip.open(gs, "rb").read() != open(hs, "rb").read():
        bad.append("save.dat bytes differ")
    return bad, len(wa)


def cmd_headless_check(a):
    """SCEN.. / --only RE / every scenario with a Windows real-device golden: a headless replay vs that golden"""
    import copy
    names = a.scenarios or scenario_names(pieces=True)
    if a.only:
        names = [n for n in names if re.search(a.only, n)]
    ref = copy.copy(a); ref.target = "win"
    todo = []
    for n in names:
        tr, save = scenario_inputs(n)
        g = golden_dir(ref, n, tr, save)
        if os.path.exists(os.path.join(g, "meta.json")):
            todo.append((n, tr, save, g))
    print(f"headless-check: {len(todo)} scenario(s) with a win golden (of {len(names)}), target {a.target}, "
          f"{'REAL devices (a regression check of rhost itself)' if a.real else 'headless'}")
    b = copy.copy(a)
    b.headless, b.flatten_out, b.dump_at, b.hook, b.frames, b.extra = not a.real, True, None, None, None, GOLDEN_EXTRA
    t0 = time.time()
    _, bd, res, out = run_batch(b, [(n, tr, None, save) for n, tr, save, _ in todo], "hlcheck", fetch=GOLDEN_FILES)
    ok = frames = 0
    for n, tr, save, g in todo:
        r = res.get(n)
        if not r:
            print(f"{n:42s} NO RESULT  {' | '.join(res.get(n + '#notes', [])[:2])}"); continue
        bad, na = headless_compare(g, os.path.join(bd, n), r)
        frames += int(r.get("frame", "0") or 0)
        ok += not bad
        print(f"{n:42s} f={r.get('frame') or '?':>7s} anchors={na:<5d} {r.get('wall_ms') or '?':>7s}ms  " + ("IDENTICAL" if not bad else "DIFF " + "; ".join(bad)))
    print(f"headless-check: {ok}/{len(todo)} IDENTICAL to the real-device goldens ({frames} frames, {time.time() - t0:.0f}s) → {bd}")
    return 0 if ok == len(todo) else 1


def cmd_skip_check(a):
    """the gate of `rhost --skip-fn`: per scenario two headless replays, plain and with the functions skipped —
    the same end frame, RNG, game-state hash (state_sem: every byte of retail's .data / .bss / heap bar the
    volatile slots) and save.dat, or the function is not pure. Device memory (state_dev) may differ: draws and
    device state setters are what a skipped render function consists of."""
    import copy
    names = a.scenarios or scenario_names(pieces=True)
    if a.only:
        names = [n for n in names if re.search(a.only, n)]
    runs = []
    for n in names:
        tr, save = scenario_inputs(n)
        runs.append((n, tr, None, save))
    print(f"skip-check {a.fns}: {len(runs)} scenario(s), target {a.target}" + (f", masked {a.mask}" if a.mask else ""))
    t0 = time.time()
    res = {}
    mask = f"--mask-range {a.mask}" if a.mask else ""
    for tag, extra in (("plain", mask or None), ("skip", f"--skip-fn {a.fns} {mask}".strip())):
        b = copy.copy(a)
        # --dump-at F[,F..] reaches both variants: state_F.rhs per scenario, to `rhost.py diff` a DIFF down to its dwords
        b.headless, b.flatten_out, b.hook, b.frames, b.extra = True, False, None, None, extra
        fetch = ("rhost.log", "save.dat") + (("state_*.rhs",) if a.dump_at else ())
        _, bd, r, out = run_batch(b, runs, "skipcheck-" + tag, fetch=fetch)
        res[tag] = (bd, r)
    ok, tp, ts = 0, 0.0, 0.0
    for n, *_ in runs:
        p, s = res["plain"][1].get(n) or {}, res["skip"][1].get(n) or {}
        bad = [f"{k} {p.get(k)}→{s.get(k)}" for k in ("status", "frame", "rng", "state_sem", "complete") if p.get(k) != s.get(k)]
        fp, fs = (os.path.join(res[t][0], n, "save.dat") for t in ("plain", "skip"))
        if os.path.exists(fp) != os.path.exists(fs) or (os.path.exists(fp) and open(fp, "rb").read() != open(fs, "rb").read()):
            bad.append("save.dat differs")
        if not p or not s:
            bad = ["no result"]
        ok += not bad
        tp += float(p.get("wall_ms", 0) or 0); ts += float(s.get("wall_ms", 0) or 0)
        if bad:
            print(f"{n:42s} DIFF " + "; ".join(bad))
        if a.dump_at:
            for f in sorted(glob.glob(os.path.join(res["plain"][0], n, "state_*.rhs"))):
                print(f"    dump: rhost.py diff {f} {os.path.join(res['skip'][0], n, os.path.basename(f))} --show 60")
    print(f"skip-check {a.fns}: {ok}/{len(runs)} equal (frame, RNG, state_sem, save.dat); wall plain {tp / 1000:.0f}s → skip {ts / 1000:.0f}s "
          f"(×{tp / max(ts, 1):.2f}); {time.time() - t0:.0f}s")
    if ok == len(runs) and not a.dump_at:
        for t in ("plain", "skip"):
            shutil.rmtree(res[t][0], ignore_errors=True)
    return 0 if ok == len(runs) else 1


def cmd_snap_check(a):
    """the gate of rh_snap.c. Per scenario a headless replay with `--snap-test A:B:K` (A / B at 30% / 70% of the
    golden's length): savestate at A; at B hash, restore, replay A..B again, K times. Every pass must hash like the
    first (state + device memory + RNG), and the run must still end on the golden's frame / RNG / save.dat bytes."""
    import copy
    names = a.scenarios or scenario_names(pieces=True)
    if a.only:
        names = [n for n in names if re.search(a.only, n)]
    ref = copy.copy(a); ref.target = "win"
    todo = []
    for n in names:
        tr, save = scenario_inputs(n)
        g = golden_dir(ref, n, tr, save)
        if os.path.exists(os.path.join(g, "meta.json")):
            fr = int(json.load(open(os.path.join(g, "meta.json")))["result"].get("frame", "0") or 0)
            if fr >= 200:
                todo.append((n, tr, save, g, fr))
    print(f"snap-check: {len(todo)} scenario(s), target {a.target}, {a.passes} restore(s) each")
    t0 = time.time()
    ok, rows = 0, []
    import concurrent.futures

    if not PREBUILT and rhost_stale():
        cmd_build(None)
    {"wine": wine_stage, "bunker": lambda: bunker().stage()}.get(a.target, ensure_staged)()   # once, here: the children must not race on it
    child_env = dict(os.environ, RHOST_PREBUILT="1", RHOST_BIN_DIR=bin_dir())   # pinned: a rebuild meanwhile changes nothing

    def one(row):                                   # the window differs per scenario: a child `run` each, -j at a time
        n, tr, save, g, fr = row
        cmd = [sys.executable, os.path.abspath(__file__), "run", n, "--headless", "--target", a.target, "-j", "1",
               f"--extra= --snap-test {int(fr * 0.3)}:{int(fr * 0.7)}:{a.passes}"]
        out = subprocess.run(cmd, capture_output=True, text=True, env=child_env).stdout
        mb = re.search(r"→ (\S+)\]", out)
        bd = mb.group(1) if mb else ""
        log = os.path.join(bd, n, "rhost.log")
        res = [l for l in open(log, errors="replace") if l.startswith("RESULT")] if os.path.exists(log) else []
        r = dict(kv.split("=", 1) for kv in res[-1].split()[1:] if "=" in kv) if res else {}
        m = json.load(open(os.path.join(g, "meta.json")))["result"]
        bad = [f"{k} {m.get(k)}→{r.get(k)}" for k in ("frame", "rng", "complete") if m.get(k) != r.get(k)]
        note = ""
        if r.get("snap_test") == "RESIDUE":
            # the restored passes agree with each other but not with the first (tracked) pass: restore is exact iff they
            # hash like an UNTRACKED replay to that frame (the first pass then carried write-tracking residue)
            out2 = subprocess.run([sys.executable, os.path.abspath(__file__), "run", n, "--headless", "--target", a.target, "-j", "1",
                                   "--frames", r.get("snap_at", "0")], capture_output=True, text=True, env=child_env).stdout
            mb2 = re.search(r"→ (\S+)\]", out2)
            log2 = os.path.join(mb2.group(1), n, "rhost.log") if mb2 else ""
            res2 = [l for l in open(log2, errors="replace") if l.startswith("RESULT")] if os.path.exists(log2) else []
            r2 = dict(kv.split("=", 1) for kv in res2[-1].split()[1:] if "=" in kv) if res2 else {}
            if r2.get("state_sem") == r.get("snap_sem") and r2.get("frame") == r.get("snap_at"):
                note = f"restore exact (= the untracked replay at f{r.get('snap_at')}); the tracked first pass differed: write-tracking residue"
                shutil.rmtree(mb2.group(1), ignore_errors=True)
            else:
                bad.append(f"snap_test=RESIDUE and the restored passes ({r.get('snap_sem')}) differ from an untracked replay ({r2.get('state_sem')})")
        elif r.get("snap_test") != "PASS":
            bad.append(f"snap_test={r.get('snap_test')}")
        gs, hs = os.path.join(g, "save.dat.gz"), os.path.join(bd, n, "save.dat")
        if os.path.exists(gs) and (not os.path.exists(hs) or gzip.open(gs, "rb").read() != open(hs, "rb").read()):
            bad.append("save.dat differs from the golden's")
        st = next((l.strip() for l in open(log, errors="replace") if l.startswith("snap:")), "") if os.path.exists(log) else ""
        if not bad and bd:
            shutil.rmtree(bd, ignore_errors=True)
        return n, fr, bad, st, bd, note
    residue = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for n, fr, bad, st, bd, note in ex.map(one, todo):
            ok += not bad
            residue += bool(note)
            m = re.search(r"dirty=(\d+) pages.*?faults=(\d+) touches=(\d+)", st)
            print(f"{n:42s} f={fr:>7d} [{int(fr * 0.3)}..{int(fr * 0.7)}] " + (f"dirty={m.group(1):>6s} touches={m.group(3):>5s}  " if m else "") +
                  ("PASS" + (f"  [{note}]" if note else "") if not bad else "FAIL " + "; ".join(bad) + f"  → {bd}"))
    print(f"snap-check: {ok}/{len(todo)} PASS ({time.time() - t0:.0f}s)" + (f"; {residue} with write-tracking residue in the first pass" if residue else ""))
    return 0 if ok == len(todo) else 1


# ---------------------------------------------------------------- triage (docs/plans/free-play-capture.md FPC-3)
# Every scenario × the current port build → its first RNG split from the retail golden (the port runs with
# --rng-golden and stops 2 ticks past it), or IDENTICAL to the golden's end + the save verdict. Rows are cached
# per (port exe sha, golden); splits cluster by signature — the split tick's first consumer run whose draw count
# differs, retail FUN chain ⇄ port function — so one bug across many pieces is one row of the report.
TRIAGE_DB = os.environ.get("RHOST_TRIAGE", "/mnt/ephemeral/openrecet/triage")
PORT_EXES = os.path.join(TRIAGE_DB, "port-exes")     # each staged port exe by sha: symbolize its callers later


def port_sha():
    return port_dir().rsplit("\\", 1)[1]


def keep_port_exe():
    os.makedirs(PORT_EXES, exist_ok=True)
    dst = os.path.join(PORT_EXES, port_sha() + ".exe")
    if not os.path.exists(dst):
        shutil.copy2(os.path.join(ROOT, "build", "openrecet.exe"), dst + ".tmp")
        os.replace(dst + ".tmp", dst)
    return dst


def retail_key(stack, rmap):
    """consumer chain of one retail draw: up to 3 FUN names, the rand wrappers / CRT dropped"""
    names = [rmap.get(int(x, 16), x).split("+")[0] for x in stack[:4] if re.fullmatch(r"[0-9a-f]+", x)]
    names = [n for n in names if n.startswith("FUN_") and n not in ("FUN_00471084", "FUN_00471089")]
    return " < ".join(dict.fromkeys(names[:3])) or "?"


def runs_of(keys):
    out = []
    for k in keys:
        if out and out[-1][0] == k:
            out[-1][1] += 1
        else:
            out.append([k, 1])
    return out


def split_signature(rkeys, pkeys):
    """the first consumer run whose length differs (the shared LCG chain pairs row i on both sides until the
    extra / missing draw) → 'retail RUN ⇄ port RUN (retail-port)'"""
    R, P = runs_of(rkeys), runs_of(pkeys)
    for i in range(max(len(R), len(P))):
        r = R[i] if i < len(R) else ["-", 0]
        p = P[i] if i < len(P) else ["-", 0]
        if r[1] != p[1]:
            return f"{r[0]} ⇄ {p[0]} ({r[1] - p[1]:+d})", i
    return "same runs, other LCG state", None


def scene_context(log, f):
    """what retail was doing around tick F: its last status line and anchor before F"""
    scene, anchor = None, None
    for m in re.finditer(r"^frame (\d+) scene=(\d+)|^anchor (\S+) @(\d+)", open(log, errors="replace").read(), re.M):
        if m.group(1) and int(m.group(1)) <= f:
            scene = m.group(2)
        elif m.group(3) and int(m.group(4)) <= f:
            anchor = f"{m.group(3)}@{m.group(4)}"
    return f"scene={scene} last-anchor={anchor}"


def triage_one(a, name):
    tr, save = scenario_inputs(name)
    g = golden_dir(a, name, tr, save)
    batch, bdir, res = golden_get(a, name, tr, save, "triage-retail")
    rd = os.path.join(bdir, name)
    r = res[name]
    frames = int(r.get("frame", "0") or 0)
    row = {"scenario": name, "golden": os.path.basename(g), "frames": frames, "port": port_sha(),
           "retail_complete": r.get("complete"), "retail_status": r.get("status")}
    if not os.path.exists(os.path.join(rd, "trace.flat.jsonl")):
        row.update(verdict="no-flat", sig="retail wrote no flat trace (a legacy op rhost can't replay)")
        return row
    pdir_l = run_port(a, batch, bdir, name, save, os.path.join(rd, "trace.flat.jsonl"), frames + 2,
                      extra=("--rng-golden", "{pw}\\rng.golden", "{pw}\\split.txt"), fetch=("split.txt", "save.dat"),
                      files=(os.path.join(rd, "rng.golden"),))
    sp = os.path.join(pdir_l, "split.txt")
    if not os.path.exists(sp):
        row.update(verdict="no-report", sig="port wrote no split.txt (crash / no --rng-golden)")
        return row
    lines = open(sp).read().splitlines()
    m = re.match(r"split (\d+) retail (\d+) (\S+) port (\d+) (\S+)", lines[0] if lines else "")
    if not m:
        row.update(verdict="identical", checked=int(lines[0].split()[1]) if lines and lines[0].startswith("identical") else None)
        rs, ps = os.path.join(rd, "save.dat"), os.path.join(pdir_l, "save.dat")
        start = save_bytes(save) if save else None
        if os.path.exists(rs) and (start is None or open(rs, "rb").read() != start):
            out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "save_compare.py"), ps, rs],
                                 capture_output=True, text=True).stdout if os.path.exists(ps) else "port wrote no save.dat"
            row["save"] = "IDENTICAL" if "IDENTICAL" in out else (out.strip().splitlines() or ["?"])[0][:200]
            if row["save"] != "IDENTICAL":
                row.update(verdict="save", sig="save: " + row["save"][:80])
        return row
    f = int(m.group(1))
    row.update(verdict="split", split=f, retail_draws=int(m.group(2)), port_draws=int(m.group(4)))
    R = [x for x in rand_rows(os.path.join(rd, "rhost.log")) if x[0] == f]
    rmap = retail_fn_of({int(x, 16) for x in sum((x[3][:4] for x in R), []) if re.fullmatch(r"[0-9a-f]+", x)})
    rkeys = [retail_key(x[3], rmap) for x in R]
    P = [int(l.split("@")[1], 16) for l in lines[1:] if l.startswith("rand ")]
    pmap = port_fn_of(set(P), exe=keep_port_exe())
    pkeys = [pmap.get(v, hex(v)).split(" (")[0] for v in P]
    row["sig"], idx = split_signature(rkeys, pkeys)
    row["context"] = scene_context(os.path.join(rd, "rhost.log"), f)
    row["repro"] = f"rhost.py portrng {name} --window {max(1, f - 2)}:{f + 1} --seq"
    return row


def cmd_triage_one(a):
    row = triage_one(a, a.scenario)
    print("TRIAGE " + json.dumps(row, ensure_ascii=False))
    return 0


def triage_db_path():
    return os.path.join(TRIAGE_DB, f"{port_sha()}.jsonl")


def cmd_triage(a):
    """every scenario (or SCEN.. / --only RE) vs its retail golden under the current port build, -j rows at once;
    cached per (port sha, golden); a clustered report of the splits"""
    import concurrent.futures
    names = a.scenarios or scenario_names(pieces=True)
    if a.only:
        names = [n for n in names if re.search(a.only, n)]
    prebuild(a)
    keep_port_exe()
    # goldens first, one parallel batch (cached ones skip)
    b = argparse.Namespace(**vars(a)); b.scenarios = names; b.only = None; b.refresh = False
    cmd_golden(b)
    os.makedirs(TRIAGE_DB, exist_ok=True)
    db = {}
    if os.path.exists(triage_db_path()):
        for l in open(triage_db_path()):
            if l.strip():
                x = json.loads(l); db[(x["scenario"], x["golden"])] = x
    keys = {n: os.path.basename(golden_dir(a, n, *scenario_inputs(n))) for n in names}
    todo = [n for n in names if a.refresh or (n, keys[n]) not in db]
    print(f"triage: port {port_sha()}, {len(names)} scenarios, {len(names) - len(todo)} cached rows")
    env = dict(os.environ, RHOST_PREBUILT="1", RHOST_BIN_DIR=bin_dir(), RHOST_PORT_DIR=port_dir())

    def one(n):
        cmd = [sys.executable, os.path.abspath(__file__), "triage-one", n, "--target", a.target, "-j", "1",
               "--wine-timeout", str(a.wine_timeout)]
        r = subprocess.run(cmd, capture_output=True, text=True, env=env)
        m = re.search(r"^TRIAGE (\{.*\})\s*$", r.stdout, re.M)
        if m:
            return json.loads(m.group(1))
        err = (r.stderr.strip().splitlines() or r.stdout.strip().splitlines() or ["?"])[-1]
        return {"scenario": n, "golden": keys[n], "verdict": "error", "sig": "error: " + err[:160], "port": port_sha()}
    t0 = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for row in (f.result() for f in concurrent.futures.as_completed([ex.submit(one, n) for n in todo])):
            db[(row["scenario"], row["golden"])] = row
            info = (f"f{row['split']} {row['sig']}" if row["verdict"] == "split" else
                    f"ticks 1..{row.get('checked')}" + (f", save {row['save']}" if row.get("save") else "")
                    if row["verdict"] == "identical" else row.get("sig") or "")
            print(f"  {row['verdict']:9s} {row['scenario']:36s} {info}", flush=True)
    with open(triage_db_path() + ".tmp", "w") as fo:
        for k in sorted(db):
            fo.write(json.dumps(db[k], ensure_ascii=False) + "\n")
    os.replace(triage_db_path() + ".tmp", triage_db_path())
    rows = [db[(n, keys[n])] for n in names if (n, keys[n]) in db]
    triage_report(rows)
    print(f"triage: {len(todo)} run in {time.time() - t0:.0f}s; db {triage_db_path()}")
    return 0


def triage_report(rows):
    from collections import Counter, defaultdict
    print("\nverdicts: " + ", ".join(f"{k} {v}" for k, v in Counter(r["verdict"] for r in rows).most_common()))
    short = [r["scenario"] for r in rows if r["verdict"] == "identical" and r.get("retail_complete") != "1"]
    if short:   # identical as far as retail got: the trace stalls / ends early in rhost itself
        print(f"  of the identical: {len(short)} where retail itself did not complete ({', '.join(short)[:200]})")
    cl = defaultdict(list)
    for r in rows:
        if r["verdict"] != "identical":
            cl[r.get("sig", "?")].append(r)
    if not cl:
        print("every scenario IDENTICAL to its golden"); return
    print(f"{len(cl)} signature(s), most scenarios first:")
    for sig, rs in sorted(cl.items(), key=lambda kv: (-len(kv[1]), min(x.get("split", 1 << 30) for x in kv[1]))):
        first = min(rs, key=lambda x: x.get("split", 1 << 30))
        print(f"  {len(rs):3d}× {sig}")
        print(f"       first: {first['scenario']} f{first.get('split', '-')} {first.get('context', '')}")
        if first.get("repro"):
            print(f"       repro: python3 tools/rhost/{first['repro']}")
        if len(rs) > 1:
            print("       also: " + ", ".join(f"{x['scenario']}" + (f" f{x['split']}" if 'split' in x else "") for x in rs if x is not first)[:300])


def cmd_portsave(a):
    """whole-flow port↔retail check: retail (rhost) vs port (--det-loads, flat trace) committed save.dat"""
    name = a.scenario
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = None
    batch, bdir, res = retail_side(a, name, tr, save, "portsave-retail", "", ("rhost.log", "save.dat", "trace.flat.jsonl"))
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


def cmd_portcensus(a):
    """which frames exercise which render arms: the port (--fx-census) on the run's flat trace writes,
    per frame, the table-A / B types newly live (the tables match retail's — portblock); prints each
    type's first frames and the live spans' count (A = DAT_069b2f80 +0x30, B = DAT_069324b0 +0)"""
    import collections
    name = a.scenario
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = None
    batch, bdir, res, _ = run_batch(a, [(name, tr, None, save)], "portcensus-retail", fetch=("rhost.log", "trace.flat.jsonl"))
    frames = int(res.get(name, {}).get("frame", "0")) or 120000
    pdir_l = run_port(a, batch, bdir, name, save, os.path.join(bdir, name, "trace.flat.jsonl"), frames,
                      extra=("--fx-census", "{pw}\\census.txt"), fetch=("census.txt",))
    path = os.path.join(pdir_l, "census.txt")
    if not os.path.exists(path):
        print("port wrote no census.txt"); return 2
    seen = collections.defaultdict(list)
    for line in open(path):
        f, k, t = line.split()
        seen[(k, int(t))].append(int(f))
    for (k, t) in sorted(seen):
        fs = seen[(k, t)]
        print(f"{k} {t:4d}: {len(fs):5d} spans, first f{', f'.join(str(x) for x in fs[:6])}")
    print(f"census: {path}")
    return 0


def cmd_portanchors(a):
    """port vs retail anchor timeline on the same flat trace (port --det-loads): first divergence"""
    name = a.scenario
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = None
    batch, bdir, res = retail_side(a, name, tr, save, "portanchors-retail", "--log-anchors", ("rhost.log", "trace.flat.jsonl"))
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
    fs = parse_frame_list(a.at)
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = None
    a.shot_at = ",".join(str(f) for f in fs)
    a.frames = a.frames or max(fs) + 2       # retail stops past the last shot, not at the trace's end
    batch, bdir, res, _ = run_batch(a, [(name, tr, None, save)], "portshots-retail", fetch=("rhost.log", "trace.flat.jsonl", "shot_*.png"))
    extra = ("--capture-to", "{pw}", "--capture-frames", a.shot_at)
    if getattr(a, "port_d3d", False):
        extra += ("--d3d-trace", "{pw}\\d3d.jsonl", "--d3d-trace-frames", a.shot_at, "--d3d-trace-verts")
    run_port(a, batch, bdir, name, save, os.path.join(bdir, name, "trace.flat.jsonl"), max(fs) + 2, extra=extra)
    pdir_l = os.path.join(bdir, "port")
    subprocess.run(["scp", "-q", f"{WIN}:C:/orc-rhost/runs/{batch}/port/frame_*.bmp", pdir_l], capture_output=True, text=True)
    if getattr(a, "port_d3d", False):
        subprocess.run(["scp", "-q", f"{WIN}:C:/orc-rhost/runs/{batch}/port/d3d.jsonl", pdir_l], capture_output=True, text=True)
        print(f"port d3d trace: {os.path.join(pdir_l, 'd3d.jsonl')}")
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
    if getattr(a, "rank", False) and out:
        rank_shots(out)
    return 0 if len(out) == len(fs) else 1


def parse_frame_list(spec):
    """comma-separated items, each 'F' or 'FROM:TO:STEP' (TO inclusive) → frame list"""
    fs = []
    for item in spec.split(","):
        if ":" in item:
            lo, hi, st = (int(x) for x in item.split(":"))
            fs += range(lo, hi + 1, st)
        else:
            fs.append(int(item))
    return fs


def rank_shots(paths):
    """per comparison image (retail | port halves): mean abs difference and the pixels whose
    summed channel difference exceeds 60, worst first — where to look"""
    try:
        import numpy as np
        from PIL import Image
    except ImportError:
        print("rank: needs numpy + PIL"); return
    rows = []
    for p in paths:
        im = np.asarray(Image.open(p).convert("RGB")).astype(int)
        h = im.shape[1] // 2
        d = np.abs(im[:, :h] - im[:, h:])
        rows.append((d.mean(), int((d.sum(axis=2) > 60).sum()), p))
    print("rank (mean abs, px>60):")
    for m, n, p in sorted(rows, reverse=True):
        print(f"  {m:7.2f} {n:8d}  {os.path.basename(p)}")


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


def port_fn_of(rvas, exe=None):
    """port caller RVA → function (file:line) via addr2line on EXE (default build/openrecet.exe; image base 0x400000)"""
    exe = exe or os.path.join(ROOT, "build", "openrecet.exe")
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
    lo_s, hi_s = a.window.split(":")
    lo, hi = int(lo_s), (10 ** 7 if hi_s == "end" else int(hi_s))
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = None
    batch, bdir, res = retail_side(a, name, tr, save, "portrng-retail", f"--log-rand {lo - 1}:{hi}", ("rhost.log", "trace.flat.jsonl"))
    # retail stops at its run end (trace end + the tail); past it every port draw would read as a diff
    rlast = int(res.get(name, {}).get("frame", "0") or 0)
    if rlast and hi > rlast:
        print(f"window clamped to the retail run: f{lo}..f{rlast} (asked f{hi})")
        hi = rlast
    pdir_l = run_port(a, batch, bdir, name, save, os.path.join(bdir, name, "trace.flat.jsonl"), hi + 2,
                      extra=("--rng-log", "{pw}\\rng.log", f"{lo}:{hi}"), fetch=("rng.log",))
    # rhost labels tick N's draws fN (= its `post N`, = the port's label); a deferred loader's draws at
    # the boundary before tick N are logged "fN-1 … worker" — the port labels those N too
    R = [(f, "rand", s0, s1, st) for f, s0, s1, st in rand_rows(os.path.join(bdir, name, "rhost.log"))]
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
    if bad and getattr(a, "seq", False):
        # the first differing tick draw by draw, retail | port side by side (the LCG chain is shared, so row i
        # is the same LCG step on both sides until the extra / missing draw); --rng-log gives one port frame
        f = bad[0]
        rs = [rkey(r) or "?" for r in R if r[0] == f]
        ps = [pmap.get(p[4], hex(p[4])).split(" (")[0] for p in P if p[0] == f and p[1] == "rand"]
        print(f"f{f} draw by draw (retail | port):")
        for i in range(max(len(rs), len(ps))):
            print(f"  {i:4d}  {(rs[i] if i < len(rs) else '-')[:70]:70s} | {ps[i] if i < len(ps) else '-'}")
    if not bad:
        print("IDENTICAL per-tick draw counts in the window")
    else:
        print("frames with different draw counts (retail/port): " + ", ".join(f"f{f} {rc.get(f, 0)}/{pc.get(f, 0)}" for f in bad[:12]))
    return 0 if not bad else 1


# The regression gate (docs/FRONT.md §Regression gate): every row is expected IDENTICAL.
GATES = ([("portsave", s) for s in ("house-pause-save-commit", "house-pause-save-commit-empty", "day2-save-commit",
                                      "multiday-tutorials-suggest", "buy-from-seller-guild-intro",
                                      "dungeon0-floors-save", "dungeon0-full", "dungeon1-run2", "dungeon1-deep",
                                      "debt-week1-paid", "dungeon1-clear", "dungeon2-run2", "dungeon2-run3",
                                      "dungeon3-run2-save", "dungeon3-floors", "dungeon3-deep")]
         + [("portanchors", s) for s in ("house-idle", "house-customer-tutorial")]
         # a new game from the title (no save): FUN_00490e56's overrun (the port takes the run's
         # DirectMusic pointer byte, engine quirk §163) + the prologue dispatch
         + [("portrng", s) for s in ("house-idle", "intro-skip-prompt")]
         + [("portrng", s) for s in ("buy-35pct-accept", "buy-40pct-accept", "buy-45pct-accept", "buy-50pct-accept",
                                      "buy-lowball-walkout", "buy-counter-raise-accept", "day2-shop",
                                      "day-end-next-morning", "day2-buy-tutorial", "multiday-tutorials-suggest",
                                      "buy-from-seller-guild-intro", "debt-gameover-restart")]
         + [("portrng", s) for s in ("dungeon0-floors-save", "adv-guild-pack-leave", "dungeon0-full",
                                      "dungeon0-wipe", "dungeon1-run", "dungeon1-run2", "dungeon1-deep",
                                      "dungeon1-charme", "tear-lectures-gameover", "debt-week1-paid",
                                      "dungeon1-clear", "dungeon2-run", "dungeon2-run2", "dungeon2-run3",
                                      "dungeon3-run", "dungeon3-run2", "dungeon3-floors", "dungeon3-deep")]
         # agent-authored traces (tools/drive, 2026-11-10): menus / pages / town / shop sessions / weeks 3–4 / vending
         # machines no recording reached; all triage-identical at 10e36b61
         + [("portsave", s) for s in ("pause-options-saved", "payday-week2-paid", "shop-session-week3", "calendar-week3",
                                      "calendar-week4", "change-tables-l16", "vender-sales-l16", "vender-revert-sell",
                                      "item-storage-l17", "endless-first-days", "week5-gameover", "order-deadline-missed",
                                      "guild-talk-topics")]
         + [("portrng", s) for s in ("tear-news-summary", "tear-outstanding-orders", "tear-menu-pages", "guild-fusion-confirm",
                                      "guild-sell-expansion", "guild-ui-flow", "decor-defaults", "decor-variants",
                                      "counter-move-tables", "market-buy-display", "pause-options-saved", "pause-status-items",
                                      "pause-encyclopedia", "payday-week2-paid", "gameover-loop-dungeon0", "town-rounds-week3",
                                      "pub-evening-week3", "shop-session-week3", "order-customer-week3", "calendar-week3",
                                      "calendar-week4", "dungeon2-21f-25f", "vending-level16", "vender-sales-l16",
                                      "vender-revert-sell", "change-tables-l16", "item-storage-l17", "endless-first-days",
                                      "week5-gameover", "order-deadline-missed", "guild-talk-topics", "title-extras")]
         # 2026-11-11: probes on crafted saves (the day-8 visitor, the stand's item status, moved tables, three vender
         # kinds, six hires, Obsidian Tower 1F–3F, the 8-row roster, the dungeon pause) and boss "arms" movies cut from
         # played boss floors (crown slime 5F / its wipe / 25F, mushroom king, Charme)
         + [("portsave", s) for s in ("day8-visitor", "move-tables-persist", "venders-limit", "amber-26f-30f-tielle",
                                       "obsidian-3f-5f-pumpkin", "obsidian-6f-10f-jellyfish",
                                       "obsidian-11f-15f-ahrimans", "obsidian-16f-20f-golem",
                                       "obsidian-21f-30f-griff", "obsidian-31f-40f-ohms",
                                       "obsidian-41f-50f-finale", "lapis-1f-10f", "lapis-11f-20f", "lapis-21f-30f",
                                       "lapis-30f-story", "lapis-31f-40f", "ending-week5-paid", "newgame-plus",
                                       "survival-start", "survival-hell-start", "lapis-41f-50f", "lapis-51f-60f", "lapis-61f-70f")]
         + [("portrng", s) for s in ("day8-visitor", "stand-item-status", "move-tables-persist", "venders-limit",
                                      "hire-caillou", "hire-tielle", "hire-elan", "hire-nagi", "hire-griff", "hire-arma",
                                      "obsidian-1f", "guild-roster-8", "pause-equip-dungeon", "dungeon2-25f-crown-kill",
                                      "crown-slime-arms", "crown-slime-wipe", "mushroom-king-arms", "charme-arms",
                                      "item-status-pause", "amber-26f-30f-tielle", "obsidian-3f-5f-pumpkin",
                                      "obsidian-6f-10f-jellyfish", "obsidian-11f-15f-ahrimans",
                                      "obsidian-16f-20f-golem", "obsidian-21f-30f-griff",
                                      "obsidian-31f-40f-ohms", "obsidian-41f-50f-finale", "lapis-1f-10f", "lapis-11f-20f",
                                      "lapis-21f-30f", "lapis-30f-story", "lapis-31f-40f", "ending-week5-paid",
                                      "newgame-plus", "survival-start", "survival-hell-start", "lapis-41f-50f", "lapis-51f-60f", "lapis-61f-70f")])


GATE_KEYS = ("save:", "anchors:", "first ", "window", "IDENTICAL", "frames with", "error", "scp failed")


def gate_rows(a):
    only = re.compile(a.only) if a.only else None
    return [(kind, scen, *win) for kind, scen, *win in GATES if not (only and not only.search(f"{kind} {scen}"))]


def cmd_gates(a):
    """the regression gate in one command: port saves, anchors and RNG streams vs retail, one line per gate.
    -j N runs N rows at once (each a child `rhost.py KIND SCEN`; this process builds + stages once)"""
    rows = gate_rows(a)
    if a.jobs > 1 and len(rows) > 1:
        return gates_parallel(a, rows)
    import contextlib, copy, io
    fns = {"portsave": cmd_portsave, "portanchors": cmd_portanchors, "portrng": cmd_portrng}
    fails = 0
    for kind, scen, *win in rows:
        b = copy.copy(a)
        b.scenario = scen
        b.window, b.context = (win[0] if win else "1:end"), 3
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                r = fns[kind](b)
        except BaseException as e:      # a failed upload / run is a failed gate, not a crash of the set
            r = 1
            buf.write(f"error: {e}\n")
        lines = [l.strip() for l in buf.getvalue().splitlines() if l.strip().startswith(GATE_KEYS)]
        fails += bool(r)
        print(f"{'ok  ' if not r else 'FAIL'} {kind:<11s} {scen:<32s} " + " | ".join(lines[:3]), flush=True)
    print(f"gates: {fails} failed")
    return 1 if fails else 0


def prebuild(a):
    """build + stage rhost and the port once, prune once: children run with RHOST_PREBUILT=1"""
    if getattr(a, "target", "win") == "bunker":
        raise SystemExit("--target bunker is retail-only (headless batches): gates / port commands run on win / wine")
    global _pruned
    if rhost_stale():
        cmd_build(None)
    build_port()
    if getattr(a, "target", "win") == "wine":
        wine_stage()
    else:
        if PRUNE_HOURS > 0 and not _pruned:
            _pruned = True
            print(prune_win_runs())
        ensure_staged()
        stage_port()


def gates_parallel(a, rows):
    import concurrent.futures
    prebuild(a)
    if not getattr(a, "no_golden", False) and not a.extra:
        b = argparse.Namespace(**vars(a))
        b.scenarios, b.only, b.refresh = sorted({r[1] for r in rows}), None, False
        cmd_golden(b)
    # the children run exactly the binaries staged here, whatever gets built meanwhile
    env = dict(os.environ, RHOST_PREBUILT="1", RHOST_BIN_DIR=bin_dir(), RHOST_PORT_DIR=port_dir())
    t0 = time.time()

    def one(row):
        kind, scen, *win = row
        cmd = [sys.executable, os.path.abspath(__file__), kind, scen, "--target", a.target, "-j", "1",
               "--wine-timeout", str(a.wine_timeout)]
        if kind == "portrng":
            cmd += ["--window", win[0] if win else "1:end"]
        if a.extra:
            cmd += ["--extra", a.extra]
        t = time.time()
        r = subprocess.run(cmd, capture_output=True, text=True, env=env)
        out = r.stdout + ("\nerror: " + r.stderr.strip().splitlines()[-1] if r.returncode and r.stderr.strip() else "")
        lines = [l.strip() for l in out.splitlines() if l.strip().startswith(GATE_KEYS)]
        return row, r.returncode, lines, time.time() - t
    done = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = [ex.submit(one, r) for r in rows]
        for f in concurrent.futures.as_completed(futs):       # each row as it finishes
            row, rc, lines, dt = f.result()
            done[tuple(row)] = (rc, lines)
            print(f"{'ok  ' if not rc else 'FAIL'} {row[0]:<11s} {row[1]:<32s} {dt:6.0f}s " + " | ".join(lines[:3]), flush=True)
    fails = sum(1 for rc, _ in done.values() if rc)
    print(f"gates: {fails} failed ({len(rows)} rows, -j{a.jobs}, {time.time() - t0:.0f}s wall)")
    return 1 if fails else 0


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
    if len(vas) > 64:   # RH_MAX_WATCH (tools/rhost/rhost.h)
        raise SystemExit(f"portstate: {len(vas)} VAs, rhost watches at most 64")
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
    if getattr(a, "window", None):
        lo, hi = a.window.split(":")
        lo, hi = int(lo or 0), (1 << 31) if hi in ("", "end") else int(hi)
        common_f = [f for f in common_f if lo <= f <= hi]
    print(f"frames: retail {len(R)} / port {len(P)} / both {len(common_f)}")
    if not common_f:
        print("no frames on both sides — nothing compared (port run failed? see the port state.log)")
        return 2
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
    if getattr(a, "changes", False):
        # every frame where any listed VA changes on either side: the two timelines of the writes
        def fmt(v):
            return v if not a.floats or v is None else "%.6g" % struct.unpack("<f", struct.pack("<i", int(v)))[0]
        prev = None
        print("changes (retail | port):")
        for f in common_f:
            cur = tuple(R[f].get(v) for v in vas) + tuple(P[f].get(v) for v in vas)
            if cur != prev:
                mark = " *" if any(R[f].get(v) != P[f].get(v) for v in vas) else ""
                print(f"  f{f:<6d} " + "  ".join(f"{v}={fmt(R[f].get(v))}|{fmt(P[f].get(v))}" for v in vas) + mark)
                prev = cur
    if firsts:
        f0 = min(firsts)
        print(f"around f{f0} (retail | port):")
        for f in range(max(0, f0 - a.context), f0 + a.context + 1):
            if f in R and f in P:
                mark = " *" if any(R[f].get(v) != P[f].get(v) for v in vas) else ""
                print(f"  f{f:<6d} " + "  ".join(f"{v}={R[f].get(v)}|{P[f].get(v)}" for v in vas) + mark)
    return 1 if firsts else 0


# the port's --block-dump blocks: name, retail VA, record stride (0 = flat)
PORT_BLOCKS = [("party", 0x56da1b8, 0), ("actors", 0x76b970, 0xba4), ("dobj", 0x695ef78, 0x2a0),
               ("popup", 0x44e28f0, 28), ("recb", 0x69324b0, 0x124), ("recc", 0x6956cb0, 0x94),
               ("drop", 0x438bedc, 0x18), ("mapcol_objs", 0x438bfb0, 0),
               ("enemy-table", 0x5c23f0, 0x68), ("enemylist", 0x53f8e8, 0x2f0),
               ("item-table", 0x95d37d0, 0x2cc), ("model-table", 0x73ae258, 0x2b8),
               ("stage-table", 0x68dd2f8, 0x1b3c), ("chara-table", 0x73ae058, 0x40),
               ("gousei-table", 0x9640650, 0x30), ("snews-names", 0x73d8ee0, 0x44),
               ("snews-sections", 0x73b2108, 0xa8), ("oder-table", 0x6a5db98, 0x4c),
               ("news-table", 0x56e0e00, 0xbc), ("reca", 0x69b2f80, 0x94)]


def read_pblk(path):
    b = open(path, "rb").read()
    if b[:4] != b"PBLK":
        raise SystemExit(f"{path}: not a PBLK block dump")
    frame, n = struct.unpack_from("<2I", b, 4)
    off, out = 12, []
    for _ in range(n):
        va, ln = struct.unpack_from("<2I", b, off)
        off += 8
        out.append((va, list(struct.unpack_from(f"<{ln // 4}I", b, off))))
        off += ln
    return frame, out


def cmd_portframes(a):
    """port vs retail at one frame: every loaded model's frame tree (FUN_00403e33's combined
    matrices) — retail's from its heap (DAT_073da2f0 + 0x2f0·n → +0x204 → +4, a frame's name at
    +0x13c, combined +0xc4, child +0x134, sibling +0x130), the port's --model-dump; frame by frame
    in tree order, the first differing frames per model (the skinned poses: Ohm, the crab)"""
    name, f = a.scenario, a.at
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = str(f)
    batch, bdir, res, _ = run_batch(a, [(name, tr, None, save)], "portframes-retail",
                                    fetch=("rhost.log", "trace.flat.jsonl", "state_*.rhs"))
    R = rhs.Dump(os.path.join(bdir, name, f"state_f{f:06d}.rhs"))
    pdir_l = run_port(a, batch, bdir, name, save, os.path.join(bdir, name, "trace.flat.jsonl"), f + 2,
                      extra=("--model-dump", str(f), "{pw}\\frames.txt"), fetch=("frames.txt",))

    def w(va):
        for lo, hi in R.ranges:
            if lo <= va < hi:
                return R.words[lo][(va - lo) // 4]
        return None

    def cstr(va):
        out = b""
        for k in range(0, 64, 4):
            x = w(va + k)
            if x is None:
                break
            out += struct.pack("<I", x)
            if b"\0" in out:
                break
        return out.split(b"\0")[0].decode("latin-1")

    def rtree(fr, depth, out, guard):
        while fr and len(out) < 2000 and fr not in guard:
            guard.add(fr)
            nm = w(fr + 0x13c)
            comb = [struct.unpack("<f", struct.pack("<I", w(fr + 0xc4 + 4 * k) or 0))[0] for k in range(16)]
            out.append((depth, cstr(nm) if nm else "-", comb))
            rtree(w(fr + 0x134), depth + 1, out, guard)
            fr = w(fr + 0x130)

    path = os.path.join(pdir_l, "frames.txt")
    if not os.path.exists(path):
        print("port wrote no frames.txt"); return 2
    P = {}
    for line in open(path):
        t = line.split()
        P.setdefault(int(t[0]), []).append((int(t[1]), t[2], [float(x) for x in t[3:19]]))
    bad = 0
    for n, pf in sorted(P.items()):
        P_ = w(0x73da2f0 + 0x2f0 * n + 0x204)
        root = w(P_ + 4) if P_ else None
        rf = []
        rtree(root, 0, rf, set())
        nd = []
        for k, (rr, pp) in enumerate(zip(rf, pf)):
            d = max(abs(x - y) for x, y in zip(rr[2], pp[2]))
            if rr[1] != pp[1] or d > 1e-3:
                nd.append((k, rr, pp, d))
        print(f"model {n}: retail {len(rf)} frames / port {len(pf)}, {len(nd)} differ")
        for k, rr, pp, d in nd[:12]:
            print(f"  #{k:3d} d{rr[0]} {rr[1]:<24s} | {pp[1]:<24s} max|d| {d:.4g}  t r {[round(x, 3) for x in rr[2][12:15]]} p {[round(x, 3) for x in pp[2][12:15]]}")
        bad += len(nd)
    return 1 if bad else 0


def cmd_portblock(a):
    """port vs retail at a frame (or several, --at F,F,.. in one run): every dword of the retail-layout
    blocks (party, actors, stage objects, popups, records B / C, drop rows, map objects, flow scalars,
    the active work bank), grouped by record"""
    name = a.scenario
    frames = sorted({int(x) for x in str(a.at).split(",") if x.strip()})
    tr, save = scenario_inputs(name)
    a.flatten_out = True
    a.dump_at = ",".join(str(f) for f in frames)
    a.frames = a.frames or frames[-1] + 2
    batch, bdir, res, _ = run_batch(a, [(name, tr, None, save)], "portblock-retail",
                                    fetch=("rhost.log", "trace.flat.jsonl", "state_*.rhs"))
    # the port names each dump blocks.bin.F when given several frames (blocks.bin for one)
    pfiles = ["blocks.bin"] if len(frames) == 1 else [f"blocks.bin.{f}" for f in frames]
    pdir_l = run_port(a, batch, bdir, name, save, os.path.join(bdir, name, "trace.flat.jsonl"), frames[-1] + 2,
                      extra=("--block-dump", ",".join(str(f) for f in frames), "{pw}\\blocks.bin"),
                      fetch=tuple(pfiles))
    worst = 0
    for f, pfile in zip(frames, pfiles):
        worst = max(worst, block_report(a, f, os.path.join(bdir, name, f"state_f{f:06d}.rhs"),
                                        os.path.join(pdir_l, pfile)))
    return worst


def block_report(a, f, rhs_path, pblk_path):
    R = rhs.Dump(rhs_path)
    pf, P = read_pblk(pblk_path)

    def rword(va):
        for lo, hi in R.ranges:
            if lo <= va < hi:
                return R.words[lo][(va - lo) // 4]
        return None

    def fmt(v):
        fv = struct.unpack("<f", struct.pack("<I", v))[0]
        return f"{v:#010x} {struct.unpack('<i', struct.pack('<I', v))[0]:>11d} {fv:>13.7g}"

    def enemylist_unread(va):
        """the enemylist (DAT_0053f8e8) is .data with a build-time image; the boot parse
        scrubs only floor_lo and the enemy ids, so retail keeps the image's other fields.
        Every reader stops at a section with floor_lo -1 and at a slot with enemy_id -1
        (the walks at all_ida 32230..32625): a dword past those is never read."""
        rel = (va - 0x53f8e8) // 4
        sec, d = rel // 188, rel % 188
        for t in range(sec - sec % 60, sec):                     # an earlier sentinel section
            if rword(0x53f8e8 + 4 * 188 * t) == 0xffffffff:
                return True
        if d == 0:
            return False
        if rword(0x53f8e8 + 4 * 188 * sec) == 0xffffffff:        # the sentinel itself: only lo is read
            return True
        slot = (d - 2) // 3 if d < 95 else (d - 95) // 3
        term = next((k for k in range(31) if rword(0x53f8e8 + 4 * (188 * sec + 2 + 3 * k)) == 0xffffffff), 31)
        if d >= 95:
            return slot >= term
        return slot > term or (slot == term and (d - 2) % 3 != 0)

    total = 0
    for va0, words in P:
        blk = next((b for b in PORT_BLOCKS if b[1] == va0), None)
        bname, stride = (blk[0], blk[2]) if blk else (("work" if 0x44e3798 <= va0 < 0x44e3798 + 8 * 0x2dfc8 else f"{va0:x}"), 0)
        diffs, unread = [], 0
        for i, pv in enumerate(words):
            va = va0 + 4 * i
            rv = rword(va)
            if rv is None or rv == pv or va in R.vol:
                continue
            if bname == "enemylist" and enemylist_unread(va):
                unread += 1
                continue
            diffs.append((va, rv, pv))
        if unread:
            print(f"== {bname}: {unread} differing dwords no reader reaches (.data image leftovers) skipped")
        if not diffs:
            continue
        total += len(diffs)
        print(f"== {bname} ({va0:#x}): {len(diffs)} dwords differ")
        for va, rv, pv in diffs[:a.list]:
            where = f"#{(va - va0) // stride} +{(va - va0) % stride:#x}" if stride else f"+{va - va0:#x}"
            print(f"   {va:#09x} {where:>12s}   retail {fmt(rv)}   port {fmt(pv)}")
        if len(diffs) > a.list:
            print(f"   … {len(diffs) - a.list} more")
    print(f"portblock f{f} (port dump f{pf}): {total} dwords differ" if total else f"portblock f{f}: all blocks IDENTICAL")
    return 1 if total else 0


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
    if a.from_scen and a.save:
        print("record: --from or --save, not both"); return 2
    ensure_staged()
    save = end_save(a.from_scen) if a.from_scen else save_arg(a.save) if a.save else None
    if save:
        print(f"[recording from {os.path.relpath(save, ROOT)}]")
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
        src = f"# save: the one {a.from_scen} ends with (rhost.py record --from {a.from_scen})\n" if a.from_scen else ""
        open(os.path.join(dst, "trace.jsonl"), "w").write(json.dumps({"savefile": rel}) + "\n" + src + body)
    print("wrote", os.path.join(dst, "trace.jsonl"))
    # self-check: replaying the recording must reproduce the recorded final state
    m = re.search(r"RESULT .*?frame=(\d+).*?state_sem=([0-9a-f]+)", out)
    if not m:
        print("record: no RESULT line — cannot self-check"); return 1
    a2 = replay_args(frames=int(m.group(1)), dump_at="end")
    _, rb, res, _ = run_batch(a2, [(a.name, os.path.join(dst, "trace.jsonl"), None, save)], "recheck",
                              fetch=("rhost.log", "state_end.rhs", "save.dat"))
    rep = res.get(a.name, {})
    rows = rhs.diff(os.path.join(bdir, "rec", "state_end.rhs"), os.path.join(rb, a.name, "state_end.rhs"), show=20)
    real = [r for r in rows if r[3] not in ("ext-ptr(both)", "ext-ptr(stale)", "volatile-addr")]
    ok = not real and rep.get("frame") == m.group(1)
    print(f"record self-check: recorded f={m.group(1)} | replay f={rep.get('frame')} | non-pointer diffs={len(real)} → {'PASS' if ok else 'FAIL'}")
    if ok:   # the self-check replay already ran the whole recording: keep its end save for `--from NAME`
        blob = store_end_save(a.name, os.path.join(dst, "trace.jsonl"), save, os.path.join(rb, a.name, "save.dat"), rep.get("frame"))
        print(f"[{a.name} ends with save {os.path.basename(blob).split('.')[0]}: `record --from {a.name}` starts from it, no replay]"
              if blob else f"[{a.name} commits no save]")
    return 0 if ok else 1


# ---------------------------------------------------------------- free-play capture (FPC-4)
# The owner plays on the Windows desktop through C:\orc-rhost\play\play.cmd (win/play.ps1): rhost --record --cut,
# one piece per process, auto-resumed with --preamble. See docs/plans/free-play-capture.md.
PLAY = WROOT + "\\play"
PLAY_FILES = [os.path.join(HERE, "win", f) for f in ("play.ps1", "play.cmd", "selfcheck.ps1", "selfcheck.py")] + \
    [os.path.join(HERE, "rhs.py"), os.path.join(HERE, "recet.ini"), VOLATILE]
SHORTCUT = "Recettear capture"


def play_install(root):
    """the current rhost build frozen in ROOT\\bin\\<sha> (never pruned, untouched by later stages) + the session
    scripts → sha"""
    ensure_staged()
    bd = bin_dir()
    sha = bd.rsplit("\\", 1)[1]
    ssh_win(f"New-Item -ItemType Directory -Force '{root}\\bin\\{sha}','{root}\\sessions' | Out-Null; "
            f"if (!(Test-Path '{root}\\bin\\{sha}\\rhost.exe')) {{ Copy-Item '{bd}\\rhost.exe' '{root}\\bin\\{sha}\\rhost.exe.tmp'; "
            f"Move-Item '{root}\\bin\\{sha}\\rhost.exe.tmp' '{root}\\bin\\{sha}\\rhost.exe' }}; exit 0")
    for f in PLAY_FILES:
        scp(f, f"{WIN}:{root.replace(chr(92), '/')}/{os.path.basename(f)}")
    # the launcher must not ship a syntax error: parse every script there
    errs = ssh_win("$e = @(); foreach ($f in 'play.ps1','selfcheck.ps1') { $t = $null; $x = $null; "
                   f"[void][System.Management.Automation.Language.Parser]::ParseFile('{root}\\' + $f, [ref]$t, [ref]$x); "
                   "foreach ($i in $x) { $e += \"${f}:$($i.Extent.StartLineNumber): $($i.Message)\" } }; $e; exit 0").strip()
    if errs:
        raise SystemExit("session scripts do not parse:\n" + errs)
    return sha


def cmd_session_install(a):
    """freeze the current rhost build as the launcher's, upload the session scripts, (re)create the Desktop
    shortcut; --save seeds play\\latest.sav"""
    sha = play_install(PLAY)
    if a.save:
        src = save_arg(a.save)
        tmp = os.path.join(BUILD, "latest.sav")
        open(tmp, "wb").write(save_bytes(src))
        have = ssh_win(f"if (Test-Path '{PLAY}\\latest.sav') {{ 'yes' }}; exit 0").strip()
        if have == "yes" and not a.force:
            print(f"session-install: {PLAY}\\latest.sav exists (the playthrough's newest save) — --force to replace it")
        else:
            scp(tmp, f"{WIN}:{PLAY.replace(chr(92), '/')}/latest.sav")
            print(f"seeded latest.sav from {os.path.relpath(src, ROOT)}")
    ssh_win(f"Set-Content '{PLAY}\\current.txt' '{sha}'; "
            "$d = [Environment]::GetFolderPath('Desktop'); $w = New-Object -ComObject WScript.Shell; "
            f"$l = $w.CreateShortcut((Join-Path $d '{SHORTCUT}.lnk')); $l.TargetPath = '{PLAY}\\play.cmd'; "
            f"$l.WorkingDirectory = '{PLAY}'; $l.IconLocation = '{WROOT}\\game\\recettear.unpacked.exe,0'; "
            "$l.Description = 'Recettear free-play capture (OpenRecet rhost)'; $l.Save(); exit 0")
    print(f"session-install: rhost {sha} frozen in {PLAY}\\bin\\{sha}; Desktop shortcut '{SHORTCUT}' → {PLAY}\\play.cmd")
    return 0


PS_PIECES = (
    "Get-ChildItem '@ROOT@\\sessions' -Directory -ErrorAction SilentlyContinue | ForEach-Object { $s = $_.Name; "
    "Get-ChildItem $_.FullName -Directory | ForEach-Object { $m = Join-Path $_.FullName 'meta.json'; "
    "$c = Join-Path $_.FullName 'check.json'; $o = @{ session = $s; piece = $_.Name; done = (Test-Path $m) }; "
    "if (Test-Path $m) { $o.meta = Get-Content $m -Raw | ConvertFrom-Json }; "
    "if (Test-Path $c) { $o.check = Get-Content $c -Raw | ConvertFrom-Json }; "
    "$o.files = @(Get-ChildItem $_.FullName -File | ForEach-Object { $_.Name }); "
    "$o | ConvertTo-Json -Depth 6 -Compress } }")


def win_pieces(root=PLAY):
    out = ssh_win(PS_PIECES.replace("@ROOT@", root))
    return [json.loads(l) for l in out.splitlines() if l.strip().startswith("{")]


def piece_line(p):
    m, c = p.get("meta") or {}, p.get("check") or {}
    r = m.get("result") or {}
    cut = m.get("cut") or {}
    why = r.get("reason") or (f"exit {m.get('exit')}" if m else "in progress")
    extra = f" day {cut.get('day', 0) + 1} → slot {cut.get('slot', 0) + 1}" if cut.get("reason") == "cut-save" else \
        f" resume {cut.get('resume')}" if cut.get("reason") == "cut-start" else ""
    return (f"pt-{p['session']}-{p['piece']}  {why:14s}{extra:22s} f={r.get('frame', '-'):>7}  "
            f"{m.get('wall_s', '-')!s:>6}s  start_day={cut.get('start_day', '-')}  check={c.get('status', '-')}")


def cmd_session_status(a):
    ps = win_pieces(a.root)
    if not ps:
        print(f"no capture pieces under {PLAY}\\sessions"); return 0
    for p in ps:
        print(piece_line(p))
    print(f"{sum(1 for p in ps if p['done'])} finished piece(s), "
          f"{sum(1 for p in ps if (p.get('check') or {}).get('status') == 'PASS')} self-check PASS")
    return 0


def cmd_session_test(a):
    """the capture loop end to end, headless: play.ps1 under the queue runner in C:\\orc-rhost\\playtest; piece 1
    replays SCEN as its "human" (with --cut: a day-boundary save / a second game start ends it), the next pieces
    resume through the preamble and record zero input (an off-screen window: no DInput) for --frames; then both
    self-checks"""
    root = WROOT + "\\playtest"
    tr, save = scenario_inputs(a.scenario)
    sha = play_install(root)
    ssh_win(f"Remove-Item '{root}\\sessions\\*','{root}\\latest.sav' -Recurse -Force -ErrorAction SilentlyContinue; exit 0")
    up = root.replace(chr(92), '/')
    scp(tr, f"{WIN}:{up}/test-trace.jsonl")
    if save:
        tmp = os.path.join(BUILD, "playtest.sav")
        open(tmp, "wb").write(save_bytes(save))
        scp(tmp, f"{WIN}:{up}/latest.sav")
    job = f"{datetime.datetime.now():%Y%m%d-%H%M%S}-session-test"
    ps = (f"& '{root}\\play.ps1' -Root '{root}' -Bin '{sha}' -TestTrace '{root}\\test-trace.jsonl' "
          f"-TestArgs '--quiet --turbo' -TestFrames '{a.frames or 2500}'")
    loc = os.path.join(BUILD, "session-test.ps1")
    open(loc, "w").write(ps + "\r\n")
    scp(loc, f"{WIN}:{up}/session-test.ps1")
    t0 = time.time()
    print(win_job(job, f"{root}\\session-test.ps1", timeout=4 * 3600), flush=True)
    print(f"[session loop {time.time() - t0:.0f}s; waiting for the self-checks]", flush=True)
    t1 = time.time()
    while True:
        ps_ = win_pieces(root)
        if all(p.get("check") or not (p.get("meta") or {}).get("result") for p in ps_):
            break
        queued = ssh_win(f"@(Get-ChildItem {WROOT}\\queue -Filter '*-selfcheck-*.ps1').Count").strip()
        if queued == "0" and time.time() - t1 > 60:
            print("session-test: a piece has no check.json and no self-check job is queued or running"); break
        time.sleep(5)
    for p in ps_:
        print(piece_line(p))
        c = p.get("check") or {}
        if c.get("status") == "FAIL":
            print("    " + json.dumps(c)[:600])
    ok = ps_ and all((p.get("check") or {}).get("status") == "PASS" for p in ps_ if (p.get("meta") or {}).get("result"))
    print("session-test:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def fetch_save(remote_dir, files, base, dst_dir):
    """PIECE\\<base>[.gz] → a raw local file (None when the piece has neither)"""
    name = base + ".gz" if base + ".gz" in files else base if base in files else None
    if not name:
        return None
    loc = os.path.join(dst_dir, name)
    scp(f"{WIN}:{remote_dir}/{name}", loc)
    if name.endswith(".gz"):
        raw = loc[:-3]
        with gzip.open(loc, "rb") as i, open(raw, "wb") as o:
            shutil.copyfileobj(i, o)
        os.remove(loc)
        return raw
    return loc


def cmd_session_sync(a):
    """finished pieces → tests/playthrough/<session>/<NNN>/{trace.jsonl,meta.json}; start saves → _saves; a PASSed
    piece's end save → the end-save cache (golden / triage / record --from start from it, no replay)"""
    store = os.path.join(SCEN, "_saves")
    n_new = 0
    for p in win_pieces(a.root):
        if not p["done"] or (a.session and p["session"] != a.session):
            continue
        name = f"pt-{p['session']}-{p['piece']}"
        d = scen_dir(name)
        tr = os.path.join(d, "trace.jsonl")
        meta = dict(p["meta"])
        meta["check"] = p.get("check")
        if os.path.exists(tr) and not a.refresh:
            old = json.load(open(os.path.join(d, "meta.json")))
            if old.get("check") == meta["check"]:
                continue
        rdir = f"{a.root}\\sessions\\{p['session']}\\{p['piece']}".replace("\\", "/")
        tmp = os.path.join(LOCAL_RUNS, "session-sync", name)
        shutil.rmtree(tmp, ignore_errors=True)
        os.makedirs(tmp)
        scp(f"{WIN}:{rdir}/trace.jsonl", os.path.join(tmp, "trace.jsonl"))
        start = fetch_save(rdir, p["files"], "start.sav", tmp)
        end = fetch_save(rdir, p["files"], "save.dat", tmp)
        os.makedirs(d, exist_ok=True)
        head = []
        start_blob = None
        if start:
            _, start_blob = trace_save.store_save(start, store)
            start_blob = str(start_blob)
            head.append(json.dumps({"savefile": os.path.relpath(start_blob, d)}))
        else:
            head.append(json.dumps({"savefile": "@fresh"}))
        r, cut = meta.get("result") or {}, meta.get("cut") or {}
        head.append(f"# playthrough {p['session']} piece {p['piece']}: {r.get('reason', 'exit ' + str(meta.get('exit')))} "
                    f"f{r.get('frame', '?')}" + (f", preamble {meta['preamble']}" if meta.get("preamble") else "") +
                    (f", resume {cut['resume']}" if cut.get("resume") else ""))
        body = open(os.path.join(tmp, "trace.jsonl")).read()
        open(tr, "w").write("\n".join(head) + "\n" + body)
        json.dump(meta, open(os.path.join(d, "meta.json"), "w"), indent=1, sort_keys=True)
        cached = ""
        if end and (meta.get("check") or {}).get("status") == "PASS":
            blob = store_end_save(name, tr, start_blob, end, r.get("frame"))
            cached = " end save cached" if blob else " (no save commit)"
        print(piece_line(p) + cached)
        n_new += 1
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"session-sync: {n_new} piece(s) written under {os.path.relpath(PLAYTHROUGH, ROOT)}/")
    return 0


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
    p.add_argument("--target", choices=("win", "wine", "bunker"), default=os.environ.get("RHOST_TARGET", "win"),
                   help="bunker: headless only (run --headless / headless-check / snap-check / skip-check), tools/bunker/README.md")
    p.add_argument("--headless", action="store_true",
                   help="retail on rhost's fake devices (rh_dev.c): no GPU / desktop / audio; wine runs without Xvfb")
    p.add_argument("-j", "--jobs", type=int, help="parallel runs in a batch / gates rows (default: win 8, bunker 8, wine cores/2)")
    p.add_argument("--wine-timeout", type=int, default=14400, help="seconds per wine run (a long replay under contention takes hours)")
    p.add_argument("--no-golden", action="store_true", help="port commands: run retail afresh instead of its cached golden")


def cmd_tables(a):
    """retail only: per frame, the live type histograms of the effect / object tables — table A
    (DAT_069b2f80, +0x30), table B (DAT_069324b0, +0), table C (DAT_06956cb0, +0x28), the dungeon
    objects (DAT_0695ef78, +0x8c) and the in-use actors (DAT_0076b970, +0x424) — to pick what a
    render port must cover"""
    import collections
    name = a.scenario
    frames = [int(x) for x in a.at.split(",")]
    tr, save = scenario_inputs(name)
    a.dump_at = ",".join(str(f) for f in frames)
    if not a.frames:
        a.frames = max(frames) + 1
    batch, bdir, res, _ = run_batch(a, [(name, tr, None, save)], "tables",
                                    fetch=("rhost.log", "state_*.rhs"))
    tot = {k: collections.Counter() for k in "ABCOX"}
    for f in frames:
        path = os.path.join(bdir, name, f"state_f{f:06d}.rhs")
        if not os.path.exists(path):
            print(f"f{f}: no dump"); continue
        R = rhs.Dump(path)

        def w(va):
            for lo, hi in R.ranges:
                if lo <= va < hi:
                    return R.words[lo][(va - lo) // 4]
            return None

        def s32(v):
            return v - (1 << 32) if v is not None and v >= 0x80000000 else v
        na, nb, nc = w(0x76b960) or 0, w(0x76b964) or 0, w(0x76b968) or 0
        h = {k: collections.Counter() for k in "ABCOX"}
        for i in range(na):
            t = s32(w(0x69b2f80 + 0x94 * i + 0x30))
            if t != -1: h["A"][t] += 1
        for i in range(nb):
            t = s32(w(0x69324b0 + 0x124 * i))
            if t: h["B"][t] += 1
        for i in range(nc):
            t = s32(w(0x6956cb0 + 0x94 * i + 0x28))
            if t != -1: h["C"][t] += 1
        for i in range(512):
            t = s32(w(0x695ef78 + 0x2a0 * i + 0x8c))
            if t is not None and t != -1: h["O"][t] += 1
        for i in range(128):
            if w(0x76b970 + 0xba4 * i + 0x428):
                h["X"][s32(w(0x76b970 + 0xba4 * i + 0x424))] += 1
        line = "  ".join(f"{k}:{dict(sorted(v.items()))}" for k, v in h.items() if v)
        print(f"f{f}: {line or '(empty)'}")
        for k in h:
            tot[k].update(h[k])
    print("all: " + "  ".join(f"{k}:{dict(sorted(v.items()))}" for k, v in tot.items() if v))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("build").set_defaults(fn=cmd_build)
    sp.add_parser("stage").set_defaults(fn=cmd_stage)
    p = sp.add_parser("run"); p.add_argument("scenarios", nargs="*"); p.add_argument("--masks"); p.add_argument("--save"); common(p); p.set_defaults(fn=cmd_run)
    p = sp.add_parser("det"); p.add_argument("scenario"); p.add_argument("-n", type=int, default=2); p.add_argument("--calibrate", action="store_true"); common(p); p.set_defaults(fn=cmd_det)
    p = sp.add_parser("corpus"); p.add_argument("--only"); p.add_argument("--flatten", action="store_true"); common(p); p.set_defaults(fn=cmd_corpus)
    p = sp.add_parser("portsave"); p.add_argument("scenario"); common(p); p.set_defaults(fn=cmd_portsave)
    p = sp.add_parser("portcensus"); p.add_argument("scenario"); common(p); p.set_defaults(fn=cmd_portcensus)
    p = sp.add_parser("portframes"); p.add_argument("scenario"); p.add_argument("--at", type=int, required=True); common(p); p.set_defaults(fn=cmd_portframes)
    p = sp.add_parser("portanchors"); p.add_argument("scenario"); common(p); p.set_defaults(fn=cmd_portanchors)
    p = sp.add_parser("portstate"); p.add_argument("scenario"); p.add_argument("--va", required=True, help="hex VAs, e.g. 6023a0,438cc08")
    p.add_argument("--context", type=int, default=4)
    p.add_argument("--window", help="FROM:TO: compare only these frames")
    p.add_argument("--changes", action="store_true", help="list every frame where a VA changes on either side")
    p.add_argument("--floats", action="store_true", help="--changes: show the values as floats")
    common(p); p.set_defaults(fn=cmd_portstate)
    p = sp.add_parser("portrng"); p.add_argument("scenario"); p.add_argument("--window", required=True, help="FROM:TO frames (TO may be \"end\"; clamped to the retail run)")
    p.add_argument("--context", type=int, default=3)
    p.add_argument("--seq", action="store_true", help="list the first differing tick's draws in order, retail | port")
    common(p); p.set_defaults(fn=cmd_portrng)
    p = sp.add_parser("portblock"); p.add_argument("scenario"); p.add_argument("--at", required=True, help="frames F[,F..] (one run)")
    p.add_argument("--list", type=int, default=40, help="dwords listed per block"); common(p); p.set_defaults(fn=cmd_portblock)
    p = sp.add_parser("tables"); p.add_argument("scenario"); p.add_argument("--at", required=True, help="frames F,F,..")
    common(p); p.set_defaults(fn=cmd_tables)
    p = sp.add_parser("portshots"); p.add_argument("scenario"); p.add_argument("--at", required=True, help="frames F,F,.. or FROM:TO:STEP")
    p.add_argument("--port-d3d", action="store_true", help="also the port's D3D call trace of those frames (port/d3d.jsonl)")
    p.add_argument("--rank", action="store_true", help="print each frame's port|retail difference, worst first")
    common(p); p.set_defaults(fn=cmd_portshots)
    p = sp.add_parser("verify"); p.add_argument("--only"); common(p); p.set_defaults(fn=cmd_verify)
    p = sp.add_parser("gates"); p.add_argument("--only", help="regex over 'KIND SCENARIO'"); common(p); p.set_defaults(fn=cmd_gates)
    p = sp.add_parser("prune", help="delete Windows run dirs older than --hours (record runs kept)")
    p.add_argument("--hours", type=float, default=PRUNE_HOURS)
    p.set_defaults(fn=lambda a: print(prune_win_runs(a.hours)) or 0)
    p = sp.add_parser("flatten"); p.add_argument("scenarios", nargs="+"); common(p); p.set_defaults(fn=cmd_flatten)
    p = sp.add_parser("diff"); p.add_argument("a"); p.add_argument("b"); p.add_argument("--show", type=int, default=40); p.set_defaults(fn=cmd_diff)
    p = sp.add_parser("endsave"); p.add_argument("scenario")
    p.add_argument("--refresh", action="store_true", help="replay even when the trace's end save is cached")
    p.set_defaults(fn=lambda a: print(end_save(a.scenario, a.refresh)) or 0)
    p = sp.add_parser("record"); p.add_argument("name")
    p.add_argument("--save", help="SHA (unique prefix ok) | PATH | SCEN (the save scenario SCEN starts from)")
    p.add_argument("--from", dest="from_scen", metavar="SCEN", help="start from the save SCEN ends with (rhost replay → _saves)")
    p.add_argument("--force", action="store_true", help="overwrite an existing scenario")
    common(p); p.set_defaults(fn=cmd_record)
    p = sp.add_parser("headless-check", help="headless (fake-device) replays vs the Windows real-device goldens")
    p.add_argument("scenarios", nargs="*"); p.add_argument("--only")
    p.add_argument("--real", action="store_true", help="real devices instead: this rhost build vs the goldens (after a change to rhost's core)")
    common(p); p.set_defaults(fn=cmd_headless_check)
    p = sp.add_parser("skip-check", help="rhost --skip-fn gate: plain vs skipped headless replays must end in the same game state")
    p.add_argument("fns", help="hex VAs, comma-separated (e.g. 414ee2)"); p.add_argument("scenarios", nargs="*"); p.add_argument("--only")
    p.add_argument("--mask", help="LO:HI[,LO:HI] hex: render scratch the functions write, left out of state_sem in both runs")
    common(p); p.set_defaults(fn=cmd_skip_check)
    p = sp.add_parser("snap-check", help="savestate self-test (rhost --snap-test) per scenario: restore + replay must reproduce the state")
    p.add_argument("scenarios", nargs="*"); p.add_argument("--only"); p.add_argument("--passes", type=int, default=2); common(p); p.set_defaults(fn=cmd_snap_check)
    p = sp.add_parser("golden", help="make the retail goldens (cached retail side of the port commands)")
    p.add_argument("scenarios", nargs="*"); p.add_argument("--only"); p.add_argument("--refresh", action="store_true")
    common(p); p.set_defaults(fn=cmd_golden)
    p = sp.add_parser("triage", help="every scenario vs its golden under the current port: first splits, clustered")
    p.add_argument("scenarios", nargs="*"); p.add_argument("--only"); p.add_argument("--refresh", action="store_true")
    common(p); p.set_defaults(fn=cmd_triage)
    p = sp.add_parser("triage-one"); p.add_argument("scenario"); common(p); p.set_defaults(fn=cmd_triage_one)
    p = sp.add_parser("session-install", help="free-play capture: freeze this rhost build + the Desktop launcher")
    p.add_argument("--save", help="seed play\\latest.sav: SHA (prefix) | PATH | SCEN (the save SCEN starts from)")
    p.add_argument("--force", action="store_true", help="--save replaces an existing latest.sav")
    p.set_defaults(fn=cmd_session_install)
    p = sp.add_parser("session-status", help="the capture pieces on Windows")
    p.add_argument("--root", default=PLAY, help=argparse.SUPPRESS); p.set_defaults(fn=cmd_session_status)
    p = sp.add_parser("session-test", help="the capture loop end to end, headless (piece 1 = SCEN replayed with --cut)")
    p.add_argument("scenario"); p.add_argument("--frames", type=int, help="zero-input frames of the later pieces (2500)")
    p.set_defaults(fn=cmd_session_test)
    p = sp.add_parser("session-sync", help="pull finished capture pieces into tests/playthrough/")
    p.add_argument("--session"); p.add_argument("--refresh", action="store_true")
    p.add_argument("--root", default=PLAY, help=argparse.SUPPRESS)    # session-test's C:\\orc-rhost\\playtest
    p.set_defaults(fn=cmd_session_sync)
    p = sp.add_parser("restart-runner", help="restart the Windows queue runner (a staged runner.ps1 takes effect)")
    p.add_argument("--slots", type=int, help="also write queue\\slots.txt (parallel jobs)")
    p.add_argument("--force", action="store_true"); p.set_defaults(fn=cmd_restart_runner)
    a = ap.parse_args()
    if hasattr(a, "target") and getattr(a, "jobs", 0) is None:
        a.jobs = 8 if a.target in ("win", "bunker") else max(1, (os.cpu_count() or 4) // 2)
    r = a.fn(a)
    sys.exit(r or 0)


if __name__ == "__main__":
    main()
