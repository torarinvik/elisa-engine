#!/usr/bin/env python3
"""Cook the character course's streamed cell packages.

The course floor is split into 2 m cells. Each cell draws one of two cooked
meshes (``cell-mesh-0/1.elpk``) with one of 13 material packages
(``cell-paint-NN.elpk``); the native asset stream loads them as the player
crosses cell boundaries. The packages are the same deterministic set the
engine's cell-streaming smoke cooks, and ``--check`` fails if the committed
files drifted.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "scripts"))

from application_native_smoke import write_cell_stream_fixtures  # noqa: E402

CELLS = HERE / "cells"


def cooked() -> dict[str, bytes]:
    with tempfile.TemporaryDirectory(prefix="elisa-course-cells-") as temporary:
        directory = Path(temporary)
        write_cell_stream_fixtures(directory)
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
