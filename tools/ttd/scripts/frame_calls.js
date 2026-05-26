// frame_calls.js — enumerate calls into the engine module.
//
// Globals pinned by the python wrapper at load time:
//   TTD_OUTPUT_PATH                  Windows path for the JSON output
//   MODULE_PATTERN (optional)        TTD.Calls pattern; default
//                                    "recettear_unpacked!*".  Underscore
//                                    is what cdb's symbol parser uses
//                                    after sanitising the dot in
//                                    "recettear.unpacked.exe".
//   MAX_RECORDS    (optional)        cap on records written, default 200000
//
// Output JSON: array of {target_va, ret_va, time_seq} objects, one
// row per matched call.  The pattern is intentionally narrow so cdb's
// native indexer filters at scan time; "*!*" full-scan would timeout
// on a multi-hundred-MB trace.

"use strict";

function invokeScript() {
    var pattern = (typeof MODULE_PATTERN !== "undefined")
        ? MODULE_PATTERN : "recettear_unpacked!*";
    var cap = (typeof MAX_RECORDS !== "undefined") ? MAX_RECORDS : 200000;

    var session = host.namespace.Debugger.Sessions.First();
    var ttd = session.TTD;

    var out = [];
    var n = 0;
    var truncated = false;

    var calls = ttd.Calls(pattern);
    for (var c of calls) {
        var fn = c.Function;
        var fnNum = (typeof fn === "number") ? fn :
                    (fn && fn.address) ? Number(fn.address) :
                    Number(fn);
        var rec = { target_va: fnNum };
        try { rec.time_seq = Number(c.TimeStart.Sequence); } catch (_) {}
        try { rec.ret_va   = Number(c.ReturnAddress);     } catch (_) {}
        out.push(rec);
        n++;
        if (n >= cap) { truncated = true; break; }
    }

    var fs = host.namespace.Debugger.Utility.FileSystem;
    var fh = fs.CreateFile(TTD_OUTPUT_PATH, "CreateAlways");
    var tw = fs.CreateTextWriter(fh, "Utf8");
    tw.WriteLine(JSON.stringify({
        records:   out,
        kept:      n,
        truncated: truncated,
        pattern:   pattern,
    }));
    try { tw.Close(); } catch (_) {}
    fh.Close();
}
