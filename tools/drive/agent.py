#!/usr/bin/env python3
"""agent.py — an API-billed driver: Claude (Messages API + tool use) works a drive task with `rd`.

The subscription path needs no harness: a Claude Code session hands DRIVER.md + a task to a
subagent. This file is for API credit (expiring credit, or more drivers than a session should
spawn): one process per task, a hard dollar budget, a log of every turn.

    tools/drive/agent.sh tasks/NAME.md --run NAME [--budget 6] [--model claude-opus-5-5] [--effort high]

The model gets five tools and nothing else — no shell:
    rd(args)            ./rd <args>, restricted to its own run (and read-only on others)
    read(path, ..)      text files under the repo's docs / tools / src and the IDA export
    grep(pattern, path) ugrep -n -E in the same places
    view(path)          a PNG `rd look / strip` wrote, returned as an image
    finish(report)      ends the run; the report goes to <agent dir>/report.md

A manual loop (not the SDK tool runner) because each turn's cost is added up here and the run
stops on the budget, and tool results carry images. Spend = `usage` at list prices; it persists
in <agent dir>/harness.json, so a restarted run continues its budget. The key comes from
ANTHROPIC_API_KEY, else ~/claude-platform.txt.

Exit codes: 0 finished, 2 budget reached, 3 gave up / refused, 4 the API account has no credit left.
"""
import argparse
import base64
import io
import json
import os
import re
import shlex
import subprocess
import sys
import time

import anthropic

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
AGENTS = os.environ.get("RD_AGENTS", "/mnt/ephemeral/openrecet/drive/agents")
RUNS = os.environ.get("RD_RUNS", "/mnt/ephemeral/openrecet/drive/runs")
IDA = "/mnt/ephemeral/openrecet/decompiled-ida"
READ_ROOTS = [os.path.join(ROOT, d) for d in ("docs", "tools/drive", "tools/rhost", "tools/save_arena_map.json", "src",
                                               "tests/scenarios/README.md", "CLAUDE.md")] + [IDA, RUNS]

# $ per million tokens: input, output, cache read (cache writes bill 1.25x input)
PRICES = {"claude-opus-5-5": (4.0, 20.0, 0.20), "claude-sonnet-5-5": (2.0, 10.0, 0.10), "claude-haiku-5-5": (0.10, 0.50, 0.01),
          "claude-fable-5-2": (8.0, 40.0, 0.20)}
NO_FALLBACK = {"claude-haiku-5-5"}          # refusals there have no server-side fallback

