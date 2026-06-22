#!/usr/bin/env python3
"""cs_walker_drill.py — port↔retail per-frame drill for the in-shop chibi-NPC
pump (FUN_0047019f), the cs-walker-rng-phase residual.

Reads two call_trace.jsonl captures (port + retail), aligns them at the f406
first-customer cc08 entry (cc08==4 && b51c==0), and prints a per-offset table of
the NPC-pump state + the per-frame LCG-draw delta on each side, flagging frames
where the draw counts diverge.  This is the drill the PORT-DEBT(cs-walker-rng-
phase) note asks for: it pins the exact frame the port's spawn/retarget draws
fall out of step with retail's.

The port emits npcfr/npcsp/npcn/npcdr in its 0x48670f probe (scene1_player_ctrl.c);
retail emits npcfr/npcsp (tools/flow/retail_fields.json).  Retail call-trace rows
carry `seq`/`ts` (no per-row `frame`) with a separate frame-marker row, so we
attach probe rows to the current frame as we stream.

Usage:
  cs_walker_drill.py <port_dir> <retail_dir> [--span N] [--anchor-b51c {0,1}]
"""
import argparse
import json
import sys
from pathlib import Path

VA_STATE = 4744975   # 0x48670f house_update probe


def load(path: Path) -> dict:
    """frame -> merged probe field dict (0x48670f fields + rngcalls)."""
    st: dict = {}
    cur = None
    with open(path) as fh:
        for line in fh:
            # cheap pre-filter to avoid JSON-parsing every call-graph row
            has_frame = '"frame"' in line
            is_state = '4744975' in line
            is_rng = '"rngcalls"' in line
            if not (has_frame or is_state or is_rng):
                continue
            o = json.loads(line)
            if "frame" in o and o.get("va") != VA_STATE:
                cur = o["frame"]
            fr = o.get("frame", cur)
            if fr is None:
                continue
            if o.get("va") == VA_STATE:
                st.setdefault(fr, {}).update(o["f"])
            elif "f" in o and "rngcalls" in o["f"]:
                st.setdefault(fr, {})["rngcalls"] = o["f"]["rngcalls"]
    return st


def entry_frame(st: dict, b51c: int) -> int | None:
    for f in sorted(k for k in st if "cc08" in st[k]):
        if st[f].get("cc08") == 4 and st[f].get("b51c") == b51c:
            return f
    return None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port_dir", type=Path)
    ap.add_argument("retail_dir", type=Path)
    ap.add_argument("--span", type=int, default=80)
    ap.add_argument("--anchor-b51c", type=int, default=0,
                    help="align at the first cc08==4 frame with this b51c "
                         "(0 = live first-customer, 1 = scripted tutorial)")
    a = ap.parse_args(argv)

    ps = load(a.port_dir / "call_trace.jsonl")
    rs = load(a.retail_dir / "call_trace.jsonl")
    pe = entry_frame(ps, a.anchor_b51c)
    re_ = entry_frame(rs, a.anchor_b51c)
    print(f"port entry frame {pe} | retail entry frame {re_} "
          f"(cc08==4, b51c=={a.anchor_b51c})")
    if pe is None or re_ is None:
        print("!! could not find the alignment anchor on one side", file=sys.stderr)
        return 2

    def delta(st, e, off):
        a0 = st.get(e + off - 1, {}).get("rngcalls")
        a1 = st.get(e + off, {}).get("rngcalls")
        return (a1 - a0) if (a0 is not None and a1 is not None) else None

    hdr = (f"{'off':>4} | {'PORT':>2} b1cc npcfr npcsp npcn b534 rngΔ "
           f"|| {'RET':>2} b1cc npcfr npcsp b534 rngΔ")
    print(hdr); print("-" * len(hdr))
    ndiv = 0
    for off in range(0, a.span):
        p = ps.get(pe + off, {}); r = rs.get(re_ + off, {})
        pd = delta(ps, pe, off); rd = delta(rs, re_, off)
        div = (pd is not None and rd is not None and pd != rd)
        ndiv += div
        mark = "  <<< rngΔ" if div else ""
        print(f"{off:>4} |    {p.get('b1cc')} {str(p.get('npcfr')):>4} "
              f"{str(p.get('npcsp')):>4} {str(p.get('npcn')):>3} "
              f"{str(p.get('b534')):>3} {str(pd):>4} "
              f"||    {r.get('b1cc')} {str(r.get('npcfr')):>4} "
              f"{str(r.get('npcsp')):>4} {str(r.get('b534')):>3} "
              f"{str(rd):>4}{mark}")
    print(f"\n{ndiv}/{a.span} offsets diverge in per-frame rngΔ")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
