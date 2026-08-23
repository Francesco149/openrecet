# System Boundary Events and Equivalence Model (BT-00)

> **Status:** ADOPTED 2026-08-23 (roadmap `docs/plans/parity-evidence-roadmap.md` §12 Workstream BT).
> **Owner:** R3/highest-reasoning orchestrator.
> **Schema:** [`../schemas/boundary-event-v1.json`](../schemas/boundary-event-v1.json).
> **Tooling:** `tools/parity/boundary.py`, `tools/test_boundary_events.py`.
> **Vocabulary:** [`parity-vocabulary.md`](parity-vocabulary.md).

---

## 1. Architectural Role & Motivation

A **Boundary Event** is a structured, normalized observation of an interaction between the game engine and the host operating system / external subsystems (Win32 kernel, DirectInput, DirectSound/DirectMusic, filesystem, and windowing environment).

```text
+-------------------------------------------------------------------------------+
|                       System Boundary Interaction (BT-00)                     |
|                                                                               |
|  [Engine Logic] <=====> [Boundary Interceptor] <=====> [Host OS / Subsystem]  |
|                                |                                              |
|                         [Normalized Event]                                    |
|                         - Domain (win32, dinput, fs, ini, audio, win, mutex)  |
|                         - Logical Frame (anchor, occ, offset)                 |
|                         - API / Method Name                                   |
|                         - Normalized Object IDs & Arguments                   |
|                         - Return Code & Buffer SHA-256                        |
|                         - External Side Effects                               |
+-------------------------------------------------------------------------------+
```

### Why Boundary Analysis Is Necessary
- **Hardware & OS Agnosticism:** Unit state and frame diffs compare simulation math; boundary events ensure that device acquisition, focus handling, cooperative levels, file handles, and audio streams behave identically.
- **Root-Cause Isolation:** Differentiates between internal simulation divergence and external environment perturbation (e.g. lost device, cooperative acquisition retry loops, async thread races).
- **Zero Asset Leakage:** All file and audio payloads are referenced exclusively by relative paths, metadata, and cryptographic SHA-256 hashes, ensuring zero proprietary game assets are stored or committed.

---

## 2. Boundary Domains (7 Architectural Categories)

| Domain | Scope & Key APIs | Normalization Rule |
|---|---|---|
| **`win32_msg`** | Window message loop: `GetMessage`, `PeekMessage`, `DispatchMessage`, `DefWindowProc`, `WM_ACTIVATE`, `WM_SETFOCUS`, `WM_KILLFOCUS`, `WM_PAINT`, `WM_CLOSE`. | HWND normalized to `hwnd_main` / `hwnd_dialog`; message codes mapped to symbolic enum strings. |
| **`dinput_device`** | DirectInput 8 COM interfaces: `DirectInput8Create`, `CreateDevice`, `SetCooperativeLevel`, `SetDataFormat`, `Acquire`, `Unacquire`, `GetDeviceState`, `GetDeviceData`. | COM interface pointers mapped to `dinput_root`, `keyboard_dev`, `joystick_dev`. Device states mapped to normalized button masks. |
| **`filesystem_io`** | Win32 file APIs: `CreateFileA/W`, `ReadFile`, `WriteFile`, `SetFilePointer`, `CloseHandle`, `MoveFileA/W`, `DeleteFileA/W`, `GetFileAttributesA/W`, `GetFileSize`. | Absolute OS paths normalized to engine-relative paths (`data/item.txt`, `save.dat`); file buffers hashed with SHA-256. |
| **`ini_config`** | Configuration registry: `GetPrivateProfileIntA`, `GetPrivateProfileStringA`, `WritePrivateProfileStringA` over `recet.ini`. | INI sections and key names preserved; defaults and parsed scalar values normalized. |
| **`audio_device`** | DirectMusic / DirectSound interfaces: `IDirectMusicPerformance8::PlaySegmentEx`, `Stop`, `SetVolume`, `SetPan`, `CloseDown`, `IDirectSoundBuffer::Play`. | Audio paths mapped to logical channel IDs; volume values normalized to millibels / centibels. |
| **`window_lifecycle`** | Window management: `CreateWindowExA`, `ShowWindow`, `SetWindowPos`, `MoveWindow`, `DestroyWindow`, `AdjustWindowRect`. | Positions, styles, and dimensions extracted; coordinates normalized to client dimensions. |
| **`mutex_sync`** | Inter-process & thread synchronization: `CreateMutexA`, `OpenMutexA`, `ReleaseMutex`, `WaitForSingleObject`, `CreateThread`. | Mutex handles mapped to canonical mutex names (e.g. `MUTEX_RECETTEAR_SINGLETON`). |

---

## 3. Handle & Pointer Normalization Model

Raw OS handles and heap pointers vary across operating system versions and process executions. The normalization engine enforces:

1. **Handle Neutrality:**
   - Raw Win32 `HANDLE`, `HWND`, `HMODULE`, and COM `IUnknown*` values are replaced with deterministic logical identifiers (`handle:save_dat`, `hwnd:main_window`, `com:d3d_device`).
2. **Path Neutrality:**
   - All Windows drive roots (`C:\Program Files (x86)\...`), temporary folders, and WSL UNC paths (`\\wsl.localhost\NixOS\...`) are stripped to canonical relative paths (`data/item.txt`, `bgm/bgm09.mid`, `save.dat`).
3. **Payload Hashing:**
   - Any buffer read or written (such as save file data or texture bytes) records its byte count (`buffer_size`) and SHA-256 digest (`buffer_hash`), never raw binary bytes.

---

## 4. Three-Tier Equivalence Levels

The boundary comparison engine evaluates event streams under three rigorous equivalence standards:

```text
[Stream A (Retail)] vs [Stream B (Port)]
         |
         +--> 1. CALL_SEQUENCE_EQUIVALENT (Exact API call ordering, arguments, return codes)
         |
         +--> 2. RESULT_EQUIVALENT (Identical logical outcome across retry loops and non-fatal queries)
         |
         +--> 3. EFFECT_EQUIVALENT (Identical final OS-visible filesystem, audio, and window effects)
```

### Level 1: `CALL_SEQUENCE_EQUIVALENT`
- Both streams execute the exact same sequence of API calls in the exact same logical order with identical arguments and results.
- **Use Case:** High-fidelity lockstep validation of file parsing, initial device creation, and INI loading.

### Level 2: `RESULT_EQUIVALENT`
- Allows benign differences in internal polling frequency or retry loops (e.g., DirectInput re-acquire attempts after focus loss or multiple `PeekMessage` idle passes), provided the final return values and logical outcomes match.
- **Use Case:** Multi-poll game loop input polling and asynchronous worker thread queries.

### Level 3: `EFFECT_EQUIVALENT`
- Focuses exclusively on external side effects on the environment:
  - Filesystem: Identical files created, written, or modified with identical SHA-256 contents.
  - Audio: Identical BGM and SE segments triggered with matching volume levels and loop points.
  - Windowing: Identical final window state, client rectangle dimensions, and visibility flags.
- **Use Case:** Validating that platform-specific backend optimizations or async streaming refactors produce indistinguishable external behavior.

---

## 5. Stream Identity Preimage

Every boundary event stream computes a deterministic 64-hex SHA-256 `stream_id` computed over the canonical JSON encoding of its metadata and ordered events:

$$\text{stream\_id} = \text{SHA256}\Big(\text{canonical}\big(\text{scenario}, \text{target}, \text{events}[\dots], \text{provenance}\big)\Big)$$

Two streams with identical `stream_id` are provably identical in all boundary interactions.
