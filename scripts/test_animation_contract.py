#!/usr/bin/env python3
"""Check the cooked animation contract writer and the committed course rig contract."""

from __future__ import annotations

import struct
import subprocess
import sys
from pathlib import Path

import cook_animation_contract as contract

ROOT = Path(__file__).resolve().parents[1]
IDENTITY = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0)


def skin(parents: list[int], names: list[str] | None = None) -> dict:
    names = names or [f"joint_{index}" for index in range(len(parents))]
    joints = [{"name": name, "parent": parent, "rest": (0.0, 1.0 if parent >= 0 else 0.0, *IDENTITY[2:])}
        for name, parent in zip(names, parents)]
    return {"joints": joints, "cluster_joints": list(range(len(parents))), "inverse_bind_matrices": None}


def clip(joints: int, frames: int = 2, name: str = "idle") -> dict:
    samples = [component for _ in range(frames) for _ in range(joints) for component in IDENTITY]
    return {"name": name, "frames": frames, "sample_rate": 30, "samples": samples}


def rejects(label: str, action) -> bool:
    try:
        action()
    except ValueError:
        return True
    print(f"animation contract accepted {label}", file=sys.stderr)
    return False


def main() -> int:
    data = contract.encode(skin([-1, 0, 1]), [clip(3), clip(3, 4, "walk")])
    magic, version, total, checksum = struct.unpack_from("<4I", data)
    rig_id, joints, clips, metres = struct.unpack_from("<3If", data, 16)
    if (magic, version, total, joints, clips, metres) != (contract.MAGIC, 1, len(data), 3, 2, 1.0):
        print("animation contract header is wrong", file=sys.stderr)
        return 1
    if checksum != contract.fnv1a(data[16:]) or rig_id != contract.stable_id(data[32:32 + 3 * 112]):
        print("animation contract checksum or rig id is wrong", file=sys.stderr)
        return 1
    expected = 32 + 3 * 112 + 2 * 24 + 2 * 3 * 8 + (2 + 4) * 3 * 44
    if len(data) != expected:
        print(f"animation contract is {len(data)} bytes, expected {expected}", file=sys.stderr)
        return 1
    bound_skin = skin([-1, 0])
    bound_skin["inverse_bind_matrices"] = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0] * 2
    checks = [
        rejects("a child before its parent", lambda: contract.encode(skin([-1, 2, 0]), [])),
        rejects("a self parent", lambda: contract.encode(skin([0]), [])),
        rejects("duplicate joint names", lambda: contract.encode(skin([-1, 0], ["hip", "hip"]), [])),
        rejects("too many joints", lambda: contract.encode(skin([-1] * 65), [])),
        rejects("an empty rig", lambda: contract.encode(skin([]), [])),
        rejects("a missing rig", lambda: contract.encode(None, [])),
        rejects("a clip missing a joint track", lambda: contract.encode(skin([-1, 0]), [clip(1)])),
        rejects("duplicate clip names", lambda: contract.encode(skin([-1]), [clip(1), clip(1)])),
        rejects("a bind pose that disagrees with the rest pose", lambda: contract.encode(bound_skin, [])),
    ]
    if not all(checks):
        return 1
    rigs = subprocess.run([sys.executable, str(ROOT / "examples/character_course/make_rigs.py"), "--check"],
        check=False)
    return rigs.returncode


if __name__ == "__main__":
    raise SystemExit(main())
