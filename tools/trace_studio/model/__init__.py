"""trace_studio.model — PURE, unit-testable model layer.

No engine/driver/I-O-on-the-game deps: parse trace ops + anchor streams, align
two sides (the align.mjs twin), build the v2 segmented timeline, and load/write
the versioned session manifest.
"""
