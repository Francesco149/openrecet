// align.test.mjs — node test for the pure alignment core. Run:
//   nix develop --command node tools/trace_studio_web/align.test.mjs
import {
  parseSegments, resolveBases, sideLayout, absToX, xToAbs, itemAbs, divergenceReport,
  loadSpans, capIndexOfAbs, absOfCapIndex,
} from "./align.mjs";

let pass = 0, fail = 0;
const eq = (got, want, msg) => {
  const g = JSON.stringify(got), w = JSON.stringify(want);
  if (g === w) { pass++; } else { fail++; console.log(`✗ ${msg}\n    got  ${g}\n    want ${w}`); };
};
const ok = (cond, msg) => { if (cond) pass++; else { fail++; console.log(`✗ ${msg}`); } };

// A town-map-style trace: the recorded (retail) order waits CONV_POSE_START BEFORE
// LOADING_END, with an input + a pin in the final segment.
const trace = [
  { savefile: "x" },
  { frame: 0, buttons: "0x0000" }, { frame: 10, buttons: "0x0010" },
  { wait: "NEW_GAME" },
  { frame: 0, buttons: "0x0000" },
  { wait: "CONV_POSE_START" },
  { frame: 5, buttons: "0x0004" },
  { wait: "LOADING_END" },
  { phasepin: 20 }, { rngseed: [20, 19937] }, { caprange: [0, 48] },
  { frame: 0, buttons: "0x0000" },
];

const segs = parseSegments(trace);
eq(segs.map(s => s.waitAnchor), [null, "NEW_GAME", "CONV_POSE_START", "LOADING_END"], "segment wait anchors");
eq(segs[0].items.map(i => i.kind), ["input", "input"], "seg0 inputs");
eq(segs[3].items.map(i => `${i.kind}@${i.frame}`), ["phasepin@20", "rngseed@20", "input@0"], "seg3 items (caprange not drawn)");

// Retail fired everything in recorded order; port fired LOADING_END BEFORE CONV_POSE_START.
const retail = [
  { anchor: "BOOT", frame: 0 }, { anchor: "NEW_GAME", frame: 81 },
  { anchor: "CONV_POSE_START", frame: 108 }, { anchor: "LOADING_END", frame: 111 },
  { anchor: "HOUSE_FREEROAM", frame: 111 },
];
const port = [
  { anchor: "BOOT", frame: 0 }, { anchor: "NEW_GAME", frame: 261 },
  { anchor: "LOADING_END", frame: 2726 }, { anchor: "HOUSE_FREEROAM", frame: 2726 },
  { anchor: "CONV_POSE_START", frame: 2750 },
];

const rb = resolveBases(segs, retail);
eq(rb.map(b => [b.base, b.ok]), [[0, true], [81, true], [108, true], [111, true]], "retail bases all resolve");

const pb = resolveBases(segs, port);
ok(pb[1].ok && pb[1].base === 261, "port NEW_GAME resolves @261");
ok(pb[2].ok && pb[2].base === 2750, "port CONV_POSE_START resolves @2750 (after HF)");
ok(!pb[3].ok, "port LOADING_END is UNRESOLVED (it fired @2726, before CONV_POSE_START@2750) ⇒ the divergence is flagged");

// Sync on the NEW_GAME segment (resolved on both): CONV_POSE_START sits at a very
// different anchor-relative offset → the divergence is visible as a horizontal gap.
const rep = divergenceReport(segs, port, retail, 1 /* sync = NEW_GAME seg */);
const conv = rep[2];
ok(conv.retailRel === 27, `retail CONV_POSE_START +27 from NEW_GAME (got ${conv.retailRel})`);
ok(conv.portRel === 2489, `port CONV_POSE_START +2489 from NEW_GAME (got ${conv.portRel})`);
ok(conv.portRel !== conv.retailRel, "CONV_POSE_START diverges (port rel ≠ retail rel)");

// A trace item maps to absolute correctly on each side.
const pinItem = segs[3].items[0]; // phasepin@20 (seg3)
eq(itemAbs(pinItem, 3, rb), 131, "retail: phasepin seg3+20 = LOADING_END(111)+20");
eq(itemAbs(pinItem, 3, pb), 2770, "port: phasepin seg3+20 = unresolved-base(cursor=2750)+20");

// absToX / xToAbs round-trip.
const sf = sideLayout(segs, retail, 1).syncFrame; // = 81
eq(absToX(111, sf, 2), 60, "absToX: (111-81)*2 = 60");
eq(xToAbs(60, sf, 2), 111, "xToAbs round-trips");

// ════════════════════════════════════════════════════════════════════════════
// THE CAPTURED-INDEX MODEL (the timeline workhorse). The x-axis is the dense captured-frame
// ordinal per side; a 1:1 capture aligns with no forcing. See
// docs/findings/trace-editor-segment-alignment.md.
// ════════════════════════════════════════════════════════════════════════════

