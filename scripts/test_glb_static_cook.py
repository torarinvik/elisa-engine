"""Exercise dense static GLB import through Blender and the production FBX cooker."""
import base64
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from cook_glb_asset import read_glb_document


ROOT = Path(__file__).resolve().parents[1]
TRIANGLE_LIMIT = 64


def run(command: list[str]) -> None:
    print("+", " ".join(repr(argument) for argument in command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    blender = os.environ.get("BLENDER") or shutil.which("blender")
    if not blender:
        raise RuntimeError("Blender is required for the static GLB cooker integration test")
    with tempfile.TemporaryDirectory(prefix="Elisa static GLB cook ") as temporary:
        directory = Path(temporary)
        source = directory / "dense-transformed.glb"
        output = directory / "dense-transformed.pkg"
        run([blender, "--background", "--factory-startup", "--python",
            str(ROOT / "scripts/create_glb_static_fixture_blender.py"), "--",
            "--output", str(source)])
        document, _ = read_glb_document(source)
        mesh_nodes = [node for node in document.get("nodes", []) if "mesh" in node]
        if len(mesh_nodes) != 1 or mesh_nodes[0].get("translation") != [2.0, 4.0, -3.0] or \
                mesh_nodes[0].get("scale") != [2.0, 4.0, 3.0]:
            raise AssertionError("fixture GLB did not retain its authored node transform")

        run([sys.executable, str(ROOT / "scripts/cook_glb_asset.py"), str(source),
            "--asset-path", "fixtures/dense-transformed.glb", "--output", str(output),
            "--max-triangles", str(TRIANGLE_LIMIT)])
        fields = dict(line.split("=", 1) for line in output.read_text(encoding="utf-8").splitlines()
            if "=" in line)
        triangles = int(fields["triangles"])
        vertex_count = int(fields["positions"])
        index_count = int(fields["indices"])
        if not 0 < triangles <= TRIANGLE_LIMIT or index_count != triangles * 3 or vertex_count == 0:
            raise AssertionError("cooked GLB package did not honor its bounded triangle/index counts")

        positions = base64.b64decode(fields["positions_b64"], validate=True)
        normals = base64.b64decode(fields["normals_b64"], validate=True)
        tangents = base64.b64decode(fields["tangents_b64"], validate=True)
        indices = base64.b64decode(fields["indices_b64"], validate=True)
        if len(positions) != vertex_count * 12 or len(normals) != vertex_count * 12 or \
                len(tangents) != vertex_count * 16 or len(indices) != index_count * 4:
            raise AssertionError("cooked GLB stream sizes do not match their declared counts")
        coordinates = [value for (value,) in struct.iter_unpack("<f", positions)]
        normal_rows = list(struct.iter_unpack("<3f", normals))
        index_values = [value for (value,) in struct.iter_unpack("<I", indices)]
        if not all(math.isfinite(value) for value in coordinates) or \
                max(abs(value) for value in coordinates) < 1.0:
            raise AssertionError("cooked GLB lost its non-origin node transform or contains invalid positions")
        if not all(math.isfinite(value) and abs(math.sqrt(sum(axis * axis for axis in normal)) - 1.0) < 1.0e-4
                for normal in normal_rows for value in normal):
            raise AssertionError("cooked GLB normals are not finite unit vectors")
        if any(index >= vertex_count for index in index_values):
            raise AssertionError("cooked GLB index refers outside the vertex stream")
        print(f"Static GLB cook regression passed: {triangles} triangles, {vertex_count} vertices, "
            "transformed positions and bounded geometry streams.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
