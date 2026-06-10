"""edits/lint.py — preflight validation of a working trace + canonical auto-pin.

Codifies the pin-placement rules that used to live as CLAUDE.md prose (standing
policy 2026-06-09: every trace we work on is phase+RNG-pinned AND call-traced up
front, with the pins in the segment that OPENS the {caprange}). Template:
tests/scenarios/house-loaded-display-pinned/trace.jsonl —

    {"wait": "LOADING_END"}
    {"phasepin": 80}              # ≤ caprange.start; earlier = settle margin
    {"rngseed": [80, 19937]}      # SAME frame as the phasepin, canonical seed
    {"caprange": [120, 48]}
    {"calltrace": [120, 48]}      # spans the caprange

`lint()` returns findings (level error|warn|info, code, msg). `auto_pin_text()`
inserts/canonicalizes the pin block when the caprange segment lacks it — the
capture-time default that turns the policy into mechanism. Both are pure
text/op-level functions (host-testable, no engine/driver deps).

One rule reaches outside the text: `loads-without-tutloadpin` (INFO) replays
the {wait} chain against the session's PREVIOUS anchors (anchors.port.jsonl
next to the trace, when present) to resolve the capture window to absolute
frames, then flags LOADING_START firings strictly inside it when the trace
has no {tutloadpin} — a tutorial-dialogue bracket's length is worker-thread
wall-time (engine-quirks §119), so an unpinned crossing shifts the post-seam
label axis by the run-dependent bracket-length difference. Canonical fix:
{"tutloadpin": 8} (trace-global, next to {capstride}-style ops).
"""
from __future__ import annotations

import json
from pathlib import Path

CANON_SEED = 19937          # the bg-NPC-warmup canonical seed convention
SETTLE_HINT = 48            # companion spring-lerp re-converges over ~48 frames
FAR_PIN = 600               # a pin this far before the window smells mis-numbered
CANON_TUTLOADPIN = 8        # ≥ any plausible real tutorial bracket (observed 2-5f)


def _parse(lines: list[str]) -> list[tuple[int, dict | None]]:
    out: list[tuple[int, dict | None]] = []
    for i, ln in enumerate(lines):
        s = ln.strip()
        o = None
        if s and not s.startswith("#"):
            try:
                o = json.loads(s)
                if not isinstance(o, dict):
                    o = None
            except json.JSONDecodeError:
                o = None
        out.append((i, o))
    return out


def caprange_segment(lines: list[str]) -> dict | None:
    """The segment that OPENS the capture window: {wait} line index (−1 = FLAT/boot
    segment), the first {caprange} line index + value, the segment's end (next
    {wait} after the caprange, or EOF), and the pin ops found inside the segment."""
    parsed = _parse(lines)
    last_wait = -1
    seg: dict | None = None
    for i, o in parsed:
        if o is None:
            continue
        if "wait" in o:
            if seg is not None:
                seg["end"] = i
                break
            last_wait = i
        elif "caprange" in o and seg is None:
            cr = o["caprange"]
            seg = {"wait_idx": last_wait, "cr_idx": i,
                   "cr": (int(cr[0]), int(cr[1])), "end": len(lines)}
    if seg is None:
        return None
    lo, hi = seg["wait_idx"], seg["end"]
    seg["phasepins"] = [(i, int(o["phasepin"])) for i, o in parsed
                        if o and "phasepin" in o and lo < i < hi]
    seg["rngseeds"] = [(i, int(o["rngseed"][0]), int(o["rngseed"][1]))
                       for i, o in parsed
                       if o and "rngseed" in o and lo < i < hi]
    seg["calltraces"] = []
    for i, o in parsed:
        if o and "calltrace" in o and lo < i < hi:
            ct = o["calltrace"]
            seg["calltraces"].append(
                (i, (int(ct[0]), int(ct[1])) if isinstance(ct, list)
                 else (int(ct), 0)))
    return seg


