#!/usr/bin/env python3
"""Cook the character course's skinned guide rig.

The rig is the engine's synthetic two-joint skinned panel with its looping
``lift`` clip (``scripts/gltf_skin_self_test.py``), cooked through the normal
glTF importer, so the course ships no third-party model. The same cook writes
the rig's ``elisa-anim-v1`` skeleton/clip contract (``guide_rig.anim``). Cooking
is deterministic, and ``--check`` fails if either committed file drifted.
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
CONTRACT = HERE / "rigs/guide_rig.anim"


def cooked() -> tuple[bytes, bytes]:
    with tempfile.TemporaryDirectory(prefix="elisa-course-rig-") as temporary:
        output = Path(temporary) / RIG.name
        contract = Path(temporary) / CONTRACT.name
        gltf_skin_self_test.write_package(output, animation_contract=contract)
        return output.read_bytes(), contract.read_bytes()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the committed package differs")
    arguments = parser.parse_args()
    outputs = dict(zip((RIG, CONTRACT), cooked()))
    if arguments.check:
        stale = [path.name for path, data in outputs.items()
            if not path.is_file() or path.read_bytes() != data]
        if stale:
            print("Regenerate with make_rigs.py: " + ", ".join(stale), file=sys.stderr)
            return 1
        return 0
    RIG.parent.mkdir(exist_ok=True)
    for path, data in outputs.items():
        path.write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
