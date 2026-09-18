#!/usr/bin/env python3
"""Compiler wrapper that adds AddressSanitizer and UndefinedBehaviorSanitizer.

Point `CXX` at this executable to build the native probe instrumented without
changing the build orchestrator:

  CXX="$PWD/scripts/cxx_sanitize.py" elisascript scripts/wicked_probe.elisascript

`-fno-sanitize-recover=all` makes an undefined-behaviour report abort the
process, so a sanitizer finding turns into a nonzero exit status the runner
already relays. The probe exits with `std::_Exit`, so leak detection does not
run; this catches memory and undefined-behaviour errors during the run.
"""

import os
import sys

SANITIZER_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g"]


def main() -> int:
    os.execvp("c++", ["c++", *SANITIZER_FLAGS, *sys.argv[1:]])
    return 127


if __name__ == "__main__":
    raise SystemExit(main())
