// present_boundaries.js — find every IDirect3DDevice8::Present call.
//
// Each Present is a frame boundary.  Enumerating them gives us the
// time positions we need to bracket per-frame call lists.  Cheap
// query (small number of total calls — ~60/s of game time).
//
// Pattern fallback chain: try several spellings since the symbol
// resolution for d3d8.dll varies by Windows version.
//
// Output: array of {time_seq, time_steps, ret_va} per Present call.

"use strict";

function invokeScript() {
    var session = host.namespace.Debugger.Sessions.First();
    var ttd = session.TTD;

    var patterns = [
        "d3d8!CDevice::Present",
        "d3d8!Present",
        "d3d8!*Present*",
    ];

    var matched_pattern = null;
    var calls = null;
    for (var p of patterns) {
        try {
            var probe = ttd.Calls(p);
            // force enumeration of one element to confirm the pattern
            // resolves; some patterns return empty iterators.
            for (var c of probe) {
                matched_pattern = p;
                calls = ttd.Calls(p);
                break;
            }
            if (matched_pattern) break;
        } catch (e) {
            // pattern didn't resolve in this WinDbg build — try next
        }
    }

    var out = [];
    if (calls) {
        for (var c of calls) {
            var rec = {};
            try { rec.time_seq   = Number(c.TimeStart.Sequence); } catch (_) {}
            try { rec.time_steps = Number(c.TimeStart.Steps);    } catch (_) {}
            try { rec.ret_va     = Number(c.ReturnAddress);      } catch (_) {}
            out.push(rec);
        }
    }

    var fs = host.namespace.Debugger.Utility.FileSystem;
    var fh = fs.CreateFile(TTD_OUTPUT_PATH, "CreateAlways");
    var tw = fs.CreateTextWriter(fh, "Utf8");
    tw.WriteLine(JSON.stringify({
        records:         out,
        matched_pattern: matched_pattern,
        tried_patterns:  patterns,
    }));
    try { tw.Close(); } catch (_) {}
    fh.Close();
}
