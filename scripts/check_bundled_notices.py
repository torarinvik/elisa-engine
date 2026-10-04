"""Audit packaged dependencies and resource folders without claiming legal completeness."""
import argparse
import hashlib
import json
from pathlib import Path
from pathlib import PurePosixPath

ROOT = Path(__file__).resolve().parents[1]


def audit(app: Path, catalog: dict) -> dict:
    sources = {entry["name"]: entry for entry in catalog["sources"]}
    mappings = catalog.get("bundled_libraries", {})
    static_mappings = catalog.get("statically_linked_components", {})
    resource_mappings = catalog.get("bundled_resources", {})
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
    static_results = []
    for component, mapping in sorted(static_mappings.items()):
        problems = []
        names = mapping.get("notices", [])
        if not names:
            problems.append(mapping.get("remaining", "component has no notice mapping"))
        for name in names:
            matches = list((app / "Contents/Resources/Notices").rglob(name + ".txt"))
            entry = sources.get(name)
            if entry is None or len(matches) != 1:
                problems.append(f"notice {name} is missing or ambiguous")
                continue
            expected = entry.get("excerpt", entry)["sha256"]
            if hashlib.sha256(matches[0].read_bytes()).hexdigest() != expected:
                problems.append(f"notice {name} differs from the catalog")
        static_results.append({"component": component, "evidence": mapping.get("evidence", ""),
            "notices": names, "problems": problems,
            "remaining": mapping.get("remaining", [])})
    resource_results = []
    app_root = app.resolve()
    notices_root = app / "Contents/Resources/Notices"
    for component, mapping in sorted(resource_mappings.items()):
        problems = []
        names = mapping.get("notices", [])
        relative = PurePosixPath(mapping.get("path", ""))
        if (not relative.parts or relative.is_absolute()
                or any(part in (".", "..") for part in relative.parts)):
            problems.append("resource path must be a normalized relative path")
            resource_path = None
        else:
            resource_path = (app_root / Path(*relative.parts)).resolve()
            if not resource_path.is_relative_to(app_root):
                problems.append("resource path escapes the app bundle")
            elif not resource_path.is_dir():
                problems.append("resource directory is missing")
            elif not any(path.is_file() and path.resolve().is_relative_to(resource_path)
                    for path in resource_path.rglob("*")):
                problems.append("resource directory has no regular files")
        if not names:
            problems.append(mapping.get("remaining", "resource has no notice mapping"))
        for name in names:
            matches = list(notices_root.rglob(name + ".txt"))
            entry = sources.get(name)
            if entry is None or len(matches) != 1:
                problems.append(f"notice {name} is missing or ambiguous")
                continue
            expected = entry.get("excerpt", entry)["sha256"]
            if hashlib.sha256(matches[0].read_bytes()).hexdigest() != expected:
                problems.append(f"notice {name} differs from the catalog")
        resource_results.append({"component": component, "path": mapping.get("path", ""),
            "evidence": mapping.get("evidence", ""), "notices": names,
            "problems": problems, "remaining": mapping.get("remaining", [])})
    return {"schema": 1, "catalog_complete": catalog.get("complete") is True,
        "dylib_notice_files_verified": bool(results) and all(not item["problems"] for item in results),
        "statically_linked_notice_files_verified": all(not item["problems"] for item in static_results),
        "bundled_resource_notice_files_verified": all(not item["problems"] for item in resource_results),
        "libraries": results, "statically_linked_components": static_results,
        "bundled_resources": resource_results,
        "remaining": catalog.get("remaining", [])}


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
    return 0 if (report["catalog_complete"] and report["dylib_notice_files_verified"]
        and report["statically_linked_notice_files_verified"]
        and report["bundled_resource_notice_files_verified"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
