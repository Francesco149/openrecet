# Behavior Atlas and Guided Exploration (BA-00..BA-08)

> **Status:** ADOPTED 2026-08-22 (roadmap `docs/plans/parity-evidence-roadmap.md` §10 Workstream BA).
> **Owner:** R3/highest-reasoning orchestrator.
> **Schema:** [`../schemas/behavior-atlas-v1.json`](../schemas/behavior-atlas-v1.json).
> **Tooling:** `tools/behavior_atlas.py`, `tools/atlas/`.
> **Vocabulary:** [`parity-vocabulary.md`](parity-vocabulary.md).

---
## 1. Outcome & Architectural Role

The **Behavior Atlas** is the state-and-transition graph representing the verified behavioral universe of Recettear. It bridges three core pillars of the verification program:

```text
    +-------------------------------------------------------------+
    |                     Behavior Atlas Graph                    |
    |  (Content-Addressed Nodes, Certified Edges, State Roots)   |
    +------------------------------+------------------------------+
                                   |
         +-------------------------+-------------------------+
         |                                                   |
         v                                                   v
+-------------------------------+               +-------------------------------+
|     Dynamic Coverage Atlas    |               |    Evidence Compiler (EP-05)  |
|  (CV-03..CV-08 Prioritizer)   |               | (Multi-Pillar Proof Bundles)  |
+-------------------------------+               +-------------------------------+
```

1. **Composable Traversal:** Rather than executing monolithic, hand-stitched 20,000-frame scenarios from cold boot, the Behavior Atlas decomposes executions into composable, content-addressed segments (Edges) between certified game states (Nodes).
2. **Deterministic State Re-use:** When exploring branches (e.g., haggling vs refusing an offer), the engine can restore a known certified prefix up to a Node and branch forward, avoiding redundant simulation.
3. **Coverage Guidance (CV-07/BA-05):** Uncovered blocks, branches, and semantic dimensions in the Coverage Atlas guide the traversal scheduler to synthesize actions toward unexplored graph frontiers.
4. **Truthful Certification:** Edges link directly to EP-05 `proof_id` bundles, proving whether a transition exhibits 1:1 cross-target parity across identity, pixels, render programs, save state, and volatile state.

---

## 2. Behavior Node Identity

A **Behavior Node** uniquely identifies a reproducible, distinct game execution state. It is not an arbitrary tick or frame offset; it is a stable semantic boundary.

### 2.1 Identity Preimage
The Node ID is the SHA-256 hex digest computed over the canonical JSON representation of the tuple:

$$\text{node\_id} = \text{SHA256}\Big(\text{canonical}\big(\text{anchor}, \text{occurrence}, \text{persistent\_state\_root}, \text{volatile\_state\_root}, \text{rng\_state}, \text{config\_id}, \text{retail\_build\_sha256}\big)\Big)$$

Where:
- **`anchor`** (string): The active semantic anchor (e.g. `BOOT`, `TITLE_MENU`, `HOUSE_FREEROAM`, `SAVE_PICKER_READY`, `DIALOGUE_CHOICE`, `CUSTOMER_SERVICE_ENTER`).
- **`occurrence`** (int $\ge 1$): The 1-based instance counter of this anchor since session launch.
- **`persistent_state_root`** (64-hex SHA-256 or `null`): The Merkle root of the persistent save arena (`DAT_056e5770` / `save.dat`), computed via `tools.parity.state_merkle`.
- **`volatile_state_root`** (64-hex SHA-256 or `null`): The Merkle root of live volatile subsystems (`state-volatile-v1.json`: player, companion, customer service, interaction, camera, dust, etc.).
- **`rng_state`** (int or `null`): The exact 32-bit linear congruential generator state (`DAT_006023a0`).
- **`config_id`** (string): The execution environment profile (e.g., `reference-1024-windowed`).
- **`retail_build_sha256`** (64-hex SHA-256): Hash of the target executable `recettear.unpacked.exe`.

### 2.2 Invariant
If two executions produce the same `node_id`, their semantic location, on-disk save state, volatile RAM fields, RNG stream state, and configuration are provably identical.

---

## 3. Behavior Edge Identity

A **Behavior Edge** is a directed transition from a source Node to a destination Node driven by an input sequence under explicit normalization policies.

