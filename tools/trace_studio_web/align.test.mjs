// align.test.mjs — node test for the pure alignment core. Run:
//   nix develop --command node tools/trace_studio_web/align.test.mjs
import {
  parseSegments, resolveBases, sideLayout, absToX, xToAbs, itemAbs, divergenceReport,
  resolveSide, editorLayout, bandAt, absToBand,
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
// The BAND MODEL (the segment-alignment workhorse). See
// docs/findings/trace-editor-segment-alignment.md for the semantics.
// ════════════════════════════════════════════════════════════════════════════

// ── resolveSide: bases match resolveBases + every firing gets a PLACEMENT ─────
// WELL-FORMED town-map-load-rerecord: port loads fast, retail's 1st load runs ~14k
// ticks; both still place every anchor in the SAME band ⇒ they align band-for-band.
const wfTrace = parseSegments([
  { frame: 0, buttons: "0x0000" },
  { wait: "NEW_GAME" }, { frame: 0, buttons: "0x0000" },
  { wait: "LOADING_END" }, { frame: 0, buttons: "0x0000" }, { frame: 50, buttons: "0x0001" },
  { wait: "LOADING_START" }, { frame: 0, buttons: "0x0000" },
  { wait: "PAUSE_OPEN" }, { frame: 0, buttons: "0x0000" },
  { wait: "LOADING_END" }, { caprange: [0, 640] }, { frame: 0, buttons: "0x0000" }, { frame: 30, buttons: "0x0004" },
]);
const wfPort = [
  { anchor: "BOOT", frame: 0 }, { anchor: "NEW_GAME", frame: 168 }, { anchor: "LOADING_START", frame: 168 },
  { anchor: "LOADING_END", frame: 389 }, { anchor: "HOUSE_FREEROAM", frame: 389 },
  { anchor: "LOADING_START", frame: 630 }, { anchor: "PAUSE_OPEN", frame: 630 }, { anchor: "LOADING_END", frame: 659 },
];
const wfRetail = [
  { anchor: "BOOT", frame: 0 }, { anchor: "NEW_GAME", frame: 168 }, { anchor: "LOADING_START", frame: 168 },
  { anchor: "LOADING_END", frame: 14548 }, { anchor: "HOUSE_FREEROAM", frame: 14548 },
  { anchor: "LOADING_START", frame: 14789 }, { anchor: "PAUSE_OPEN", frame: 14789 }, { anchor: "LOADING_END", frame: 15105 },
];
const wfP = resolveSide(wfTrace, wfPort), wfR = resolveSide(wfTrace, wfRetail);
// bases are byte-identical to resolveBases (the single walk can't drift from the resolver)
eq(wfP.bases, resolveBases(wfTrace, wfPort), "resolveSide port bases == resolveBases");
eq(wfR.bases, resolveBases(wfTrace, wfRetail), "resolveSide retail bases == resolveBases");
// {wait PAUSE_OPEN} (seg 4) is UNRESOLVED on BOTH sides: PAUSE_OPEN fires the same frame as
// LOADING_START (not strictly after) — faithful to the resolver, band stays empty of anchors.
ok(wfP.bases[4].ok === false && wfR.bases[4].ok === false, "seg4 PAUSE_OPEN unresolved both sides (same-frame)");
// the headline: both sides place anchors in IDENTICAL bands ⇒ the ~14k load-stretch collapses
eq(wfP.placements.map(p => p.seg), [0, 1, 1, 2, 2, 3, 3, 5], "well-formed: port anchor bands");
eq(wfR.placements.map(p => p.seg), [0, 1, 1, 2, 2, 3, 3, 5], "well-formed: retail anchor bands (== port)");
eq(wfP.placements.map(p => p.rel), [0, 0, 0, 0, 0, 0, 0, 0], "well-formed: all rels 0 (anchors on band edges)");

// ── DIVERGENT town-map-load: retail never reached the 2nd load, so retail segs 2/3/4
// all fall back to base 11806. The firing must still land in the band it BELONGS to. ──
const dvTrace = parseSegments([
  { frame: 0, buttons: "0x0000" },
  { wait: "NEW_GAME" }, { frame: 0, buttons: "0x0000" },
  { wait: "LOADING_END" }, { caprange: [0, 868] }, { frame: 0, buttons: "0x0000" }, { frame: 50, buttons: "0x0001" },
  { wait: "LOADING_START" }, { frame: 0, buttons: "0x0000" },
  { wait: "LOADING_END" }, { frame: 0, buttons: "0x0000" }, { frame: 40, buttons: "0x0004" },
]);
const dvPort = [
  { anchor: "BOOT", frame: 0 }, { anchor: "NEW_GAME", frame: 157 }, { anchor: "LOADING_START", frame: 157 },
  { anchor: "LOADING_END", frame: 416 }, { anchor: "HOUSE_FREEROAM", frame: 416 },
  { anchor: "LOADING_START", frame: 643 }, { anchor: "PAUSE_OPEN", frame: 643 }, { anchor: "LOADING_END", frame: 676 },
];
const dvRetail = [
  { anchor: "BOOT", frame: 0 }, { anchor: "NEW_GAME", frame: 157 }, { anchor: "LOADING_START", frame: 157 },
  { anchor: "LOADING_END", frame: 11806 }, { anchor: "HOUSE_FREEROAM", frame: 11806 },
];
const dvP = resolveSide(dvTrace, dvPort), dvR = resolveSide(dvTrace, dvRetail);
eq(dvR.bases.map(b => [b.base, b.ok]), [[0, true], [157, true], [11806, true], [11806, false], [11806, false]],
  "divergent: retail segs 3/4 fall back to base 11806 (unresolved)");
// THE bug fix: retail LOADING_END@11806 + HOUSE_FREEROAM@11806 place in seg 2 (NOT seg 4)
eq(dvR.placements.map(p => p.seg), [0, 1, 1, 2, 2], "divergent: retail anchors → seg 2, NOT the stacked seg 4");
eq(dvP.placements.map(p => p.seg), [0, 1, 1, 2, 2, 3, 3, 4], "divergent: port anchors fill all 5 bands");

// ── editorLayout: sequential, non-overlapping bands sized by CONTENT (no window) ──
const dvL = editorLayout(dvTrace, dvPort, dvRetail);
ok(dvL.X.every((x, k) => k === 0 || x > dvL.X[k - 1]), "layout: band origins strictly increasing");
ok(dvL.X.every((x, k) => k === 0 || x >= dvL.X[k - 1] + dvL.W[k - 1]), "layout: bands never overlap (X[k] ≥ X[k-1]+W[k-1])");
eq(dvL.W[2], 54, "layout: seg-2 band sized to its content (inputs to frame 50, +pad)");
ok(dvL.window === null, "layout: no window params → window is null");
// retail's seg-2 content sits at the SAME band origin as the port's ⇒ aligned
eq(dvL.X[2] + dvR.placements[3].rel, dvL.X[2] + dvP.placements[3].rel, "layout: retail vs port LOADING_END coincide in band 2");

// ── absToBand: an absolute frame → the band it falls in (resolved bases only) ──
eq(absToBand(416, dvP.bases), { seg: 2, rel: 0 }, "absToBand: port 416 = seg-2 base");
eq(absToBand(500, dvP.bases), { seg: 2, rel: 84 }, "absToBand: port 500 → seg 2 + 84");
// retail's unresolved segs 3/4 are SKIPPED — a frame past 11806 stays in seg 2 (the last
// segment retail actually fired), never a stacked/unresolved later band.
eq(absToBand(99999, dvR.bases).seg, 2, "absToBand: retail frame past its last anchor stays in seg 2 (skips unresolved)");

// ── the CAPTURED WINDOW spans multiple bands (the merchants-guild bug): the window is an
// absolute span on a side, mapped across every band it covers — each widened to fit. ──
const dvW = editorLayout(dvTrace, dvPort, dvRetail, { windowSide: "port", windowStartAbs: 416, windowEndAbs: 700 });
eq(dvW.window, { startSeg: 2, startRel: 0, endSeg: 4, endRel: 24 }, "window: spans seg2→seg4 (caprange counted through 2 segments)");
ok(dvW.W[2] >= 227 && dvW.W[3] >= 33 && dvW.W[4] >= 25, "window: each COVERED band widened to fit its slice (227/33/25)");
ok(dvW.X.every((x, k) => k === 0 || x >= dvW.X[k - 1] + dvW.W[k - 1]), "window: bands still never overlap");

// ── bandAt: screen→segment inverse ───────────────────────────────────────────
eq(bandAt(dvL.X, dvL.X[2] + 40), { seg: 2, rel: 40 }, "bandAt: a point in band 2 → {seg2, rel}");
eq(bandAt(dvL.X, dvL.X[4] + 3).seg, 4, "bandAt: a point in band 4 → seg 4 (not stacked into 2)");
eq(bandAt(dvL.X, -5).seg, 0, "bandAt: left of band 0 clamps to seg 0");

// ── edge cases: a single-segment dense session + a no-anchors trace ───────────
const oneSeg = parseSegments([{ frame: 0, buttons: "0x0000" }, { frame: 10, buttons: "0x0010" }]);
const oneL = editorLayout(oneSeg, [{ anchor: "BOOT", frame: 0 }], [{ anchor: "BOOT", frame: 0 }],
  { windowSide: "port", windowStartAbs: 0, windowEndAbs: 47 });
eq(oneL.X, [0], "edge: single segment → one band at origin 0");
eq(oneL.W, [52], "edge: single-segment band fits the 48f window (0..47, +pad)");
eq(oneL.window, { startSeg: 0, startRel: 0, endSeg: 0, endRel: 47 }, "edge: single-segment window stays in band 0");
const noAnchors = editorLayout(dvTrace, [], []);
eq(noAnchors.port.placements, [], "edge: no firings → no placements (no crash)");
ok(noAnchors.X.length === 5, "edge: bands still laid out from items alone when anchors absent");

console.log(`\nalign.test: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
