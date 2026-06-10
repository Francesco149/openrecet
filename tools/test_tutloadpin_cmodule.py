#!/usr/bin/env python3
"""The {tutloadpin} worker-tail blocker is a CModule compiled by frida's
embedded TinyCC INSIDE the retail process — a parse error there only surfaces
mid-capture (cost a full recapture cycle on 2026-06-10: TinyCC rejects
`__stdcall`). This test extracts the CModule source verbatim from
openrecet-agent.js and compiles it via a local frida attach, so an agent edit
that breaks the C fails here, not on the capture host.

The parser is shared across frida platforms; only codegen differs — a local
Linux target validates the Windows-x86 capture path's parse.

Run: nix develop --command python3 tools/test_tutloadpin_cmodule.py
"""
import json
import subprocess
import sys
import time
from pathlib import Path

import frida

AGENT = Path(__file__).parent / "frida" / "openrecet-agent.js"


def extract_cmodule_source() -> str:
    src = AGENT.read_text()
    marker = "const cm = new CModule(`"
    i = src.index(marker) + len(marker)
    j = src.index("`", i)
    return src[i:j]


def main() -> int:
    cmodule_src = extract_cmodule_source()
    child = subprocess.Popen(["sleep", "30"])
    result: list = []
    try:
        sess = frida.attach(child.pid)
        script = sess.create_script("""
const cmsrc = %s;
const flags = Memory.alloc(8);
const cell1 = Memory.alloc(Process.pointerSize);
const cell2 = Memory.alloc(Process.pointerSize);
try {
  const cm = new CModule(cmsrc, {tlp_flags: flags, tlp_yield: cell1, tlp_tick: cell2});
  send('CMODULE-COMPILE-OK onEnter=' + cm.onEnter);
} catch (e) {
  send('CMODULE-COMPILE-FAIL: ' + e.message);
}
""" % json.dumps(cmodule_src))
        script.on("message", lambda m, d: result.append(str(m)))
        script.load()
        time.sleep(1)
    finally:
        child.kill()

    for m in result:
        print(m)
    ok = any("CMODULE-COMPILE-OK" in m for m in result)
    print("OK: tutloadpin CModule compiles" if ok
          else "FAIL: tutloadpin CModule did not compile")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