### 3.1 Identity Preimage
$$\text{edge\_id} = \text{SHA256}\Big(\text{canonical}\big(\text{src\_node\_id}, \text{input\_digest}, \text{completion\_condition}, \text{normalization\_policy}\big)\Big)$$

Where:
- **`src_node_id`** (64-hex SHA-256): The starting Node.
- **`input_digest`** (64-hex SHA-256): The SHA-256 hash of the canonical input stream (ordered list of `(frame, button_mask, analog)`).
- **`completion_condition`** (object): The formal predicate terminating the edge:
  - `anchor_reached`: Semantic anchor reached at specified occurrence.
  - `frame_count`: Fixed duration in simulation ticks.
  - `state_predicate`: Boolean expression over canonical state fields (e.g., `cc08 == 1 && b534 == 0`).
  - `dialogue_advance`: Dialogue box dismiss / script completion.
  - `save_committed`: Completion of `FUN_004905a8` save disk write.
- **`normalization_policy`** (object): Normalization pins required for determinism:
  - `phasepin`: Subsystem phase counter alignment (e.g., `db054 = 80`).
  - `rngseed`: Initial or anchor-bound RNG seed pinning.
  - `playtimepin`: Bilateral async-load playtime normalization.
  - `csloadpin` / `tutloadpin`: Asynchronous worker thread synchronization gates.

---

## 4. Restoration Boundaries & Safety Policy

Not all states can be restored instantaneously via memory pokes. The Atlas classifies restoration safety into three tiers:

| Tier | Boundary Type | Restoration Mechanism | Safety & Verification |
|---|---|---|---|
| **Tier 1: Save-Point (Cold/Warm)** | Title load screen, slot commit | Materialize certified `save.dat` from CAS into sandbox, issue normal in-game load input sequence. | **Safe & Complete:** Guarantees all runtime heap, D3D resources, and actor tables reinitialize cleanly through engine code. |
| **Tier 2: Certified Prefix Replay** | Free-roam house, customer arrival | Replay certified deterministic input prefix from nearest Tier 1 node under pinned RNG and phase. | **Deterministic:** Reproduces exact heap and resource states without native state-injection side-effects. |
| **Tier 3: In-Engine State Snapshot (Live-Probe)** | Interactive live exploration | Snapshot `DAT_044e2c88` working arena + `DAT_056da1d8` actor tables via `openrecet` probe. | **Exploratory Only:** Allowed for scout and live probe; forbidden for authoritative EP-05 proof certification until validated via Tier 1/2. |

---

## 5. Node Equivalence & Cyclic Graphs

Game loops naturally create cycles (e.g. daily loops, menu navigation, shop customer queues). The Atlas distinguishes **syntactic cycles** from **state identity**:

1. **Different RNG $\implies$ Distinct Nodes:** If the player returns to `HOUSE_FREEROAM` on Day 1 at 10:00 AM, but the RNG state has advanced from `0x12345678` to `0x87654321`, the graph produces two distinct Nodes $N_1 \neq N_2$.
2. **Normalized Loops:** When an edge pins or resets the RNG (e.g., entering a deterministic mini-game or using `{rngseed}`), and the state roots match, the destination Node references an existing `node_id`, closing a cycle.
3. **Graph Traversal Safety:** Traversal runners and schedulers track visited `(node_id, edge_id)` pairs to detect unbounded loops and terminate with `CYCLE_DETECTED`.

---

## 6. Canonical Acceptance Examples

The Behavior Atlas schema and identity models are validated against the seven canonical interaction patterns:

### Example 1: Menu Choice (Title $\to$ Options $\to$ Title)
- $N_0$: `(anchor="BOOT", state=null)`
- $E_{0\to 1}$: Input `[DOWN, DOWN, A]`, completion `anchor_reached:OPTIONS_MENU_READY`.
- $N_1$: `(anchor="OPTIONS_MENU_READY", submenu_state=2)`.
- $E_{1\to 0}$: Input `[B]`, completion `anchor_reached:TITLE_MENU_READY`.
- $N_2$: `(anchor="TITLE_MENU_READY", submenu_state=0)`.

### Example 2: Shop Sale (Customer Offer $\to$ Accept)
- $N_0$: `(anchor="CUSTOMER_SERVICE_ENTER", kyaku_index=1, ask=1000, gold=5000)`.
- $E_{0\to 1}$: Input `[A, A]`, completion `state_predicate:cc08==0`.
- $N_1$: `(anchor="HOUSE_FREEROAM", gold=6000, items_sold+=1)`.