def _window_loads_from_anchors(lines: list[str], seg: dict,
                               anchors_path: Path) -> list[int] | None:
    """LOADING_START fire-frames strictly inside the caprange window, resolved
    to ABSOLUTE frames by replaying the trace's {wait} chain over a previous
    capture's anchor stream (input_segtrace semantics: a wait resolves at the
    first stream firing with frame >= segment entry, strictly > for a repeated
    same-name anchor). None when the stream can't resolve the chain (stale
    anchors / renamed waits) — the caller stays silent then."""
    try:
        anchors = []
        for raw in anchors_path.read_text().splitlines():
            s = raw.strip()
            if not s:
                continue
            o = json.loads(s)
            anchors.append((str(o["anchor"]), int(o["frame"])))
    except (OSError, ValueError, KeyError):
        return None
    parsed = _parse(lines)
    waits = [str(o["wait"]) for i, o in parsed
             if o and "wait" in o and i <= seg["wait_idx"]]
    base, base_anchor = 0, None
    for w in waits:
        nxt = None
        for name, frame in anchors:
            if name != w:
                continue
            if (frame > base) if w == base_anchor else (frame >= base):
                nxt = frame
                break
        if nxt is None:
            return None
        base, base_anchor = nxt, w
    cr_start, cr_count = seg["cr"]
    lo, hi = base + cr_start, base + cr_start + cr_count
    return [f for name, f in anchors
            if name == "LOADING_START" and lo < f < hi]


def lint(text: str, trace_dir: Path | None = None) -> list[dict]:
    """Validate a working trace. Returns [{level, code, msg}] — empty = clean."""
    f: list[dict] = []

    def add(level: str, code: str, msg: str) -> None:
        f.append({"level": level, "code": code, "msg": msg})

    lines = text.splitlines()
    parsed = _parse(lines)
    ops = [o for _, o in parsed if o]

    n_cr = sum(1 for o in ops if "caprange" in o)
    if n_cr == 0:
        add("error", "no-caprange", "trace has no {caprange} — nothing to capture")
        return f
    if n_cr > 1:
        add("warn", "multi-caprange",
            f"{n_cr} {{caprange}} ops — only the FIRST is honored")

    seg = caprange_segment(lines)
    assert seg is not None
    cr_start, cr_count = seg["cr"]

    # ── savefile resolvable ────────────────────────────────────────────────
    for o in ops:
        ref = o.get("savefile")
        if not ref:
            continue
        if str(ref).startswith("@"):
            continue                                  # @fresh — no blob
        if trace_dir is not None and not (Path(trace_dir) / ref).exists():
            add("error", "savefile-missing",
                f"{{savefile}} ref does not resolve next to the trace: {ref}")

    # ── phase pin ──────────────────────────────────────────────────────────
    pins = seg["phasepins"]
    if not pins:
        add("warn", "no-phasepin",
            f"caprange segment has no {{phasepin}} — diff will carry load-phase "
            f"noise (policy: pin every trace; canonical: {{\"phasepin\": "
            f"{cr_start}}} before the caprange)")
    else:
        if len(pins) > 1:
            add("warn", "multi-phasepin",
                f"{len(pins)} {{phasepin}} ops in the caprange segment")
        for _, pf in pins:
            if pf > cr_start:
                add("error", "pin-inside-window",
                    f"{{phasepin: {pf}}} fires AFTER the window opens at "
                    f"{cr_start} — frames before it capture unpinned")
            elif cr_start - pf > FAR_PIN:
                add("warn", "pin-far-before-window",
                    f"{{phasepin: {pf}}} is {cr_start - pf} frames before the "
                    f"window — check it's numbered against the right anchor")
            elif pf == cr_start:
                add("info", "no-settle-margin",
                    f"phasepin at the window start — pin ~{SETTLE_HINT}f earlier "
                    f"when the scene allows (companion spring-lerp settle)")

    # ── RNG seed ───────────────────────────────────────────────────────────
    seeds = seg["rngseeds"]
    pin_frame = pins[0][1] if pins else cr_start
    by_frame: dict[int, list[int]] = {}
    for _, sf, sv in seeds:
        by_frame.setdefault(sf, []).append(sv)
    for sf, vals in by_frame.items():
        if len(vals) > 1:
            add("error", "stacked-rngseed",
                f"{len(vals)} {{rngseed}} ops at frame {sf} — replace the recorded "
                f"seed with ONE canonical {{\"rngseed\": [{sf}, {CANON_SEED}]}}")
    if not seeds:
        add("warn", "no-rngseed",
            f"caprange segment has no {{rngseed}} — RNG-driven content (sparkle/"
            f"dust/bg-NPC respawn) will desync (canonical: {{\"rngseed\": "
            f"[{pin_frame}, {CANON_SEED}]}} at the phasepin frame)")
    else:
        at_pin = by_frame.get(pin_frame, [])
        if not at_pin:
            add("warn", "rngseed-not-at-pin",
                f"no {{rngseed}} at the pin frame {pin_frame} (seeds at "
                f"{sorted(by_frame)}) — the canonical pattern seeds at the "
                f"phasepin frame")
        elif any(v != CANON_SEED for v in at_pin):
            add("info", "noncanonical-seed",
                f"{{rngseed}} at {pin_frame} uses seed {at_pin[0]} (canonical is "
                f"{CANON_SEED}; recorded retail seeds don't transfer cross-target)")

    # ── call trace ─────────────────────────────────────────────────────────
    cts = seg["calltraces"]
    if not cts:
        add("warn", "no-calltrace",
            f"caprange segment has no {{calltrace}} — keep the flow-trace on every "
            f"working trace (policy 2026-06-09; canonical: {{\"calltrace\": "
            f"[{cr_start}, {cr_count}]}})")
    else:
        for _, (cs, cc) in cts:
            if cc and (cs > cr_start or cs + cc < cr_start + cr_count):
                add("warn", "calltrace-span",
                    f"{{calltrace: [{cs},{cc}]}} does not span the caprange "
                    f"[{cr_start},{cr_count}]")

    # ── tutorial-load-bracket pin ──────────────────────────────────────────
    # Needs a previous capture's anchors next to the trace (a session being
    # re-captured); silent on a fresh build — the crossing only becomes
    # knowable once anchors exist.
    if (trace_dir is not None
            and not any("tutloadpin" in o for o in ops)):
        apath = Path(trace_dir) / "anchors.port.jsonl"
        if not apath.exists():
            apath = Path(trace_dir) / "anchors.retail.jsonl"
        if apath.exists():
            crossing = _window_loads_from_anchors(lines, seg, apath)
            if crossing:
                add("info", "loads-without-tutloadpin",
                    f"capture window crosses {len(crossing)} LOADING bracket(s) "
                    f"(at abs {crossing} per {apath.name}) with no "
                    f"{{tutloadpin}} — a tutorial-dialogue bracket's length is "
                    f"worker-thread wall-time (quirk §119), shifting the post-"
                    f"seam label axis run-to-run; add {{\"tutloadpin\": "
                    f"{CANON_TUTLOADPIN}}} (trace-global) to pin BOTH sides")
    return f


