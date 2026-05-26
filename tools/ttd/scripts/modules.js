// modules.js — dump loaded modules with their address ranges.
//
// Output JSON: array of {name, base_va, end_va, size}.  Use this to
// discover the right module-name pattern for follow-up TTD.Calls()
// queries (which take "module!symbol"-style patterns and can use
// native indexing — much faster than a "*!*" full-scan).

"use strict";

function invokeScript() {
    var session = host.namespace.Debugger.Sessions.First();
    var proc = session.Processes.First();

    var out = [];
    for (var m of proc.Modules) {
        try {
            var base = Number(m.BaseAddress);
            var size = Number(m.Size);
            out.push({
                name:    String(m.Name),
                base_va: base,
                end_va:  base + size,
                size:    size,
            });
        } catch (e) {
            // skip any module we can't read
        }
    }

    var fs = host.namespace.Debugger.Utility.FileSystem;
    var fh = fs.CreateFile(TTD_OUTPUT_PATH, "CreateAlways");
    var tw = fs.CreateTextWriter(fh, "Utf8");
    tw.WriteLine(JSON.stringify({records: out}));
    try { tw.Close(); } catch (_) {}
    fh.Close();
}