### Example 3: Day Transition (Day 1 Evening $\to$ Day 2 Morning)
- $N_0$: `(anchor="DAY_END_REST", card_day=1, time=3)`.
- $E_{0\to 1}$: Input `[A]`, completion `anchor_reached:MORNING_NEWS_READY`.
- $N_1$: `(anchor="MORNING_NEWS_READY", card_day=2, time=0, news_generated=true)`.

### Example 4: Dungeon Branch (Hallway $\to$ Combat $\to$ Exit)
- $N_0$: `(anchor="DUNGEON_FLOOR_START", floor=1, hp=100)`.
- $E_{0\to 1}$: Input `[RIGHT*60, ATTACK*3]`, completion `anchor_reached:DUNGEON_STAIRS_FOUND`.
- $N_1$: `(anchor="DUNGEON_STAIRS_FOUND", floor=1, enemy_count=0, exp+=50)`.

### Example 5: Death / Retry
- $N_0$: `(anchor="DUNGEON_COMBAT", hp=10)`.
- $E_{0\to 1}$: Input `[WAIT*60]`, completion `anchor_reached:DUNGEON_WIPE_OUT`.
- $N_1$: `(anchor="HOUSE_FREEROAM", hp=1, inventory_lost=true, time=3)`.

### Example 6: Save / Reload
- $N_0$: `(anchor="SAVE_PICKER_READY", slot=0)`.
- $E_{0\to 1}$: Input `[A, A]`, completion `save_committed:0`.
- $N_1$: `(anchor="SAVE_PICKER_READY", persistent_state_root="a1b2c3...", playtime_pinned=true)`.

### Example 7: Daily Economy Loop
- Sequence of certified edges $E_{\text{morning}} \to E_{\text{open\_shop}} \to E_{\text{customers}} \to E_{\text{advance\_time}} \to E_{\text{sleep}}$, returning to morning state with strictly monotonic `card_day` and updated `gold`/`closeness` state roots.

---

## 7. Coverage-Guided Exploration Scheduler (BA-05)

The **Coverage-Guided Scheduler** (`tools/atlas/scheduler.py`, `tools/behavior_atlas.py explore`) navigates the Behavior Atlas frontier to discover and verify new paths, rare branches, and unexercised code blocks.

### 7.1 Multi-Factor Frontier Scoring
For each candidate grammar action from an active Node, the scheduler evaluates:

$$\text{Score} = \Big(\text{Novelty} + \text{CoveragePotential} + \text{RareBranch} + \text{DebtUnblock}\Big) \times \gamma^{\text{depth}} - \Big(\text{VisitCount} \times \lambda_{\text{visit}}\Big)$$

Where:
- **Novelty:** Reward for unvisited `(node_id, action_name)` transitions ($+5.0$).
- **Coverage Potential:** Estimated new basic blocks and transitions based on the Coverage Atlas ($+10.0 \times \Delta_{\text{blocks}}$).
- **Rare Branch:** Incentive for reaching target rare anchors or outcomes (e.g. `OPTIONS_MENU_READY`, `SAVE_PICKER_READY`, `BARGAIN_PINNED`) ($+8.0$).
- **Debt Unblock:** Bonus for exercising subsystems with active `PORT-DEBT` tags ($+6.0$).
- **Depth Decay:** Geometric penalty $\gamma^{\text{depth}}$ ($\gamma = 0.95$) preventing runaway linear depth before exploring breadth.
- **Visit Penalty:** Linear penalty $\lambda_{\text{visit}} \times \text{visits}$ preventing search traps in cyclic loops.

### 7.2 Dual-Execution Guidance & Divergence Halting
1. **Retail-First Reachability:** Explores candidate actions in the reference retail environment to verify reachability and record ground-truth transitions.
2. **Port Verification:** Executes the corresponding action sequence on the port.
3. **Early-Stopping on Divergence:** At the first state, volatile root, or proof divergence, exploration halts immediately, logging the full divergence signature and reproducing trajectory.

---

## 8. Hierarchical Trace Minimizer (BA-06)

The **Trace Minimizer** (`tools/atlas/minimizer.py`, `tools/behavior_atlas.py minimize`) applies hierarchical delta-debugging to compress scenario traces and bug reproductions to their minimal canonical form.

