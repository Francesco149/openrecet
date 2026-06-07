// align.dump.mjs — emit a canonical projection of the shared fixture through the
// JS alignment twin, for the Python golden cross-check.
//   nix develop --command node tools/trace_studio_web/align.dump.mjs
// tools/test_trace_studio_segments.py runs this and asserts the projection equals
// the one trace_studio.model.segments produces from the same fixture — so the JS
// twin and the Python source-of-truth can never silently drift.
import { readFileSync } from "node:fs";
import {
  parseSegments, resolveBases, sideLayout, absToX, xToAbs, itemAbs, divergenceReport,
} from "./align.mjs";

const fx = JSON.parse(readFileSync(new URL("./align.fixture.json", import.meta.url)));
const segs = parseSegments(fx.trace);
const rb = resolveBases(segs, fx.retail);
const pb = resolveBases(segs, fx.port);
const rep = divergenceReport(segs, fx.port, fx.retail, fx.sync_seg);
const sf = sideLayout(segs, fx.retail, fx.sync_seg).syncFrame;

// A normalized, key-name-agnostic projection (the semantic content both impls share).
const proj = {
  segments: segs.map(s => ({ wait: s.waitAnchor, items: s.items.map(i => [i.kind, i.frame]) })),
  retail_bases: rb.map(b => [b.base, b.ok]),
  port_bases: pb.map(b => [b.base, b.ok]),
  divergence: rep.map(d => [d.seg, d.anchor, d.portRel, d.retailRel]),
  item_abs: { retail: itemAbs(segs[3].items[0], 3, rb), port: itemAbs(segs[3].items[0], 3, pb) },
  roundtrip: { absToX: absToX(111, sf, 2), xToAbs: xToAbs(60, sf, 2) },
};
process.stdout.write(JSON.stringify(proj));
