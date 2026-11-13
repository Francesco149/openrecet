#!/usr/bin/env python3
"""Daily playable build: push master and dispatch the `nightly` workflow once a day.

Owner rule (2026-11-13): an up-to-date openrecet.exe goes out at least once a day.
After 13:00 owner time (Europe/Rome), if no nightly build has been STARTED since
13:00 today, push master (fast-forward only) and run .github/workflows/nightly.yml
(workflow_dispatch). The workflow builds, gates (no proprietary bytes, x87 math) and
clobbers the asset of the rolling `nightly` pre-release.

Called by tools/git-hooks/post-commit in the background after every commit, and at
session start (CLAUDE.md). Idempotent and cheap when there is nothing to do.

  python3 tools/ci/daily_build.py            # check, act if due
  python3 tools/ci/daily_build.py --dry-run  # say what it would do
  python3 tools/ci/daily_build.py --force    # ignore the clock / earlier builds

Rules:
  - a build started since today's cutoff (queued / running / succeeded) = done;
  - a FAILED build since the cutoff is retried only for a different master sha;
  - only a clean fast-forward push of `master`: never force, never another branch;
  - nothing happens before the cutoff.
Env: DAILY_BUILD_TZ (default Europe/Rome), DAILY_BUILD_HOUR (default 13).
"""
import argparse
import datetime as dt
import json
import os
import subprocess
import sys
from zoneinfo import ZoneInfo

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORKFLOW = "nightly.yml"


def sh(*args, check=True):
    r = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    if check and r.returncode != 0:
        raise SystemExit(f"daily-build: {' '.join(args)} failed: {r.stderr.strip() or r.stdout.strip()}")
    return r


def log(msg):
    stamp = dt.datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S %z")
    print(f"[{stamp}] daily-build: {msg}", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()

    tz = ZoneInfo(os.environ.get("DAILY_BUILD_TZ", "Europe/Rome"))
    hour = int(os.environ.get("DAILY_BUILD_HOUR", "13"))
    now = dt.datetime.now(tz)
    cutoff = now.replace(hour=hour, minute=0, second=0, microsecond=0)
    if now < cutoff and not a.force:
        log(f"before {hour:02d}:00 {tz.key} ({now:%H:%M}); nothing to do")
        return 0

    if sh("git", "rev-parse", "--abbrev-ref", "HEAD").stdout.strip() != "master":
        log("not on master; skipped")
        return 0
    head = sh("git", "rev-parse", "master").stdout.strip()

    runs = json.loads(sh("gh", "run", "list", "--workflow", WORKFLOW, "-L", "20",
                         "--json", "createdAt,headSha,status,conclusion,databaseId").stdout or "[]")
    for r in runs:
        created = dt.datetime.fromisoformat(r["createdAt"].replace("Z", "+00:00"))
        if created < cutoff or a.force:
            continue
        failed = r["status"] == "completed" and r["conclusion"] != "success"
        if not failed:
            log(f"build {r['databaseId']} ({r['headSha'][:8]}, {r['status']}"
                f"{'/' + r['conclusion'] if r['conclusion'] else ''}) started since {cutoff:%Y-%m-%d %H:%M}; done for today")
            return 0
        if r["headSha"] == head:
            log(f"build {r['databaseId']} of {head[:8]} failed since the cutoff; not retrying the same sha "
                f"(gh run view {r['databaseId']} --log-failed)")
            return 1

    sh("git", "fetch", "-q", "origin", "master")
    if sh("git", "merge-base", "--is-ancestor", "origin/master", "master", check=False).returncode != 0:
        log("origin/master is not an ancestor of master (diverged); not pushing — needs a human")
        return 1
    ahead = int(sh("git", "rev-list", "--count", "origin/master..master").stdout.strip())

    if a.dry_run:
        log(f"would push master ({ahead} commits ahead, {head[:8]}) and dispatch {WORKFLOW}")
        return 0
    if ahead:
        sh("git", "push", "origin", "master:master")
        log(f"pushed master ({ahead} commits, {head[:8]})")
    sh("gh", "workflow", "run", WORKFLOW, "--ref", "master")
    log(f"dispatched {WORKFLOW} for {head[:8]}; release: "
        "https://github.com/Francesco149/openrecet/releases/tag/nightly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
