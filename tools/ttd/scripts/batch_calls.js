// batch_calls.js — for each VA in TARGET_VAS, count calls + capture
// the call-site distribution.
//
// This is the leaf-first analysis primitive.  Given a list of engine
// function entry-point VAs (from objdump on the unpacked binary, or
// from a Ghidra function export), this script answers per-VA:
//   - was the function called during the recording?
//   - how many times?
//   - from which return-addresses (call sites)?
//
// Globals:
//   TARGET_VAS   array of integer VAs to probe, OR an object of the
//                form {vas: [int, ...], ...} (the on-disk metadata
//                form at tools/ttd/data/engine_function_vas.json).
//   MAX_CALLS_PER_VA  (optional)  cap per-VA enumeration, default 200
//
// Output JSON: array of {target_va, n_calls, truncated, callers:
// [{ret_va, count, first_time_seq}]}.  callers[] is sorted by count
// descending so the hot call sites are first.

"use strict";

function invokeScript() {
    var cap = (typeof MAX_CALLS_PER_VA !== "undefined") ?
        MAX_CALLS_PER_VA : 5000;
    var raw = (typeof TARGET_VAS !== "undefined") ? TARGET_VAS : [];
    var vas;
    if (raw && typeof raw === "object" && typeof raw.length === "number") {
        vas = raw;
    } else if (raw && typeof raw === "object" && raw.vas) {
        vas = raw.vas;
    } else {
        vas = [];
    }

    var session = host.namespace.Debugger.Sessions.First();
    var ttd = session.TTD;

    var out = [];
    for (var i = 0; i < vas.length; i++) {
        var va = vas[i];
        var rec = { target_va: va, n_calls: 0, truncated: false, callers: [] };

        try {
            var calls = ttd.Calls(va);
            var callerMap = {};
            for (var c of calls) {
                rec.n_calls++;
                var retVa = 0;
                var timeSeq = 0;
                try { retVa  = Number(c.ReturnAddress);    } catch (_) {}
                try { timeSeq = Number(c.TimeStart.Sequence); } catch (_) {}
                var key = "" + retVa;
                if (!callerMap[key]) {
                    callerMap[key] = {
                        ret_va: retVa, count: 0,
                        first_time_seq: timeSeq,
                    };
                }
                callerMap[key].count++;
                if (rec.n_calls >= cap) { rec.truncated = true; break; }
            }
            // sort callers by count desc
            for (var k in callerMap) rec.callers.push(callerMap[k]);
            rec.callers.sort(function (a, b) { return b.count - a.count; });
        } catch (e) {
            rec.error = String(e);
        }

        out.push(rec);
    }

    var fs = host.namespace.Debugger.Utility.FileSystem;
    var fh = fs.CreateFile(TTD_OUTPUT_PATH, "CreateAlways");
    var tw = fs.CreateTextWriter(fh, "Utf8");
    tw.WriteLine(JSON.stringify({
        records:        out,
        n_vas_probed:   vas.length,
        max_per_va:     cap,
    }));
    // TextWriter exposes no Close()/Dispose() in cdb's data model; the
    // File handle's Close() is what flushes + releases.  Wrap defensively
    // in case a future host build adds one.
    try { tw.Close(); } catch (_) {}
    fh.Close();
}
