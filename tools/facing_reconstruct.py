#!/usr/bin/env python3
"""
tools/facing_reconstruct.py — recover the per-frame walk-impulse heading (db05c)
of PORT and RETAIL across the house-table-corner divergence (engine-quirks §69),
WITHOUT a new retail capture.

The HOUSE free-roam velocity recurrence (FUN_0048b850) is
    W_n = clamp_0.175( V_{n-1} + 0.1 * (sin d_n, cos d_n) )      # pre-damp
    V_n = 0.82 * W_n                                             # post-damp (recorded)
where d_n is the stored facing `_DAT_056db05c` used that frame. Both clamp and
damp preserve direction, so
    theta(V_n) = atan2( V_{n-1}.x + 0.1 sin d_n , V_{n-1}.z + 0.1 cos d_n ).
Given consecutive recorded velocities we solve that 1-D equation for d_n. The
port additionally LOGS its true facing (--player-pos-log `facing`), so we can
validate the reconstruction against ground truth on the port side, then trust it
on the retail side (where only vx/vz were captured).

The point: the port sets d_n = atan2(dpad) raw every frame; retail's d_n is a
slewed/sticky stored heading. This tool shows, frame-by-frame, how retail's
reconstructed heading differs from raw atan2(dpad) at the steering corner.

Usage:
  tools/facing_reconstruct.py \
      --port   /tmp/corner_port_pos.jsonl \
      --retail tests/scenarios/house-table-corner/golden-retail/watch.jsonl \
      --offset 808 --rel-lo 1800 --rel-hi 1860
(--offset = port_frame - retail_rel; find it by matching the px/pz/vx/vz state.)
"""
from __future__ import annotations
import argparse, json, math
from pathlib import Path

ACCEL = 0.1
CAP   = 0.175
DAMP  = 0.82

# d-pad mask bits (input_binding_mask[0..3]) — see scene1_player_ctrl.c.
BTN_RIGHT, BTN_LEFT, BTN_UP, BTN_DOWN = 0x1, 0x2, 0x4, 0x8


def load_jsonl(p: Path) -> list[dict]:
    return [json.loads(l) for l in p.read_text().splitlines() if l.lstrip().startswith("{")]


def vel_of(row: dict) -> tuple[float, float] | None:
    """(vx, vz) from a port row or a retail watch row (vals nesting)."""
    if "vx" in row and "vz" in row:
        return float(row["vx"]), float(row["vz"])
    v = row.get("vals", {})
    if "vx" in v and "vz" in v:
        return float(v["vx"]), float(v["vz"])
    return None


def dpad_angle(mask: int):
    dx = (1 if mask & BTN_RIGHT else 0) - (1 if mask & BTN_LEFT else 0)
    dz = (1 if mask & BTN_DOWN else 0) - (1 if mask & BTN_UP else 0)
    if dx == 0 and dz == 0:
        return None
    return math.atan2(dx, dz)


def ang_norm(a: float) -> float:
    while a > math.pi:  a -= 2 * math.pi
    while a < -math.pi: a += 2 * math.pi
    return a


def solve_facing(prev: tuple[float, float], theta_n: float):
    """Solve atan2(prev.x+0.1 sin d, prev.z+0.1 cos d) == theta_n for d.

    Returns (d, sensitivity) where sensitivity is the |d theta / d d| magnitude
    at the solution (low => d weakly constrained, i.e. momentum-dominated)."""
    px, pz = prev
    best_d, best_err = 0.0, 1e9
    # coarse then fine sweep (robust, no atan2 branch headaches)
    for k in range(0, 3600):
        d = -math.pi + (2 * math.pi) * k / 3600.0
        x = px + ACCEL * math.sin(d)
        z = pz + ACCEL * math.cos(d)
        th = math.atan2(x, z)
        err = abs(ang_norm(th - theta_n))
        if err < best_err:
            best_err, best_d = err, d
    # local sensitivity
    eps = 1e-3
    def th_of(d):
        return math.atan2(px + ACCEL*math.sin(d), pz + ACCEL*math.cos(d))
    sens = abs(ang_norm(th_of(best_d+eps) - th_of(best_d-eps)) / (2*eps))
    return best_d, best_err, sens


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=Path, required=True)
    ap.add_argument("--retail", type=Path, required=True)
    ap.add_argument("--offset", type=int, required=True,
                    help="port_frame - retail_rel")
    ap.add_argument("--rel-lo", type=int, default=1800)
    ap.add_argument("--rel-hi", type=int, default=1860)
    a = ap.parse_args()

    port = {r["frame"]: r for r in load_jsonl(a.port)}
    retail = {r["frame"]: r for r in load_jsonl(a.retail)}

    def deg(x):
        return None if x is None else round(math.degrees(x), 1)

    hdr = f"{'rel':>5} {'btn':>5} {'dpadA':>6} | {'portFace':>8} {'portVth':>7} {'portRec':>7} | {'retVth':>7} {'retRec':>7} {'sens':>5} | {'ret-raw':>7}"
    print(hdr)
    print("-" * len(hdr))
    for rel in range(a.rel_lo, a.rel_hi + 1):
        rrow = retail.get(rel)
        prow = port.get(rel + a.offset)
        rprev = retail.get(rel - 1)
        pprev = port.get(rel - 1 + a.offset)
        if not rrow or not prow:
            continue
        rv, pv = vel_of(rrow), vel_of(prow)
        btn = prow.get("buttons", 0)
        dA = dpad_angle(btn)
        port_face = prow.get("facing")
        # velocity-direction angles
        pvth = math.atan2(pv[0], pv[1]) if (pv and (pv[0] or pv[1])) else None
        rvth = math.atan2(rv[0], rv[1]) if (rv and (rv[0] or rv[1])) else None
        # reconstructed impulse heading
        prec = rrec = sens = None
        if pprev and pv:
            pp = vel_of(pprev)
            if pp and pvth is not None:
                prec, _, _ = solve_facing(pp, pvth)
        if rprev and rv and rvth is not None:
            rp = vel_of(rprev)
            if rp:
                rrec, _, sens = solve_facing(rp, rvth)
        ret_raw = None
        if rrec is not None and dA is not None:
            ret_raw = ang_norm(rrec - dA)
        mark = "  <-- steer" if (dA is not None and port_face is not None
                                 and abs(ang_norm(dA - port_face)) > 0.01) else ""
        print(f"{rel:>5} {btn:>5} {str(deg(dA)):>6} | "
              f"{str(deg(port_face)):>8} {str(deg(pvth)):>7} {str(deg(prec)):>7} | "
              f"{str(deg(rvth)):>7} {str(deg(rrec)):>7} {('' if sens is None else round(sens,2)):>5} | "
              f"{str(deg(ret_raw)):>7}{mark}")


if __name__ == "__main__":
    main()
