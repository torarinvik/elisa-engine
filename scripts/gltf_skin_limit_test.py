#!/usr/bin/env python3
"""Skin joint limit: mocap rigs of about 70 bones (and up to the native pose
limit of 256) cook; a skin with more joints is rejected before any package is
written. Also provides the chain rig that scripts/pose_scrub_benchmark.py
renders."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cook_gltf_geometry  # noqa: E402
import cook_gltf_skin  # noqa: E402
import gltf_skin_self_test  # noqa: E402


def chain_document(joints: int) -> dict:
    """The self-test skinned panel with its tip extended into a joint chain."""
    document = gltf_skin_self_test.generated_document(inverse_bind_matrices=None)
    for primitive in document["meshes"][0]["primitives"]:
        primitive.pop("targets", None)
    document["meshes"][0].pop("weights", None)
    parent = 2
    for index in range(joints - 2):
        child = len(document["nodes"])
        document["nodes"][parent]["children"] = [child]
        document["nodes"].append({"name": f"chain_{index}", "translation": [0.0, 0.02, 0.0]})
        document["skins"][0]["joints"].append(child)
        parent = child
    return document


def write_chain_rig(output: Path, joints: int) -> tuple[Path, dict]:
    with tempfile.TemporaryDirectory(prefix="elisa-chain-rig-") as temporary:
        source = Path(temporary) / "chain_rig.gltf"
        source.write_text(json.dumps(chain_document(joints), separators=(",", ":")), encoding="utf-8")
        return cook_gltf_geometry.cook_geometry_package(source, gltf_skin_self_test.ASSET_PATH, output)


def main() -> int:
    if cook_gltf_skin.MAX_JOINTS != 256:
        print(f"skin limit test expects 256 joints, cooker allows {cook_gltf_skin.MAX_JOINTS}", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="elisa-skin-limit-") as temporary:
        for joints in (70, 200):
            output = Path(temporary) / f"rig-{joints}.pkg"
            write_chain_rig(output, joints)
            text = output.read_text(encoding="utf-8", errors="replace")
            if f"skin_bones={joints}\n" not in text:
                print(f"a {joints}-joint skin did not cook with {joints} palette bones", file=sys.stderr)
                return 1
        rejected = Path(temporary) / "rig-257.pkg"
        try:
            write_chain_rig(rejected, 257)
        except ValueError:
            pass
        else:
            print("a 257-joint skin was accepted", file=sys.stderr)
            return 1
        if rejected.exists():
            print("a rejected skin left a partial package", file=sys.stderr)
            return 1
    print("Skin joint limit accepts 70 and 200 joints and rejects 257.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
