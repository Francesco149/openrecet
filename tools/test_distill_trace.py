#!/usr/bin/env python3
"""
tools/test_distill_trace.py — regression guard for the anchor-segment
drop-fragile fold (findings/cutscene-replay-anchor-drift.md).

The former hand-rolled drop_fragile.py (a one-off surgical edit on the
house-firstcust-cutscene-day2 trace) is now a first-class distill feature:
`--drop-fragile-after FRAME` / `--drop-fragile-region LO:HI`.  In an AUTO-PLAY
cutscene region, cosmetic/FX anchors (blinks, sprite fades, text-anim, LOADING)
DRIFT in relative order under turbo and deadlock the replay when waited on, so
they must NOT be emitted as {wait} sync points there — only the reliable scene
boundaries survive.

This pins:
  1. Without a drop region, every distinct-frame anchor is a {wait} (unchanged).
  2. With --drop-fragile-after, FRAGILE anchors past the boundary are dropped as
     syncs while RELIABLE boundaries (CONV_POSE_START/END, PAUSE_*, HOUSE_FREEROAM)
     survive — and the INTERACTIVE region before the boundary is UNTOUCHED.
  3. Dropping a fragile sync does not lose the inputs in its window: the following
     kept segment's window spans it, so change-points are preserved (rebased).
  4. --drop-fragile-region drops only inside the bounded span.

Run: nix develop --command python3 tools/test_distill_trace.py
Exits non-zero on failure; prints OK on success.
"""
from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


D = load("distill_trace")


def synth():
    """A tiny synthetic recording: an INTERACTIVE region (dense taps, frames
    0-100) then an AUTO-PLAY region (held button, fragile anchors 200-500).

    Reliable + fragile anchors are interleaved on both sides of frame 150."""
    changes = [(0, "0x0000"), (10, "0x0010"), (14, "0x0000"),
               (40, "0x0010"), (44, "0x0000"), (70, "0x0010"), (74, "0x0000"),
               # auto-play: hold 0x0020 from 200 through 480 (no taps)
               (200, "0x0020"), (480, "0x0000")]
    total = 500
    anchors = [
        {"name": "BOOT", "frame": 0, "gframe": 0, "rng": 1},
        # interactive region (< 150): a blink (fragile) + a PAUSE (reliable)
        {"name": "CONV_POSE_BLINK", "frame": 30, "gframe": 30, "rng": 11},
        {"name": "PAUSE_OPEN", "frame": 60, "gframe": 60, "rng": 12},
        {"name": "PAUSE_CLOSE", "frame": 90, "gframe": 90, "rng": 13},
        # auto-play region (>= 150): reliable boundary + a run of fragiles
        {"name": "CONV_POSE_START", "frame": 210, "gframe": 210, "rng": 20},
        {"name": "TEXT_ANIM_START", "frame": 250, "gframe": 250, "rng": 21},
        {"name": "CONV_POSE_BLINK", "frame": 300, "gframe": 300, "rng": 22},
        {"name": "EXTRA_SPRITE_START", "frame": 340, "gframe": 340, "rng": 23},
        {"name": "LOADING_START", "frame": 380, "gframe": 380, "rng": 24},
        {"name": "LOADING_END", "frame": 400, "gframe": 400, "rng": 25},
        {"name": "CONV_POSE_END", "frame": 470, "gframe": 470, "rng": 26},
    ]
    return changes, [], [], [], total, 4259672399, anchors


def waits(text: str) -> list[str]:
    out = []
    for ln in text.splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        o = json.loads(s)
        if "wait" in o:
            out.append(o["wait"])
    return out


def input_frames_after(text: str, wait_name: str) -> list[tuple[int, str]]:
    """(frame, buttons) change-point rows that appear anywhere in `text`."""
    out = []
    for ln in text.splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        o = json.loads(s)
        if "buttons" in o and "frame" in o and o["frame"] != 0:
            out.append((o["frame"], o["buttons"]))
    return out


