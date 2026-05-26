// frame_calls.js — enumerate calls into the engine module range.
//
// Globals pinned by the python wrapper at load time:
//   TTD_OUTPUT_PATH           Windows path for the JSON output file
//   MODULE_LO   (optional)    inclusive lower bound, default 0x00400000
//   MODULE_HI   (optional)    exclusive upper bound, default 0x00800000
//   MAX_RECORDS (optional)    cap on records written, default 200000
//
// Output JSON: array of {target_va, ret_va, time_seq} objects, one
// per engine→engine call.  External module calls (d3d8, dinput8,
// kernel32, etc.) are filtered out before write.

"use strict";

function invokeScript() {
    var lo = (typeof MODULE_LO !== "undefined") ? MODULE_LO : 0x00400000;
    var hi = (typeof MODULE_HI !== "undefined") ? MODULE_HI : 0x00800000;
    var cap = (typeof MAX_RECORDS !== "undefined") ? MAX_RECORDS : 200000;

    var session = host.namespace.Debugger.Sessions.First();
    var ttd = session.TTD;

    var out = [];
    var skipped = 0;
    var n = 0;

    // TTD.Calls("*!*") enumerates every recorded call.  Without
    // public symbols the function-name field is empty, but the
    // numeric address fields are populated, which is all we need.
    var calls;
    try {
        calls = ttd.Calls("*!*");
    } catch (e) {
        // narrow pattern fallback — some WinDbg builds reject "*!*"
        calls = ttd.Calls("");
    }

    for (var c of calls) {
        var fn = c.Function;
        var fnNum = (typeof fn === "number") ? fn :
                    (fn && fn.address) ? Number(fn.address) :
                    Number(fn);
        if (!(fnNum >= lo && fnNum < hi)) {
            skipped++;
            continue;
        }

        var rec = {
            target_va: fnNum,
        };

        // try to surface time-position + return-address when present.
        try { rec.time_seq = Number(c.TimeStart.Sequence); } catch (_) {}
        try { rec.ret_va   = Number(c.ReturnAddress);     } catch (_) {}

        out.push(rec);
        n++;
        if (n >= cap) break;
    }

    var fs = host.namespace.Debugger.Utility.FileSystem;
    var fh = fs.CreateFile(TTD_OUTPUT_PATH, "CreateAlways");
    var tw = fs.CreateTextWriter(fh, "Utf8");
    tw.WriteLine(JSON.stringify({
        records: out,
        kept:    n,
        skipped: skipped,
        module_lo: lo,
        module_hi: hi,
    }));
    tw.Close();
    fh.Close();
}
