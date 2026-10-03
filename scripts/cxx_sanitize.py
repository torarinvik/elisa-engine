#!/usr/bin/env python3
"""Compiler wrapper that adds AddressSanitizer and UndefinedBehaviorSanitizer.

Point `CXX` at this executable to build the native probe instrumented without
changing the build orchestrator:

  CXX="$PWD/scripts/cxx_sanitize.py" elisascript scripts/wicked_probe.elisascript

`-fno-sanitize-recover=all` makes an undefined-behaviour report abort the
process, so a sanitizer finding turns into a nonzero exit status the runner
already relays. The native probe returns normally. On the current macOS SDK,
AddressSanitizer reports that leak detection is unsupported; use `leaks
-atExit` for macOS exit-time allocation reports.

`ELISA_SANITIZER=undefined` selects a UBSan-only build. The full graphics probe
runs successfully under both UBSan and ASan+UBSan with the SDL3-native Wicked
build; the boundary harness runs the same sanitizer pair over the smaller FFI
surface.

`sanitizers/wicked_ignorelist.txt` skips only the null check inside Wicked's
bundled metal-cpp headers, which call members through nil Objective-C objects.
"""

import os
import sys
from pathlib import Path

IGNORELIST = Path(__file__).resolve().parent / "sanitizers/wicked_ignorelist.txt"

SANITIZER_FLAVORS = {
    "address,undefined": ["-fsanitize=address,undefined"],
    "undefined": ["-fsanitize=undefined"],
}


def main() -> int:
    flavor = os.environ.get("ELISA_SANITIZER", "address,undefined")
    flags = SANITIZER_FLAVORS.get(flavor)
    if flags is None:
        print(f"unknown ELISA_SANITIZER '{flavor}'; use {', '.join(SANITIZER_FLAVORS)}", file=sys.stderr)
        return 2
    os.execvp("c++", ["c++", *flags, "-fno-sanitize-recover=all",
        f"-fsanitize-ignorelist={IGNORELIST}", "-g", *sys.argv[1:]])
    return 127


if __name__ == "__main__":
    raise SystemExit(main())