TOOLS = [
    {"name": "rd", "description": "Run the rd driver CLI: the string is everything after `rd` (e.g. `play myrun \"A .*8\" --until \"dlg == 0\"`). "
     "State-changing commands work only on your own run. `file` and `port` are not available: report instead.",
     "input_schema": {"type": "object", "properties": {"args": {"type": "string"}}, "required": ["args"], "additionalProperties": False}, "strict": True},
    {"name": "read", "description": "Read a text file (repo docs/, tools/drive/, tools/rhost/, src/, the IDA export all_ida.c / functions_ida.tsv, run dirs). "
     "offset = first line (1-based), limit = number of lines (max 400).",
     "input_schema": {"type": "object", "properties": {"path": {"type": "string"}, "offset": {"type": "integer"}, "limit": {"type": "integer"}},
                      "required": ["path", "offset", "limit"], "additionalProperties": False}, "strict": True},
    {"name": "grep", "description": "Search files with an extended regex (grep -n -E, recursive on a directory); up to `max` matching lines (max 200).",
     "input_schema": {"type": "object", "properties": {"pattern": {"type": "string"}, "path": {"type": "string"}, "max": {"type": "integer"}},
                      "required": ["pattern", "path", "max"], "additionalProperties": False}, "strict": True},
    {"name": "view", "description": "Look at a PNG that `rd look` or `rd strip` wrote (the path it printed).",
     "input_schema": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"], "additionalProperties": False}, "strict": True},
    {"name": "finish", "description": "End the run. report = what the card asks for: run name, frames, each step's frame, state you named, game facts, tool gaps.",
     "input_schema": {"type": "object", "properties": {"report": {"type": "string"}}, "required": ["report"], "additionalProperties": False}, "strict": True},
]


def allowed(path):
    p = os.path.realpath(path if os.path.isabs(path) else os.path.join(ROOT, path))
    return p if any(p == r or p.startswith(os.path.realpath(r) + os.sep) for r in READ_ROOTS) else None


def tool_rd(args, run):
    try:
        argv = shlex.split(args)
    except ValueError as e:
        return f"cannot parse: {e}", True
    if not argv:
        return "empty command", True
    cmd = argv[0]
    if cmd in ("file", "port"):
        return f"`rd {cmd}` is the orchestrator's: say in your report that the run is ready to file", True
    read_only = cmd in ("status", "text", "eval", "ls", "lint") or (cmd == "ram" and len(argv) > 2 and argv[2] in ("list", "read", "status")) or \
        (cmd == "ck" and len(argv) > 2 and argv[2] == "list")
    if cmd not in ("ls", "lint") and (len(argv) < 2 or (argv[1] != run and not read_only)):
        return f"your run is {run!r}: `rd {cmd} {run} ...`", True
    if cmd == "new" and "--force" not in argv and os.path.exists(os.path.join(RUNS, run, "meta.json")):
        return f"run {run} exists (add --force to start it over)", True
    try:
        r = subprocess.run([sys.executable.replace("/venv/bin/python", "/venv/bin/python"), os.path.join(HERE, "drive.py")] + argv, cwd=ROOT,
                           capture_output=True, text=True, timeout=900, env=dict(os.environ, PYTHONUNBUFFERED="1"))
    except subprocess.TimeoutExpired:
        return "rd did not return within 900 s", True
    out = (r.stdout + ("\n" + r.stderr if r.stderr.strip() else "")).strip()
    out = "\n".join(l for l in out.splitlines() if not re.search(r"post-quantum|store now, decrypt later|may need to be upgraded", l))
    if len(out) > 7000:
        out = out[:3500] + f"\n… [{len(out) - 7000} characters cut] …\n" + out[-3500:]
    return out or f"(no output, exit {r.returncode})", r.returncode not in (0, 1)


def tool_read(path, offset, limit):
    p = allowed(path)
    if not p or not os.path.isfile(p):
        return f"not readable here: {path}", True
    limit = max(1, min(int(limit or 200), 400))
    offset = max(1, int(offset or 1))
    out = []
    with open(p, errors="replace") as f:
        for i, line in enumerate(f, 1):
            if i >= offset + limit:
                out.append(f"… (more: offset {i})")
                break
            if i >= offset:
                out.append(f"{i}\t{line.rstrip()[:400]}")
    return "\n".join(out) or "(empty range)", False


def tool_grep(pattern, path, mx):
    p = allowed(path)
    if not p:
        return f"not searchable here: {path}", True
    mx = max(1, min(int(mx or 60), 200))
    try:
        r = subprocess.run(["grep", "-n", "-E", "-r", "-I", "--", pattern, p], capture_output=True, text=True, timeout=120, errors="replace")
    except subprocess.TimeoutExpired:
        return "grep timed out (narrow the path)", True
    lines = r.stdout.splitlines()
    out = "\n".join(l[:300].replace(ROOT + "/", "") for l in lines[:mx])
    return (out + (f"\n… {len(lines) - mx} more matches" if len(lines) > mx else "")) or "no matches", False


def tool_view(path):
    p = allowed(path)
    if not p or not p.lower().endswith(".png") or not os.path.isfile(p):
        return [{"type": "text", "text": f"no such picture: {path}"}], True
    from PIL import Image
    im = Image.open(p).convert("RGB")
    if im.width > 1568:
        im = im.resize((1568, int(im.height * 1568 / im.width)), Image.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return [{"type": "image", "source": {"type": "base64", "media_type": "image/png", "data": base64.standard_b64encode(buf.getvalue()).decode()}},
            {"type": "text", "text": f"{os.path.basename(p)} ({im.width}x{im.height})"}], False


def cost(model, u):
    pin, pout, pread = PRICES[model]
    return ((u.input_tokens or 0) * pin + (getattr(u, "cache_creation_input_tokens", 0) or 0) * pin * 1.25 +
            (getattr(u, "cache_read_input_tokens", 0) or 0) * pread + (u.output_tokens or 0) * pout) / 1e6


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("task"); ap.add_argument("--run", required=True)
    ap.add_argument("--budget", type=float, default=6.0, help="dollars for this agent dir, cumulative")
    ap.add_argument("--model", default="claude-opus-5-5", choices=sorted(PRICES))
    ap.add_argument("--effort", default="high", choices=("low", "medium", "high", "xhigh", "max"))
    ap.add_argument("--max-turns", type=int, default=400)
    a = ap.parse_args()
    adir = os.path.join(AGENTS, a.run)
    os.makedirs(adir, exist_ok=True)
    hp = os.path.join(adir, "harness.json")
    H = json.load(open(hp)) if os.path.exists(hp) else {"spend": 0.0, "turns": 0, "model": a.model}
    if H["spend"] >= a.budget:
        print(f"budget already used: ${H['spend']:.2f} of ${a.budget:.2f}")
        return 2
    key = os.environ.get("ANTHROPIC_API_KEY") or open(os.path.expanduser("~/claude-platform.txt")).read().strip()
    client = anthropic.Anthropic(api_key=key, max_retries=4)
    task = open(a.task).read()
    system = [{"type": "text", "text":
               "You are a driver agent for the OpenRecet project: you author a verified input trace of retail Recettear with the `rd` tool. "
               "Work autonomously to the end; nobody answers questions. Your tools are rd, read, grep, view, finish — there is no shell. "
               f"Your run name is `{a.run}` (create it with `rd new {a.run} --from <the task's start> --force`). "
               f"You have about ${a.budget - H['spend']:.0f} of API budget: prefer few, dense steps; pictures cost a replay, state is free. "
               "When the task is done (or you are certain it cannot be done), call finish with the report the card describes.\n\n"
               "=== DRIVER CARD (tools/drive/DRIVER.md) ===\n" + open(os.path.join(HERE, "DRIVER.md")).read()}]
    messages = [{"role": "user", "content": f"Task file {os.path.relpath(os.path.abspath(a.task), ROOT)}:\n\n{task}"}]
    log = open(os.path.join(adir, "log.jsonl"), "a")
    idle, t0 = 0, time.time()
    kw = dict(model=a.model, max_tokens=16000, system=system, tools=TOOLS, thinking={"type": "adaptive"},
              output_config={"effort": a.effort}, cache_control={"type": "ephemeral"})
    betas = []
    if a.model not in NO_FALLBACK:            # a safety decline re-runs on Anthropic's recommended model instead of ending the run
        betas, kw["fallbacks"] = ["server-side-fallback-2026-07-01"], "default"
    for turn in range(a.max_turns):
        try:
            resp = client.beta.messages.create(messages=messages, betas=betas, **kw) if betas else client.messages.create(messages=messages, **kw)
        except anthropic.BadRequestError as e:
            if "credit balance" in str(e).lower():
                print(f"API credit exhausted (after ${H['spend']:.2f} here): {e.message}")
                return 4
            print(f"bad request: {e.message}")
            return 3
        except anthropic.RateLimitError as e:
            wait = int(e.response.headers.get("retry-after", "30"))
            print(f"rate limited: waiting {wait}s"); time.sleep(wait)
            continue
        except anthropic.APIStatusError as e:
            if "credit" in str(e).lower() or e.status_code == 402:
                print(f"API credit exhausted (after ${H['spend']:.2f} here): {e.message}")
                return 4
            print(f"API error {e.status_code}: {e.message}")
            if e.status_code >= 500:
                time.sleep(20)
                continue
            return 3
        except anthropic.APIConnectionError:
            print("network error: retrying in 20s"); time.sleep(20)
            continue
        c = cost(a.model, resp.usage)
        H["spend"] += c; H["turns"] += 1
        json.dump(H, open(hp, "w"))
        if resp.stop_reason == "refusal":
            print(f"the model declined ({getattr(resp, 'stop_details', None)}): stopping")
            return 3
        messages.append({"role": "assistant", "content": resp.content})
        results, done, calls = [], None, []
        for b in resp.content:
            if b.type != "tool_use":
                continue
            i = b.input if isinstance(b.input, dict) else {}
            try:
                if b.name == "rd":
                    out, err = tool_rd(str(i.get("args", "")), a.run)
                elif b.name == "read":
                    out, err = tool_read(str(i.get("path", "")), i.get("offset", 1), i.get("limit", 200))
                elif b.name == "grep":
                    out, err = tool_grep(str(i.get("pattern", "")), str(i.get("path", "")), i.get("max", 60))
                elif b.name == "view":
                    out, err = tool_view(str(i.get("path", "")))
                elif b.name == "finish":
                    done, out, err = str(i.get("report", "")), "recorded", False
                else:
                    out, err = f"unknown tool {b.name}", True
            except Exception as e:      # noqa: BLE001 — a tool failure is the model's information, not the harness's end
                out, err = f"tool failed: {type(e).__name__}: {e}", True
            calls.append({"tool": b.name, "input": i, "error": err, "output": out if isinstance(out, str) else "[image]"})
            results.append({"type": "tool_result", "tool_use_id": b.id, "content": out, "is_error": err})
        text = " ".join(b.text for b in resp.content if b.type == "text").strip()
        log.write(json.dumps({"turn": H["turns"], "t": round(time.time() - t0), "cost": round(c, 4), "spend": round(H["spend"], 3),
                              "stop": resp.stop_reason, "text": text[:2000], "calls": calls}) + "\n")
        log.flush()
        print(f"[{H['turns']:3d}] ${H['spend']:.2f}  " + "; ".join(f"{x['tool']}({str(x['input'].get('args', x['input'].get('path', '')))[:70]})" for x in calls)
              + (f"  {text[:90]!r}" if text and not calls else ""), flush=True)
        if done is not None:
            open(os.path.join(adir, "report.md"), "w").write(done + f"\n\n---\nmodel {a.model}, effort {a.effort}, {H['turns']} turns, ${H['spend']:.2f}, "
                                                                    f"{(time.time() - t0) / 60:.0f} min\n")
            print(f"finished: {adir}/report.md  ({H['turns']} turns, ${H['spend']:.2f})")
            return 0
        if results:
            idle = 0
            if H["spend"] >= a.budget:
                results.append({"type": "text", "text": "The budget is used up. Call finish now with the report of where the run stands."})
            messages.append({"role": "user", "content": results})
        else:
            idle += 1
            if idle >= 2 or resp.stop_reason not in ("end_turn", "max_tokens"):
                print("no tool call twice: stopping")
                return 3
            messages.append({"role": "user", "content": "Nobody reads your text. Continue with the tools, or call finish with the report."})
        if H["spend"] >= a.budget * 1.05:
            print(f"budget reached: ${H['spend']:.2f}")
            return 2
    print("turn limit reached")
    return 3


if __name__ == "__main__":
    sys.exit(main())