### 8.1 Minimization Stages
1. **Action Chunk Elimination:** Removes coarse semantic action segments that do not contribute to the target failure or completion condition.
2. **Wait Frame Reduction:** Binary-search minimizes idle wait frame intervals between actions down to the lowest functional bound.
3. **Repeated Input Coalescing:** Compresses multi-frame held button masks to minimal pulse durations.
4. **Frame-Level Delta-Debugging:** Runs 1-minimal delta-debugging over fine-grained input events.

### 8.2 Invariants and Determinism
- **Signature Matching:** Minimization preserves the exact `DivergenceSignature` (error kind, divergent field, expected vs actual values).
- **Flakiness Gating:** Repeat evaluations verify determinism; non-deterministic outcomes yield verdict `INCONCLUSIVE` rather than accepting a corrupt or false minimum.

---

## 9. RNG Callsite Map and Seed Solver (BA-07)

The **RNG Callsite Map and Seed Solver** (`tools/atlas/rng_solver.py`, `tools/behavior_atlas.py rng-map`, `solve-seed`) models the exact 32-bit MSVC LCG stream (`FUN_005041f6`, `DAT_006023a0`):

### 9.1 LCG Mathematical Model
- **Forward Step:** $S_{n+1} = (S_n \times \text{0x343fd} + \text{0x269ec3}) \pmod{2^{32}}$
- **15-Bit Output:** $\text{val}_{15} = (S_{n+1} \gg 16) \ \& \ \text{0x7fff}$
- **Backward Inversion:** Using modular inverse $A^{-1} = \text{0xb9b33155}$:
  $$S_{n-1} = \big((S_n - \text{0x269ec3}) \times \text{0xb9b33155}\big) \pmod{2^{32}}$$
- **Arbitrary Jump ($O(\log k)$):** Matrix doubling exponentiation computes $S_{n+k}$ or $S_{n-k}$ in logarithmic time.

### 9.2 Semantic Callsite Registry
Maps return addresses to consumer categories:
- `0x00460a1a`: Customer greeting/reaction dialogue variant (`val % 2 == 0` vs `1`).
- `0x00460672`: Haggling price tolerance acceptance threshold.
- `0x00460f16`: Customer pushback counter-offer speech bubble.
- `0x0046f621`: Background window NPC spawn positioning.
- `0x0047019f`: Chibi in-shop browsing NPC movement cadence.
- `0x0048a833`: Tear wing-glow sparkle particle jitter.
- `0x00451790`: Pre-reseed cold boot particle initialization.
- `0x00476320` / `0x00477810`: Dungeon monster spawn and drop roll tables.

---

## 10. Multi-Dimensional Atlas Health & Integrity (BA-08)

The **Atlas Health Reporter** (`tools/atlas/health.py`, `tools/behavior_atlas.py health`) enforces multi-dimensional progress tracking without masking gaps behind aggregate percentages:
1. **Edge Status Matrix:** Proven (certified with EP-05 proof bundles) vs Untested vs Divergent vs Flaky.
2. **Topological Integrity:** Disconnected components, unreachable nodes from entry points, terminal node traps, cycle structures.
3. **Scene Grammar Coverage:** Percentage of grammar actions verified with proven transitions.
4. **Proof Provenance & Gap Ledger:** Identifies active scenarios missing proof bundles.
5. **Configuration Matrix:** Environmental profile distribution (`reference-1024-windowed`, etc.).

---

## 11. CLI Reference

```sh
# Initialize Behavior Atlas store
nix develop --command python3 tools/behavior_atlas.py init

# Import scenarios corpus into atlas graph
nix develop --command python3 tools/behavior_atlas.py import-scenarios

# Run coverage-guided behavior exploration
nix develop --command python3 tools/behavior_atlas.py explore --max-iterations 100 --max-depth 10

# Hierarchically minimize a failure trace
nix develop --command python3 tools/behavior_atlas.py minimize --scenario house-pause-save-commit --out /tmp/min.jsonl

# Inspect engine RNG callsites and semantic consumer map
nix develop --command python3 tools/behavior_atlas.py rng-map

# Solve seed advance for target dialogue or haggling predicate
nix develop --command python3 tools/behavior_atlas.py solve-seed --mod 2 --target-val 0 --draws 2

# Generate full multi-dimensional Behavior Atlas health report
nix develop --command python3 tools/behavior_atlas.py health
```
