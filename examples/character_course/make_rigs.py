#!/usr/bin/env python3
"""Cook the character course's skinned guide rig.

The rig is the engine's synthetic two-joint skinned panel with its looping
``lift`` clip (``scripts/gltf_skin_self_test.py``), cooked through the normal
glTF importer, so the course ships no third-party model. Cooking is
deterministic, and ``--check`` fails if the committed package drifted.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "scripts"))

import gltf_skin_self_test  # noqa: E402

RIG = HERE / "rigs/guide_rig.pkg"


def cooked() -> bytes:
    with tempfile.TemporaryDirectory(prefix="elisa-course-rig-") as temporary:
        output = Path(temporary) / RIG.name
        gltf_skin_self_test.write_package(output)
        return output.read_bytes()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the committed package differs")
    arguments = parser.parse_args()
    data = cooked()
    if arguments.check:
        if not RIG.is_file() or RIG.read_bytes() != data:
            print("Regenerate with make_rigs.py: " + RIG.name, file=sys.stderr)
            return 1
        return 0
    RIG.parent.mkdir(exist_ok=True)
    RIG.write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
