# Observed Call Capsules and Memory Model (CC-00)

> **Status:** ADOPTED 2026-08-22 (roadmap `docs/plans/parity-evidence-roadmap.md` §11 Workstream CC).
> **Owner:** R3/highest-reasoning orchestrator.
> **Schema:** [`../schemas/call-capsule-v1.json`](../schemas/call-capsule-v1.json).
> **Tooling:** `tools/parity/capsule.py`, `tools/diff_test.py`.
> **Vocabulary:** [`parity-vocabulary.md`](parity-vocabulary.md).

---

## 1. Architectural Role & Motivation

A **Call Capsule** is a self-contained, content-addressed snapshot of a single function invocation in the reference executable (`recettear.unpacked.exe`). It captures:
1. **Invocation Envelope:** Calling convention, CPU/FPU registers, stack arguments, and pointed-object graphs.
2. **Prestate Snapshot:** Relevant global variables, working arena fields, and RNG state.
3. **Execution Effects:** Return value, output registers, ordered sequence of memory writes, and nested external dispatches.
4. **Poststate Snapshot:** Mutated globals, pointed objects, and RNG stream advances.

```text
+-------------------------------------------------------------------------------+
|                             Call Capsule (CC-00)                              |
|                                                                               |
|  [Inputs]                                                    [Outputs]        |
|  - ABI (cdecl/stdcall/thiscall)                              - Return Value   |
|  - Stack Args & Registers                                    - Registers Out  |
|  - Pointed Objects (Relocated)  ===[ Host Replay Execution ]===> - Ordered Writes |
|  - Prestate (Globals & RNG)                                  - Poststate      |
|                                                              - External Calls |
+-------------------------------------------------------------------------------+
```

### Why Capsules Are Necessary
- **Unit Parity at Scale:** End-to-end frame-diffing catches whole-system divergences, but localizing fine-grained logic bugs requires function-level differential replay.
- **Hybrid Validation (CC-05):** Allows swapping a single ported C function into frozen or live retail memory to prove 1:1 drop-in compatibility.
- **Relocation Independence:** Separates absolute Win32 memory addresses (`0x00400000`..`0x09000000`) from pure behavioral logic so test fixtures run natively on host Linux under AddressSanitizer and UndefinedBehaviorSanitizer.

---

## 2. Supported Calling Conventions (x86-32)

| ABI | Register Usage | Stack Cleanup | Return Value Location | Engine Usage Examples |
|---|---|---|---|---|
| **`cdecl`** | Caller-saved: EAX, ECX, EDX. Callee-saved: EBX, ESI, EDI, EBP. | **Caller** cleans stack. Arguments pushed right-to-left. | Integer/Pointer in `EAX`, Float in `ST(0)`. | Standard CRT functions, mathematical utilities, rendering routines (`FUN_00404efc`). |
| **`stdcall`** | Caller-saved: EAX, ECX, EDX. Callee-saved: EBX, ESI, EDI, EBP. | **Callee** cleans stack via `ret N`. Arguments pushed right-to-left. | Integer/Pointer in `EAX`, Float in `ST(0)`. | Win32 API callbacks, window procedures, audio stream callbacks. |
| **`thiscall`** | `ECX` holds `this` pointer (or pushed first in standard MSVC member functions). | **Callee** cleans stack. | Integer/Pointer in `EAX`, Float in `ST(0)`. | Object-oriented state machines, GUI widgets, Direct3D COM method invocations. |
| **`fastcall`** | `ECX` = Arg 1, `EDX` = Arg 2. Remaining args pushed on stack. | **Callee** cleans stack. | Integer/Pointer in `EAX`, Float in `ST(0)`. | High-frequency inner-loop helpers (texture hashing, fast clamping). |

---

## 3. x87 FPU State & Bit Preservation

Conforming to the project's strict x87 arithmetic preservation policy (`docs/PLAN.md` §3):
1. **No Epsilon Tolerance:** Floating-point outputs and registers (`st0`..`st7`) are compared by exact IEEE-754 32-bit (`f32`) or 64-bit (`f64`) bit patterns.
2. **FPU Stack Depth:** Functions returning floating-point values push the result to `ST(0)` and pop intermediate operands cleanly, leaving FPU top-of-stack unchanged.
3. **Control Word Consistency:** Host test adapters configure the x87 FPU control word (`_FPU_EXTENDED` / 80-bit internal precision) to match MSVC CRT runtime expectations.

