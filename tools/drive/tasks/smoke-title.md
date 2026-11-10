---
name: smoke-title
start: fresh
steps:
  - title menu takes input: scene == 0 and t_fade == 0 and frame > 80
  - a new game has started: scene == 1
forbid:
  - the game was left: scene == 2
---
Harness smoke test. From a fresh boot, wait for the title menu, start a New Game, and stop as soon as
`scene == 1`. Then `rd check` this file, `rd verify`, and finish with a three-line report. No pictures.