def main() -> int:
    changes, caps, escs, cts, total, seed, anchors = synth()

    # (1) baseline — no drop region: every distinct-frame anchor is a {wait}.
    base = D.emit_anchor_segments(changes, caps, escs, cts, total, anchors, seed)
    base_waits = waits(base)
    # BOOT dropped; all others (10 distinct-frame anchors) kept.
    expect_all = ["CONV_POSE_BLINK", "PAUSE_OPEN", "PAUSE_CLOSE", "CONV_POSE_START",
                  "TEXT_ANIM_START", "CONV_POSE_BLINK", "EXTRA_SPRITE_START",
                  "LOADING_START", "LOADING_END", "CONV_POSE_END"]
    assert base_waits == expect_all, f"(1) baseline waits: {base_waits}"

    # (2) drop-fragile-after 150: interactive region intact; auto-play fragiles gone,
    #     reliable boundaries survive.
    dropped = D.emit_anchor_segments(changes, caps, escs, cts, total, anchors, seed,
                                     drop_regions=[(150, total + 1)])
    dw = waits(dropped)
    expect_dropped = ["CONV_POSE_BLINK", "PAUSE_OPEN", "PAUSE_CLOSE",  # < 150 untouched
                      "CONV_POSE_START", "CONV_POSE_END"]              # reliable only
    assert dw == expect_dropped, f"(2) drop-after waits: {dw}"
    # the pre-boundary blink at frame 30 (fragile) MUST survive
    assert dw[0] == "CONV_POSE_BLINK", "(2) interactive-region fragile was dropped!"

    # (3) dropping fragile syncs must NOT lose inputs: the held 0x0020 press at 200
    #     and its release at 480 are still present (rebased into a surviving segment).
    masks = {m for _, m in input_frames_after(dropped, "")}
    assert "0x0020" in masks, "(3) auto-play hold press lost after dropping fragiles"

    # (4) bounded region: drop only inside [340, 410] → the LOADING pair + sprite
    #     inside go, but the blink at 300 and text-anim at 250 survive.
    region = D.emit_anchor_segments(changes, caps, escs, cts, total, anchors, seed,
                                    drop_regions=[(340, 410)])
    rw = waits(region)
    assert "LOADING_START" not in rw and "LOADING_END" not in rw, f"(4) region waits: {rw}"
    assert "EXTRA_SPRITE_START" not in rw, f"(4) region waits: {rw}"
    assert "TEXT_ANIM_START" in rw and rw.count("CONV_POSE_BLINK") == 2, \
        f"(4) region over-dropped: {rw}"

    # (5) the auto-play boundary hint fires on sparse held input.
    hint = D._suggest_autoplay_boundary(changes, total, min_hold=240)
    assert hint == 200, f"(5) boundary hint: {hint}"

    # (6) carry-pins: hand-tuned PIN ops (not in the raw) are re-applied on re-distill.
    #     head pins go before the first rngseed; a mid pin re-anchors after the segment
    #     whose rng matches (here the PAUSE_OPEN@60 anchor, rng=12).
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "src.jsonl"
        # a stand-in hand-tuned trace: 4 head load pins + 1 rng-anchored mid pin.
        src.write_text("\n".join([
            '{"savefile": "../_saves/deadbeef.sav.gz"}',
            '{"csloadpin": 24}', '{"primaryloadpin": 16}', '{"tutloadpin": 8}',
            '{"bgnpcseed": [123, 1, [0,0]]}',
            '{"rngseed": [0, 999]}', '{"frame": 0, "buttons": "0x0000"}',
            '{"caprange": [0, 14000]}',                         # capture directive — NOT a pin
            '{"wait": "PAUSE_OPEN"}', '{"rngseed": [0, 12]}',   # seg rng 12 == anchor rng
            '{"bgnpcpin": [0, [1,2,3]]}', '{"frame": 0, "buttons": "0x0000"}',
        ]) + "\n")
        head, midp = D.extract_carry_pins(src)
        assert [next(iter(o)) for o in head] == \
            ["csloadpin", "primaryloadpin", "tutloadpin", "bgnpcseed"], f"(6) head: {head}"
        # caprange is a CAPTURE directive, never a carried pin (a stale one re-dumps GBs)
        assert not any("caprange" in o for o in head), f"(6) caprange leaked into head: {head}"
        assert not any("caprange" in o for _, o in midp), f"(6) caprange leaked into mid: {midp}"
        assert len(midp) == 1 and midp[0][0] == 12 and "bgnpcpin" in midp[0][1], \
            f"(6) mid: {midp}"
        carried = D.emit_anchor_segments(changes, caps, escs, cts, total, anchors, seed,
                                         carry_head=head, carry_mid=midp)
        lines = [l for l in carried.splitlines() if l.strip() and not l.startswith("#")]
        # head pins precede the first rngseed
        first_seed = next(i for i, l in enumerate(lines) if '"rngseed"' in l)
        head_keys = [next(iter(json.loads(l))) for l in lines[:first_seed]]
        assert head_keys == ["csloadpin", "primaryloadpin", "tutloadpin", "bgnpcseed"], \
            f"(6) emitted head order: {head_keys}"
        # the mid pin sits immediately after the PAUSE_OPEN segment's {rngseed:[0,12]}
        idx12 = next(i for i, l in enumerate(lines) if json.loads(l).get("rngseed") == [0, 12])
        assert "bgnpcpin" in lines[idx12 + 1], \
            f"(6) bgnpcpin not re-anchored after seg rng 12: {lines[idx12:idx12+2]}"

    # (7) trailing hold: the final segment runs through to the recording's end so
    #     post-last-anchor idle (DAY 2 brooming) replays instead of being trimmed.
    #     Last anchor CONV_POSE_END@470, total=500 → a {frame:30} hold closes it.
    def last_frame_row(text):
        last = None
        for ln in text.splitlines():
            s = ln.strip()
            if not s or s.startswith("#"):
                continue
            o = json.loads(s)
            if "buttons" in o and "frame" in o:
                last = o
        return last
    lf = last_frame_row(base)
    assert lf == {"frame": 30, "buttons": "0x0000"}, f"(7) trailing hold: {lf}"

    print("OK: distill_trace drop-fragile fold "
          f"(baseline {len(base_waits)} waits → drop-after {len(dw)}; "
          f"region-drop {len(rw)}; hint@{hint}; carry {len(head)} head + {len(midp)} mid; "
          f"trailing-hold {lf['frame']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