def auto_pin_text(text: str, cr: tuple[int, int] | None = None,
                  pin_frame: int | None = None) -> tuple[str, list[str]]:
    """Insert/canonicalize the pin block in the caprange-opening segment.

    - no {phasepin} in the segment → insert {"phasepin": F} right before the
      {caprange} line (F = pin_frame or caprange.start)
    - a {rngseed} already AT the pin frame → rewrite its seed to the canonical
      19937 (recorded retail seeds don't transfer cross-target; never stack)
    - no {rngseed} at the pin frame → insert {"rngseed": [F, 19937]} after the pin
    Existing pins at other frames are left alone. Returns (new_text, actions)."""
    lines = text.splitlines()
    seg = caprange_segment(lines)
    if seg is None:
        return text, []
    cr_start = (cr or seg["cr"])[0]
    actions: list[str] = []

    pins = seg["phasepins"]
    pf = pins[0][1] if pins else (pin_frame if pin_frame is not None else cr_start)

    # canonicalize a same-frame rngseed (replace, never stack)
    seed_at_pin = False
    for li, sf, sv in seg["rngseeds"]:
        if sf == pf:
            seed_at_pin = True
            if sv != CANON_SEED:
                lines[li] = json.dumps({"rngseed": [pf, CANON_SEED]})
                actions.append(f"canonicalized {{rngseed}} at {pf}: "
                               f"{sv} → {CANON_SEED}")

    ins: list[str] = []
    if not pins:
        ins.append(json.dumps({"phasepin": pf}))
        actions.append(f"added {{phasepin: {pf}}}")
    if not seed_at_pin:
        ins.append(json.dumps({"rngseed": [pf, CANON_SEED]}))
        actions.append(f"added {{rngseed: [{pf}, {CANON_SEED}]}}")
    if ins:
        at = seg["cr_idx"]               # pins sit immediately before the caprange
        lines[at:at] = ins
    return ("\n".join(lines) + "\n"), actions
