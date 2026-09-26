"""Audit packaged dylibs against the notice catalog without claiming legal completeness."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def audit(app: Path, catalog: dict) -> dict:
    sources = {entry["name"]: entry for entry in catalog["sources"]}
    mappings = catalog.get("bundled_libraries", {})
    results = []
    for library in sorted((app / "Contents/Frameworks").rglob("*.dylib")):
        mapping = mappings.get(library.name, {})
        problems = []
        names = mapping.get("notices", [])
        if not names:
            problems.append(mapping.get("remaining", "library has no notice mapping"))
        for name in names:
            matches = list((app / "Contents/Resources/Notices").rglob(name + ".txt"))
            entry = sources.get(name)
            if entry is None or len(matches) != 1:
                problems.append(f"notice {name} is missing or ambiguous")
                continue
            expected = entry.get("excerpt", entry)["sha256"]
            if hashlib.sha256(matches[0].read_bytes()).hexdigest() != expected:
                problems.append(f"notice {name} differs from the catalog")
        results.append({"library": library.name, "notices": names, "problems": problems})
    return {"schema": 1, "catalog_complete": catalog.get("complete") is True,
        "dylib_notice_files_verified": bool(results) and all(not item["problems"] for item in results),
        "libraries": results, "remaining": catalog.get("remaining", [])}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--catalog", type=Path, default=ROOT / "native/notice-sources.json")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = audit(args.app, json.loads(args.catalog.read_text()))
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
    else:
        print(encoded, end="")
    return 0 if report["catalog_complete"] and report["dylib_notice_files_verified"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
