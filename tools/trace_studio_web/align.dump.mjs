// align.dump.mjs — emit a canonical projection of the shared fixture through the
// JS alignment twin, for the Python golden cross-check.
//   nix develop --command node tools/trace_studio_web/align.dump.mjs
// tools/test_trace_studio_segments.py runs this and asserts the projection equals
// the one trace_studio.model.segments produces from the same fixture — so the JS
// twin and the Python source-of-truth can never silently drift.
import { readFileSync } from "node:fs";
import {
  parseSegments, resolveBases, sideLayout, absToX, xToAbs, itemAbs, divergenceReport,
  resolveSide, editorLayout, bandAt, absToBand,
} from "./align.mjs";

const fx = JSON.parse(readFileSync(new URL("./align.fixture.json", import.meta.url)));
const segs = parseSegments(fx.trace);
const rb = resolveBases(segs, fx.retail);
const pb = resolveBases(segs, fx.port);
const rep = divergenceReport(segs, fx.port, fx.retail, fx.sync_seg);
const sf = sideLayout(segs, fx.retail, fx.sync_seg).syncFrame;
const rsR = resolveSide(segs, fx.retail);
const rsP = resolveSide(segs, fx.port);
// a multi-segment captured window on the retail side (abs 85…140 spans seg1→seg3).
const elay = editorLayout(segs, fx.port, fx.retail,
  { windowSide: "retail", windowStartAbs: 85, windowEndAbs: 140 });

// A normalized, key-name-agnostic projection (the semantic content both impls share).
const proj = {
  segments: segs.map(s => ({ wait: s.waitAnchor, items: s.items.map(i => [i.kind, i.frame]) })),
  retail_bases: rb.map(b => [b.base, b.ok]),
  port_bases: pb.map(b => [b.base, b.ok]),
  divergence: rep.map(d => [d.seg, d.anchor, d.portRel, d.retailRel]),
  item_abs: { retail: itemAbs(segs[3].items[0], 3, rb), port: itemAbs(segs[3].items[0], 3, pb) },
  roundtrip: { absToX: absToX(111, sf, 2), xToAbs: xToAbs(60, sf, 2) },
  // the BAND MODEL: per-side firing placement (segment a firing belongs to) + the sequential
  // non-overlapping band layout + the screen→segment inverse.
  resolve_side: {
    retail: { bases: rsR.bases.map(b => [b.base, b.ok]), placements: rsR.placements.map(p => [p.seg, p.rel]) },
    port: { bases: rsP.bases.map(b => [b.base, b.ok]), placements: rsP.placements.map(p => [p.seg, p.rel]) },
  },
  editor_layout: { X: elay.X, W: elay.W, ext: elay.ext, window: elay.window },
  band_at: [35, 2515, 3000, -3].map(pos => { const b = bandAt(elay.X, pos); return [pos, b.seg, b.rel]; }),
  abs_to_band: [85, 140, 200].map(a => { const b = absToBand(a, rsR.bases); return [a, b.seg, b.rel]; }),
};
process.stdout.write(JSON.stringify(proj));
