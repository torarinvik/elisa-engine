#!/usr/bin/env python3
"""Compare complete VM tag inventories from one original effect lifecycle run."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

FOOTPRINT = re.compile(r"effect memory cycle (\d+): phys_footprint (\d+) KiB")
REGIONS = re.compile(r"effect memory VM regions cycle (\d+): complete (\d+) status (-?\d+) regions (\d+)")
TAG = re.compile(r"effect memory VM tag cycle (\d+): tag (\d+) virtual (\d+) resident (\d+) dirtied (\d+) regions (\d+)")
FIELDS = ("virtual", "resident", "dirtied", "regions")


def analyze(text: str) -> dict[str, object]:
    footprints, inventories, tags = {}, {}, {}
    for line in text.splitlines():
        if match := FOOTPRINT.fullmatch(line):
            cycle, value = map(int, match.groups())
            if value <= 0:
                raise ValueError(f"invalid footprint in cycle {cycle}")
            if cycle in footprints:
                raise ValueError(f"duplicate footprint cycle {cycle}; provide one run")
            footprints[cycle] = value
        elif match := REGIONS.fullmatch(line):
            cycle, complete, status, visited = map(int, match.groups())
            if cycle in inventories:
                raise ValueError(f"duplicate VM inventory cycle {cycle}")
            inventories[cycle] = (complete, status, visited)
        elif match := TAG.fullmatch(line):
            cycle, tag, *values = map(int, match.groups())
            bucket = tags.setdefault(cycle, {})
            if tag in bucket or tag > 256:
                raise ValueError(f"invalid or duplicate VM tag {tag} in cycle {cycle}")
            bucket[tag] = dict(zip(FIELDS, values))
    expected = set(range(7))
    if set(footprints) != expected or set(inventories) != expected or set(tags) != expected:
        raise ValueError("requires baseline cycle 0 and all six measured cycles with VM tags")
    for cycle, (complete, status, visited) in inventories.items():
        if complete != 1 or sum(row["regions"] for row in tags[cycle].values()) != visited:
            raise ValueError(f"incomplete VM inventory in cycle {cycle} (status {status})")
    peak = max(footprints, key=footprints.get)
    deltas = []
    for tag in tags[0].keys() | tags[peak].keys():
        before, after = tags[0].get(tag, {}), tags[peak].get(tag, {})
        delta = {field: after.get(field, 0) - before.get(field, 0) for field in FIELDS}
        if any(delta.values()):
            deltas.append({"tag": tag, **delta})
    deltas.sort(key=lambda row: (-row["resident"], row["tag"]))
    growth = footprints[peak] - footprints[0]
    return {"schema": 1, "peak_cycle": peak, "footprint_kib": footprints,
        "peak_growth_kib": growth, "allowance_kib": 8192, "over_allowance": growth > 8192,
        "peak_tag_deltas": deltas,
        "limitations": "VM resident totals include shared mappings and are not physical footprint; a tag delta does not establish a leak or allocation owner."}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    try:
        if args.log.stat().st_size > 32 * 1024 * 1024:
            raise ValueError("diagnostic log exceeds 32 MiB")
        report = analyze(args.log.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        parser.exit(2, f"effect memory report: {error}\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
