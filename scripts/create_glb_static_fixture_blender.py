"""Create a deterministic dense, transformed GLB for the static cooker test."""
import argparse
from pathlib import Path
import sys

import bpy


def main():
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    options = parser.parse_args(arguments)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    side = 40
    vertices = [(x / (side - 1), y / (side - 1), 0.0)
        for y in range(side) for x in range(side)]
    faces = []
    for y in range(side - 1):
        for x in range(side - 1):
            lower_left = y * side + x
            faces.append((lower_left, lower_left + 1, lower_left + side + 1, lower_left + side))
    data = bpy.data.meshes.new("Dense GLB fixture")
    data.from_pydata(vertices, [], faces)
    data.update()
    mesh = bpy.data.objects.new("Dense GLB fixture", data)
    bpy.context.collection.objects.link(mesh)
    mesh.location = (2.0, 3.0, 4.0)
    mesh.scale = (2.0, 3.0, 4.0)
    bpy.context.view_layer.objects.active = mesh
    mesh.select_set(True)
    options.output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(options.output), export_format="GLB",
        use_selection=True, export_apply=False)
    print(f"Created dense static GLB fixture: {options.output}")


if __name__ == "__main__":
    main()