---

## 4. Pointed-Object Memory Model & Relocation Maps

When a function accepts pointer arguments (e.g. `dst*`, `actor*`, `item*`), the memory model avoids capturing unconstrained arbitrary pointers:

### 4.1 Bounded Object Graphs
- **Extent Sizing:** Each pointed object records an explicit `size_bytes` extent.
- **Relocation Descriptors:** Nested pointers inside struct members are mapped via relative offsets to target object IDs in the capsule's `relocation_map`.
- **Sandbox Materialization:** On host replay, each object is allocated in an isolated heap sandbox; pointer arguments are dynamically adjusted to point into the sandbox buffer.

### 4.2 Ordered Memory Writes
All in-place struct or buffer mutations are recorded as sequential write tuples:
$$\text{Write}_i = (\text{seq}_i, \text{reloc\_offset}, \text{old\_value}, \text{new\_value}, \text{type}, \text{owner\_va})$$

Replay verifies that the host implementation executes identical mutations in the exact same sequence.

---

## 5. Canonical Acceptance Categories & Fixtures

The Call Capsule schema and engine support the five required architectural categories:

### Category 1: Pure Leaf Function
- **Target:** `FUN_00431990` (`boss_id_allowed`).
- **Signature:** `int __cdecl boss_id_allowed(int boss_id, int dungeon_mode)`.
- **Characteristics:** Reads only stack arguments; no global variables read or written; returns integer result (`1` or `0`).
- **Capsule Invariant:** `prestate == {}`, `ordered_writes == []`, `poststate == {}`.

### Category 2: Known Global Variable Access
- **Target:** `FUN_0043195d` (`floor_is_checkpoint`).
- **Signature:** `int __cdecl floor_is_checkpoint(int floor_num)`.
- **Characteristics:** Reads global checkpoint flags `DAT_00438bf20` and `DAT_00438bf24`; returns boolean.
- **Capsule Invariant:** `prestate` records snapshot of globals; `poststate` verifies globals remain unmutated (read-only access).

### Category 3: In-Place Struct Mutation
- **Target:** `FUN_00404efc` (`render_quad_add`).
- **Signature:** `void __cdecl render_quad_add(Rect* dst, Rect* src, Texture* tex, uint32_t diffuse)`.
- **Characteristics:** Mutates `dst->w` and `dst->h` in-place, writes quad vertex buffer at `DAT_00647e0c`.
- **Capsule Invariant:** `pointed_objects["dst"]` records old vs new struct bytes; `ordered_writes` tracks sequential field assignments.

### Category 4: RNG State Consumer
- **Target:** `FUN_005041f6` (`rng_next15`).
- **Signature:** `uint16_t __cdecl rng_next15(void)`.
- **Characteristics:** Reads and mutates the global LCG seed `DAT_006023a0`; returns 15-bit pseudo-random integer.
- **Capsule Invariant:** `prestate["rng"] = S_n`, `return_val = (S_{n+1} >> 16) & 0x7fff`, `poststate["rng"] = S_{n+1}`.

### Category 5: Unsupported OS-Coupled Call
- **Target:** `Win32 GetCursorPos` or file handle operations.
- **Characteristics:** Functions directly coupled to Win32 kernel handles or asynchronous device states.
- **Capsule Invariant:** Classified as `unsupported_os_call`; validation engine flags `UNSUPPORTED_OS_CALL` and excludes from pure host replay.

---

## 6. Capsule Identity Preimage

The `capsule_id` is a 64-hex SHA-256 digest computed over the canonical JSON encoding of all deterministic input components:

$$\text{capsule\_id} = \text{SHA256}\Big(\text{canonical}\big(\text{target\_va}, \text{abi}, \text{category}, \text{registers\_in}, \text{stack\_args}, \text{pointed\_objects}, \text{prestate}, \text{provenance}\big)\Big)$$

Two capsules with identical `capsule_id` represent provably identical input and prestate conditions.
