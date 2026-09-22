"""Run with Blender --background --python to verify bounded static GLB staging."""
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cook_glb_asset_blender import recalculate_static_normals, stage_static_reduction, triangle_count


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    side = 32
    vertices = [(x / (side - 1), y / (side - 1), 0.0)
        for y in range(side) for x in range(side)]
    faces = []
    for y in range(side - 1):
        for x in range(side - 1):
            lower_left = y * side + x
            faces.append((lower_left, lower_left + 1, lower_left + side + 1, lower_left + side))
    mesh_data = bpy.data.meshes.new("Dense static staging fixture")
    mesh_data.from_pydata(vertices, [], faces)
    mesh_data.update()
    mesh = bpy.data.objects.new("Dense static staging fixture", mesh_data)
    bpy.context.collection.objects.link(mesh)
    mesh.location = (2.0, 3.0, 4.0)
    mesh.scale = (2.0, 3.0, 4.0)
    location = tuple(mesh.location)
    scale = tuple(mesh.scale)
    before = triangle_count(mesh.data)
    assert before == (side - 1) * (side - 1) * 2

    stage_static_reduction([mesh], 64)
    after = triangle_count(mesh.data)
    assert after < before and after <= 64, (before, after)
    recalculate_static_normals([mesh])
    assert all(poly.normal.length > 0.99 for poly in mesh.data.polygons)
    assert tuple(mesh.location) == location and tuple(mesh.scale) == scale
    print(f"GLB static reduction regression passed: {before} -> {after} triangles; normals and transform retained.")


if __name__ == "__main__":
    main()
