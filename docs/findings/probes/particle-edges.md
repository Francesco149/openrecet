# Probe: particle-edges (partial: none of the three edges reachable by play)

- **Run:** `pe-a` → scenario `particle-edges` · **start:** save:824053720e01 (crafted hire lineage; Caillou, Amber Garden 1F) · **task:** `tools/drive/tasks/particle-edges.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/particle-edges/` (2,260 frames: Caillou's hit sparks 0x98 spawned and killed; a flier's sting
  → 32 return sparks 0x75, the flier killed mid-flight)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f2260, rng 6ec60d46); golden + triage: **identical**; gate row.

1. **Water drops 0x11:** `wet` needs the feet below a water surface, the drops a fall into water faster than 0.2 / tick.
   Water surfaces per stage (collision materials): Hall of Trials (4), Jade Way 1F (5) / 6F (9), Amber 6F (8): none;
   Amber 1F bridges (6): y −2.2 under the islands (pit floor −5, a fall below −4 kills); Amber 16F lava (10) −3.9; the
   underground lake (11: Amber 21F, Lapis 1F) −1.6..−2.7; Amber 11F valley (13) −2.7..−4.5 in one room type. Invisible
   walls stop the player at every edge (searches over ~2,800 cells on 4 stages found no way in). Stages ≥ 14 untested.
2. **0x98 at age 64:** 32 sparks per swing hit; each starts steering at a random tick 16..31; the steering is a damped
   spring that lands on a still target ~12 ticks later at any distance — missing needs the target moving ≥ ~0.75 / tick
   (Caillou walks 0.155; his teleport, key 5, jumps at skill tick 55, too late). Killed within 3 units at ages 24..42.
3. **0x75 at age 64:** fliers `FUN_0042a74c` (types 0x11..0x15, 0x38..0x3a) fire record 19 (a drain sting); a hit sends
   32 return sparks aimed at the flier's slot; killing it mid-flight leaves its position in place (even after the slot
   frees) and the sparks die at the corpse.

So 0x4102b5 / 0x4103dd need a fast-moving or teleporting target — practically unreachable; 0x11 needs a way into water not
found. Left in `retail-coverage.md` as "reachable in principle, not by play found".
