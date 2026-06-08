// align.test.mjs — node test for the pure alignment core. Run:
//   nix develop --command node tools/trace_studio_web/align.test.mjs
import {
  parseSegments, resolveBases, sideLayout, absToX, xToAbs, itemAbs, divergenceReport,
  refFrame,
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

// ── refFrame: piecewise re-base a side onto the reference (port) axis ─────────
// Real town-map-load numbers: the port skips retail's load, so retail's LOADING_END
// lands at frame 11806 where the port's is 363. Piecewise re-basing must collapse
// retail's seg-2 content onto the port's truthful frames (the editor-alignment bug).
const tmSegs = parseSegments([
  { frame: 0, buttons: "0x0000" }, { wait: "NEW_GAME" }, { frame: 0, buttons: "0x0000" },
  { wait: "LOADING_END" }, { frame: 0, buttons: "0x0000" }, { frame: 50, buttons: "0x0001" },
]);
const tmPort = [{ anchor: "BOOT", frame: 0 }, { anchor: "NEW_GAME", frame: 157 }, { anchor: "LOADING_END", frame: 363 }];
const tmRetail = [{ anchor: "BOOT", frame: 0 }, { anchor: "NEW_GAME", frame: 157 }, { anchor: "LOADING_END", frame: 11806 }];
const tmPB = resolveBases(tmSegs, tmPort), tmRB = resolveBases(tmSegs, tmRetail);
eq(tmPB.map(b => b.base), [0, 157, 363], "town-map port bases");
eq(tmRB.map(b => b.base), [0, 157, 11806], "town-map retail bases (load-stretched)");
eq(refFrame(11806, tmRB, tmPB), 363, "refFrame: retail LOADING_END 11806 → port 363 (collapsed)");
eq(refFrame(11856, tmRB, tmPB), 413, "refFrame: retail seg-2 +50 (11856) → port 413");
eq(refFrame(363, tmPB, tmPB), 363, "refFrame: reference (port) through itself is identity");
eq(refFrame(157, tmRB, tmPB), 157, "refFrame: shared NEW_GAME base is unchanged");
eq(refFrame(100, tmRB, tmPB), 100, "refFrame: pre-load seg-0 frame is unchanged");

// A WITHIN-segment divergence must SURVIVE (only the declared {wait} base shift is
// absorbed): an anchor 24f into the segment on port but 10f in on retail stays a gap.
ok(refFrame(363 + 24, tmPB, tmPB) !== refFrame(11806 + 10, tmRB, tmPB),
  "refFrame: a within-segment offset (port +24 vs retail +10) survives as a gap");
eq(refFrame(11806 + 10, tmRB, tmPB), 373, "refFrame: retail seg-2 +10 → port 373 (not 387)");

console.log(`\nalign.test: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
