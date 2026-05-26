# tools/ttd — record + query harness

A thin Python wrapper around the Windows-host recorder + headless
debugger.  Every invocation redirects verbose tool output to a log
file and returns a single line of JSON to stdout.  The structured
result is the only thing a caller (human or automated) needs to act
on.  The log file is yours to inspect manually if a failure status
shows up.

## Layout

```
tools/ttd/
├── ttd_paths.py            binary discovery (returns {ttd_exe, cdb_exe})
├── ttd_capture.py          record a trace
├── ttd_query.py            run a JS query against a trace
├── _run_elevated.ps1       PowerShell elevation wrapper used by capture
└── scripts/
    ├── probe.js            sanity: writes a constant JSON to validate
                            the cdb + .scriptrun + file-write path
    ├── modules.js          dump loaded modules + base/end VAs
    ├── calls_to_addr.js    count calls to ONE VA (+ first call site)
    ├── batch_calls.js      count + caller distribution for an array of VAs
                            (the leaf-first analysis primitive)
    ├── present_boundaries.js  enumerate Present() calls (BLOCKED — needs
                                d3d8 public symbols which aren't loaded)
    └── frame_calls.js      enumerate by module pattern (BLOCKED — retail
                              has no PDB so "recettear!*" returns empty)
```

## Working call enumeration: address-keyed

cdb's `TTD.Calls()` accepts either a `"module!symbol"` pattern OR a
raw integer address.  Patterns require symbols (PDBs); retail ships
none.  Address queries don't need symbols and are natively indexed
by cdb — fast (sub-10s for one VA on a 350 MB trace).

The leaf-first workflow:

  1. Capture a trace via `ttd_capture.py`.
  2. Produce a list of engine function entry-point VAs.  Two
     candidate sources: `objdump -d vendor/unpacked/recettear.unpacked
     .exe | grep '^[0-9a-f]\\{8\\} <FUN_'` (if symbols are exported),
     or a Ghidra headless export.
  3. Run `batch_calls.js` with `TARGET_VAS=[v1, v2, …]` to get
     per-VA call counts + caller distributions.
  4. Run the equivalent analysis on the port (using `nm openrecet
     .exe` for the VA list).
  5. Diff retail vs port call-graph metadata.  Mismatches surface as:
       a function called in retail but not port (missing port impl),
       different call counts (logic divergence),
       different caller distributions (call-site divergence).

## One-time setup (manual)

1. Install via winget on the Windows host (already done if you're
   here): `winget install --id Microsoft.WinDbg ...`.
2. Smoke-test discovery (safe, no tool invocation, just path lookup):
   ```fish
   nix develop --command python3 tools/ttd/ttd_paths.py
   ```
   Expected single-line JSON with `"status": "ok"` + `ttd_exe` +
   `cdb_exe` populated.  If `failed`, set the env-var overrides
   (`OPENRECET_TTD_EXE`, `OPENRECET_CDB_EXE`) to absolute Windows
   paths and re-run.

## Recording a trace

```fish
nix develop --command python3 tools/ttd/ttd_capture.py \
    --scenario boot-idle \
    --wall-s 4
```

Default target is `vendor/unpacked/recettear.unpacked.exe`; default
run-dir is `runs/ttd-<scenario>-<UTC-timestamp>/`.

On success: `{"status":"ok","trace_path":"…/trace.run","size_mb":N,
"elapsed_s":N,"log_path":"…/cdb.log"}`.

On failure: `{"status":"failed","stage":"<label>","log_path":"…"}`
— inspect the log yourself, fix the issue, re-run.  Failure stages
in `ttd_capture.py`'s module docstring.

## Querying a trace

```fish
nix develop --command python3 tools/ttd/ttd_query.py \
    --trace runs/ttd-boot-idle-20260526T180000Z/trace.run \
    --script frame_calls.js
```

Defaults the output JSON to `<run_dir>/frame_calls.json`.  On
success the returned `n_records` is the count of engine-range calls
written.  The output JSON has shape:

```json
{
  "records": [{"target_va": 4204880, "ret_va": 4214016, "time_seq": 1234}, …],
  "kept": N, "skipped": M,
  "module_lo": 4194304, "module_hi": 8388608
}
```

`target_va` and `ret_va` are decimal integers (add `0x00000000` and
compare in hex to map to Ghidra addresses — they're already
absolute VAs, no module-base subtraction needed).

### Pinning per-invocation JS globals

`--extra-global KEY=JSONVALUE` pins additional globals before the
script loads.  Values are JSON-encoded — strings need quotes.

```fish
# narrow the engine address range
python3 tools/ttd/ttd_query.py --trace … --script frame_calls.js \
    --extra-global MODULE_LO=4198400 \
    --extra-global MODULE_HI=8000000 \
    --extra-global MAX_RECORDS=500000
```

## Adding a query

Drop a new `.js` file under `tools/ttd/scripts/`.  Required shape:

```js
"use strict";
function invokeScript() {
    // TTD_OUTPUT_PATH is pre-pinned by the python wrapper to the
    // absolute Windows path of the output file.
    var session = host.namespace.Debugger.Sessions.First();
    // …query whatever you want from session.TTD…
    var records = [/* objects */];

    var fs = host.namespace.Debugger.Utility.FileSystem;
    var fh = fs.CreateFile(TTD_OUTPUT_PATH, "CreateAlways");
    var tw = fs.CreateTextWriter(fh, "Utf8");
    tw.WriteLine(JSON.stringify({records: records}));
    tw.Close();
    fh.Close();
}
```

Then invoke via `--script <your_file>.js`.  The python harness will
auto-discover the script under `scripts/`, pin the output path,
spawn the debugger, and report the structured result.

## Why this design

Every stdout/stderr byte from the recorder + debugger goes to a log
file the harness never reads.  All "result of doing the thing" data
flows through the structured JSON output file, which is written by
our own scripts in a format we control.  This keeps callers' input
streams clean — no verbose tool banter ever lands in a downstream
consumer's context.
