#!/usr/bin/env python3
"""Runs INSIDE the bunker container (bwrap, see tools/bunker/README.md): every run dir of one batch under wine,
-j at a time, after admission (the pool's memory, shared with rd sessions), the way rhost.py's run_batch_wine does a headless batch: `wine C:\\orc-rhost\\bin\\<sha>\\rhost.exe
<argv.json>`, WINEDEBUG=-all, no DISPLAY, stdout → <run>/rhost.stdout; a run whose rhost.log has its RESULT line
gets 20 s to exit, then its process group is killed. Per run it writes <run>/bunker.json (wall, peak RSS).

    batch.py BATCH_DIR BIN_WIN -j N [--timeout S]
"""
import argparse
import json
import os
import signal
import subprocess
import sys
import threading
import time

p = argparse.ArgumentParser()
p.add_argument("bdir")
p.add_argument("exe_win")
p.add_argument("-j", type=int, default=1)
p.add_argument("--timeout", type=int, default=4 * 3600)
a = p.parse_args()

runs = sorted(d for d in os.listdir(a.bdir) if os.path.exists(os.path.join(a.bdir, d, "argv.json")))
sem = threading.Semaphore(a.j)
lock = threading.Lock()
starts = []


def pool_free_mb():
    """MB the pool (/pool = its cgroup, read-only) can still take, and the host's MemAvailable — pool.py pool_free()"""
    try:
        mx = int(open("/pool/memory.max").read().split()[0])
        st = dict(l.split() for l in open("/pool/memory.stat"))
        used = int(open("/pool/memory.current").read()) - (int(st["file"]) - int(st["shmem"]))
        host = next(int(l.split()[1]) << 10 for l in open("/proc/meminfo") if l.startswith("MemAvailable:"))
        return (mx - used) >> 20, host >> 20
    except (OSError, ValueError, KeyError):
        return 1 << 20, 1 << 20


def admit():
    """the pool is shared with rd sessions and other batches: start a run only with 600 MB free in the bound (+ 350 MB per
    run this batch started in the last 5 s: they have not grown yet) and 1 GB MemAvailable on the host; the lock is
    pool.py's (/orc/.admit.lock), so check + start is atomic across every bunker launcher"""
    import fcntl
    lk = open("/orc/.admit.lock", "a")
    while True:
        fcntl.flock(lk, fcntl.LOCK_EX)
        free, host = pool_free_mb()
        recent = sum(1 for t in starts if time.time() - t < 5)
        if free - 350 * recent >= 600 and host >= 1024:
            return lk
        fcntl.flock(lk, fcntl.LOCK_UN)
        time.sleep(1)


def tree_rss(sid):
    """summed VmRSS (kB) of the processes in session SID (wine loader + rhost.exe + its wine helpers)"""
    tot = 0
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            if os.getsid(int(pid)) != sid:
                continue
            for line in open(f"/proc/{pid}/status"):
                if line.startswith("VmRSS:"):
                    tot += int(line.split()[1])
        except (OSError, ValueError):
            pass
    return tot


def one(name):
    with sem:
        rd = os.path.join(a.bdir, name)
        argv = json.load(open(os.path.join(rd, "argv.json")))
        log = os.path.join(rd, "rhost.log")
        so = open(os.path.join(rd, "rhost.stdout"), "w")
        env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
        with lock:
            lk = admit()
            pr = subprocess.Popen(["wine", a.exe_win] + argv, stdout=so, stderr=subprocess.DEVNULL, env=env,
                                  start_new_session=True)
            starts.append(time.time())
            time.sleep(0.3)
            lk.close()
        t0 = time.time()
        done_at, peak, killed = None, 0, False
        while pr.poll() is None:
            time.sleep(1)
            peak = max(peak, tree_rss(pr.pid))
            if done_at is None and os.path.exists(log) and "\nRESULT " in open(log, errors="replace").read()[-4096:]:
                done_at = time.time()
            if (done_at and time.time() - done_at > 20) or time.time() - t0 > a.timeout:
                os.killpg(pr.pid, signal.SIGKILL)
                killed = True
                break
        so.close()
        json.dump({"wall_s": round(time.time() - t0, 2), "peak_rss_kb": peak, "killed": killed},
                  open(os.path.join(rd, "bunker.json"), "w"))
        with lock:
            print(f"[bunker] {name} done in {time.time() - t0:.1f}s, peak rss {peak // 1024} MB", flush=True)


ts = [threading.Thread(target=one, args=(n,)) for n in runs]
for t in ts:
    t.start()
for t in ts:
    t.join()
subprocess.run(["wineserver", "-k"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
sys.exit(0)
