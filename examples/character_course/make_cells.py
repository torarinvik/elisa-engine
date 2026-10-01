#!/usr/bin/env python3
"""Cook the character course's streamed cell packages.

The course floor is split into 2 m cells. Each cell draws one of two cooked
meshes (``cell-mesh-0/1.elpk``) with one of 13 material packages
(``cell-paint-NN.elpk``); the native asset stream loads them as the player
crosses cell boundaries. The packages are the same deterministic set the
engine's cell-streaming smoke cooks. The durable beacons (beacons.elisa)
stream from the same mounted directory: ``beacon-post``/``beacon-lamp`` are
the meshes and ``beacon-stone``/``beacon-unlit``/``beacon-lit`` the materials
named by the beacons' stable asset IDs. ``--check`` fails if the committed
files drifted.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "scripts"))

from application_native_smoke import cooked_mesh_text, write_cell_stream_fixtures  # noqa: E402
from elisa_package import write_package  # noqa: E402

CELLS = HERE / "cells"


def write_beacon_packages(directory: Path) -> None:
    """A unit box post, an octahedral lamp spanning -1..1 like the render
    primitives they replace, and three single-colour material packages."""
    box = (
        -1.0, -1.0, -1.0, 1.0, -1.0, -1.0, 1.0, 1.0, -1.0, -1.0, 1.0, -1.0,
        -1.0, -1.0, 1.0, 1.0, -1.0, 1.0, 1.0, 1.0, 1.0, -1.0, 1.0, 1.0,
    )
    box_indices = (0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
        2, 3, 7, 2, 7, 6, 1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3)
    lamp = (1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, -1.0)
    lamp_indices = (0, 2, 4, 1, 4, 2, 0, 4, 3, 1, 3, 4, 0, 5, 2, 1, 2, 5, 0, 3, 5, 1, 5, 3)
    write_package(directory / "beacon-post.elpk", {"mesh": cooked_mesh_text("beacon-post", box, box_indices)})
    write_package(directory / "beacon-lamp.elpk", {"mesh": cooked_mesh_text("beacon-lamp", lamp, lamp_indices)})
    for name, texel in (("stone", (140, 128, 112, 255)), ("unlit", (77, 87, 107, 255)), ("lit", (255, 209, 64, 255))):
        write_package(directory / f"beacon-{name}.elpk", {"albedo": bytes(texel) * 8})


def cooked() -> dict[str, bytes]:
    with tempfile.TemporaryDirectory(prefix="elisa-course-cells-") as temporary:
        directory = Path(temporary)
        write_cell_stream_fixtures(directory)
        write_beacon_packages(directory)
        return {path.name: path.read_bytes() for path in sorted(directory.iterdir())}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if a committed package differs")
    arguments = parser.parse_args()
    packages = cooked()
    if arguments.check:
        committed = {path.name for path in CELLS.glob("*.elpk")} if CELLS.is_dir() else set()
        stale = [name for name, data in packages.items()
            if not (CELLS / name).is_file() or (CELLS / name).read_bytes() != data]
        stale += sorted(committed - set(packages))
        if stale:
            print("Regenerate with make_cells.py: " + ", ".join(stale), file=sys.stderr)
            return 1
        return 0
    CELLS.mkdir(exist_ok=True)
    for name, data in packages.items():
        (CELLS / name).write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
