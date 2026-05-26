// calls_to_addr.js — count calls into a specific engine address.
//
// Validates that address-keyed TTD.Calls(va) works in the absence of
// PDB symbols.  Pattern queries ("module!*") need symbols loaded
// to expand the wildcard; an integer address does not.
//
// Globals:
//   TARGET_VA  (required)   integer address to query

"use strict";

function invokeScript() {
    var va = (typeof TARGET_VA !== "undefined") ? TARGET_VA : 0x00400000;
    var session = host.namespace.Debugger.Sessions.First();
    var ttd = session.TTD;

    var n = 0;
    var first = null;
    var ok = false;
    var err = "";

    try {
        var calls = ttd.Calls(va);
        for (var c of calls) {
            if (first === null) {
                first = {};
                try { first.time_seq = Number(c.TimeStart.Sequence); } catch (_) {}
                try { first.ret_va   = Number(c.ReturnAddress);     } catch (_) {}
            }
            n++;
            if (n >= 1000) break;   // cap so we don't enumerate huge sets
        }
        ok = true;
    } catch (e) {
        err = String(e);
    }

    var fs = host.namespace.Debugger.Utility.FileSystem;
    var fh = fs.CreateFile(TTD_OUTPUT_PATH, "CreateAlways");
    var tw = fs.CreateTextWriter(fh, "Utf8");
    tw.WriteLine(JSON.stringify({
        records: [],
        ok:      ok,
        target_va: va,
        n_calls: n,
        first:   first,
        error:   err,
    }));
    try { tw.Close(); } catch (_) {}
    fh.Close();
}
