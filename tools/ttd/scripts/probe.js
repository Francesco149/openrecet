// probe.js — minimal cdb scriptload validator.
//
// Writes a constant JSON object to TTD_OUTPUT_PATH.  No engine
// queries, no TTD API calls — just confirms that cdb.exe loads
// the JS provider, runs invokeScript(), and can write to the
// designated output file.  If this fails, the issue is in the
// harness wiring (cdb arg quoting, FileSystem permissions, JS
// provider availability) and NOT in any specific TTD API.

"use strict";

function invokeScript() {
    var fs = host.namespace.Debugger.Utility.FileSystem;
    var fh = fs.CreateFile(TTD_OUTPUT_PATH, "CreateAlways");
    var tw = fs.CreateTextWriter(fh, "Utf8");
    tw.WriteLine(JSON.stringify({
        records: [],
        probe:   "ok",
        ttd_output_path: TTD_OUTPUT_PATH,
    }));
    tw.Close();
    fh.Close();
}
