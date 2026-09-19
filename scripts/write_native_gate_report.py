#!/usr/bin/env python3
"""Write the structured result for the ElisaScript native-first gate."""

import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 8:
        print("usage: write_native_gate_report.py OUTPUT MODE DEP SOURCE HYGIENE HEADLESS NATIVE", file=sys.stderr)
        return 2
    output, mode = Path(sys.argv[1]), sys.argv[2]
    names = ("dependency", "source_length", "module_hygiene", "headless", "native")
    try:
        statuses = {name: int(value) for name, value in zip(names, sys.argv[3:])}
    except ValueError:
        print("native gate statuses must be integers", file=sys.stderr)
        return 2
    output.write_text(json.dumps({"mode": mode, **statuses}, sort_keys=True) + "\n", encoding="utf-8")
    print(f"native gate report: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