// ── loadSpans: LOADING_START→END pairs (a dangling START is dropped) ──────────
const lsFirings = [
  { anchor: "BOOT", frame: 0 }, { anchor: "LOADING_START", frame: 927 },
  { anchor: "PAUSE_CLOSE", frame: 927 }, { anchor: "LOADING_END", frame: 928 },
  { anchor: "LOADING_START", frame: 1500 },                 // dangling (no END)
];
eq(loadSpans(lsFirings), [{ start: 927, end: 928 }], "loadSpans: one completed pair, dangling START dropped");

// ── capIndexOfAbs: real merchants-guild PORT numbers (base_abs 766, one 1-frame internal
// load at 927→928 — the suppressed frame 161 missing from the captured PNGs). ──
const pBase = 766, pLoads = [{ start: 927, end: 928 }];
eq(capIndexOfAbs(766, pBase, pLoads), 0, "capIndex: base_abs → 0");
eq(capIndexOfAbs(926, pBase, pLoads), 160, "capIndex: abs 926 → 160 (last pre-load frame)");
eq(capIndexOfAbs(928, pBase, pLoads), 161, "capIndex: abs 928 (LOADING_END) → 161 (load frame 927 suppressed)");
eq(capIndexOfAbs(929, pBase, pLoads), 162, "capIndex: abs 929 → 162");
eq(capIndexOfAbs(700, pBase, pLoads), -66, "capIndex: a frame before the window → negative");

// THE alignment property: two sides with DIFFERENT base_abs AND different load STRETCH map a
// corresponding gameplay frame (no internal load before it) to the SAME index — they align
// with zero forcing, purely because the capture is 1:1.
eq(capIndexOfAbs(766 + 90, 766, []), 90, "capIndex: port gameplay +90 → index 90");
eq(capIndexOfAbs(15123 + 90, 15123, []), 90, "capIndex: retail (base 15123) +90 → index 90 (aligned, no forcing)");
// a load BEFORE base_abs (its frames weren't captured here) is NOT subtracted.
eq(capIndexOfAbs(15200, 15123, [{ start: 14789, end: 15105 }]), 77, "capIndex: a pre-window load is ignored");
// two internal loads (retail-like, durations 85 + 26) shift the post-load index by 111.
const rLoads = [{ start: 15284, end: 15369 }, { start: 15370, end: 15396 }];
eq(capIndexOfAbs(15200, 15123, rLoads), 77, "capIndex(2-load): before both loads → 77");
eq(capIndexOfAbs(15369, 15123, rLoads), 161, "capIndex(2-load): end of load 1 → 161 (−85)");
eq(capIndexOfAbs(15396, 15123, rLoads), 162, "capIndex(2-load): after both loads → 162 (273 − 111)");

// ── absOfCapIndex: the inverse (index → abs), round-trips through the suppressed load ──
eq(absOfCapIndex(0, pBase, pLoads), 766, "absOfCapIndex: 0 → base_abs");
eq(absOfCapIndex(160, pBase, pLoads), 926, "absOfCapIndex: 160 → 926");
eq(absOfCapIndex(161, pBase, pLoads), 928, "absOfCapIndex: 161 → 928 (skips the suppressed load frame 927)");
eq(absOfCapIndex(162, pBase, pLoads), 929, "absOfCapIndex: 162 → 929");
for (const g of [0, 50, 160, 161, 200])
  ok(capIndexOfAbs(absOfCapIndex(g, pBase, pLoads), pBase, pLoads) === g, `round-trip capIndex∘absOfCapIndex(${g})`);
for (const g of [0, 100, 161, 162, 300])
  ok(capIndexOfAbs(absOfCapIndex(g, 15123, rLoads), 15123, rLoads) === g, `round-trip (2-load) g=${g}`);

// ── inputs map to a captured index via the segment base (resolveBases) + capIndex ──
// A trace input at (seg2, +30) on a side whose seg2 base is 766 → abs 796 → index 30.
const inSegs = parseSegments([
  { frame: 0, buttons: "0x0000" }, { wait: "NEW_GAME" }, { frame: 0, buttons: "0x0000" },
  { wait: "LOADING_END" }, { frame: 30, buttons: "0x0010" },
]);
const inFirings = [
  { anchor: "BOOT", frame: 0 }, { anchor: "NEW_GAME", frame: 157 }, { anchor: "LOADING_END", frame: 766 },
];
const inBases = resolveBases(inSegs, inFirings);
const inItem = inSegs[2].items[0];                          // input @ seg2 +30
eq(capIndexOfAbs(itemAbs(inItem, 2, inBases), 766, []), 30, "input seg2+30 → captured index 30 (abs 796 − base 766)");

console.log(`\nalign.test: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
