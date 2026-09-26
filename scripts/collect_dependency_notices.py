#!/usr/bin/env python3
"""Collect hash-verified notice texts from the explicitly partial source catalog."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def collect(manifest: Path, roots: dict[str, Path], output: Path) -> int:
    document = json.loads(manifest.read_text(encoding="utf-8"))
    if document.get("schema") != 1:
        raise ValueError("unsupported notice catalog")
    verified = []
    names = set()
    for entry in document["sources"]:
        name = entry["name"]
        if not name or Path(name).name != name or name in (".", "..") or name in names:
            raise ValueError("invalid or duplicate notice name")
        names.add(name)
        root = roots[entry["root"]].resolve()
        source = (root / entry["path"]).resolve()
        if not source.is_relative_to(root):
            raise ValueError(f"notice source escapes root: {name}")
        data = source.read_bytes()
        if not data or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"notice source changed: {name}")
        if "excerpt" in entry:
            excerpt = entry["excerpt"]
            offset, length = excerpt["offset"], excerpt["bytes"]
            if (type(offset) is not int or type(length) is not int or offset < 0
                    or length <= 0 or offset + length > len(data)):
                raise ValueError(f"invalid notice excerpt: {name}")
            data = data[offset:offset + length]
            if hashlib.sha256(data).hexdigest() != excerpt["sha256"]:
                raise ValueError(f"notice excerpt changed: {name}")
        verified.append((name, data))
    if output.exists():
        raise ValueError("notice destination already exists; select a new destination")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".elisa-notices-", dir=output.parent) as folder:
        staged = Path(folder) / "notices"
        staged.mkdir()
        for name, data in verified:
            (staged / (name + ".txt")).write_bytes(data)
        (staged / "sources.json").write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
        os.replace(staged, output)
    return len(verified)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=ROOT / "native/notice-sources.json")
    parser.add_argument("--wicked-root", type=Path, default=ROOT.parent / "WickedEngine")
    parser.add_argument("--brew-prefix", type=Path, default=Path("/opt/homebrew"))
    args = parser.parse_args()
    try:
        count = collect(args.manifest, {"engine": ROOT, "wicked": args.wicked_root,
            "brew": args.brew_prefix}, args.output)
    except (OSError, ValueError, KeyError) as error:
        print(f"notice collection failed: {error}")
        return 1
    print(f"Collected {count} verified notice files; consult sources.json for remaining coverage")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
