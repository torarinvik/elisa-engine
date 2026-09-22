"""Self-test for static glTF node hierarchies baked by the runtime cooker.

The node hierarchy panel places three strip meshes under a matrix root: two
instances of a red strip under a translated group, a green strip authored
facing down and turned up by a rotation, and a blue strip mirrored by a
negative scale. Baked, the strips sit at x = -2/3, 0 and 2/3 of the panel, a
quarter wide, all facing +Y.
"""

from __future__ import annotations

from copy import deepcopy
import math
from pathlib import Path
import struct
import sys

import cook_assets
import cook_gltf_geometry
import cook_gltf_nodes


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "test/fixtures/node_hierarchy_panel.gltf"
SUBSETS = [(0, 12, 0), (12, 6, 1), (18, 6, 2)]
# Each strip's x range and z range in panel space, in placement order.
STRIPS = [(-19 / 24, -13 / 24, -1.0, 0.0), (-19 / 24, -13 / 24, 0.0, 1.0),
    (-0.125, 0.125, -1.0, 1.0), (13 / 24, 19 / 24, -1.0, 1.0)]
TOLERANCE = 1e-6


def fail(message: str) -> int:
    print(f"glTF cooker self-test failed: {message}", file=sys.stderr)
    return 1


def triangle_facing(positions: list, indices: list) -> list[float]:
    """The y component of each triangle's winding normal."""
    facing = []
    for triangle in range(0, len(indices), 3):
        a, b, c = (positions[index] for index in indices[triangle:triangle + 3])
        ab = [b[axis] - a[axis] for axis in range(3)]
        ac = [c[axis] - a[axis] for axis in range(3)]
        facing.append(ab[2] * ac[0] - ab[0] * ac[2])
    return facing


def baked_correctly(geometry: dict) -> bool:
    positions = list(struct.iter_unpack("<3f", geometry["positions"]))
    normals = list(struct.iter_unpack("<3f", geometry["normals"]))
    indices = list(struct.unpack(f"<{geometry['index_count']}I", geometry["indices"]))
    if (geometry["subsets"] != SUBSETS or geometry["vertex_count"] != 16 or
            geometry["material_slots"] != 3 or len(geometry["slot_materials"]) != 3 * 48):
        return False
    for strip, (x_low, x_high, z_low, z_high) in enumerate(STRIPS):
        corners = positions[strip * 4:strip * 4 + 4]
        xs = sorted(point[0] for point in corners)
        zs = sorted(point[2] for point in corners)
        if (abs(xs[0] - x_low) > TOLERANCE or abs(xs[3] - x_high) > TOLERANCE or
                abs(zs[0] - z_low) > TOLERANCE or abs(zs[3] - z_high) > TOLERANCE or
                any(abs(point[1]) > TOLERANCE for point in corners)):
            return False
    # Every face, including the rotated and the mirrored strip, faces up.
    return (all(normal == (0.0, 1.0, 0.0) for normal in normals) and
        all(facing > 0.0 for facing in triangle_facing(positions, indices)))


def rotation(axis: tuple, angle: float) -> tuple:
    """The axis-angle rotation matrix by Rodrigues' formula, row-major 3x4."""
    length = math.sqrt(sum(component * component for component in axis))
    x, y, z = (component / length for component in axis)
    c, s = math.cos(angle), math.sin(angle)
    t = 1.0 - c
    return (t * x * x + c, t * x * y - s * z, t * x * z + s * y, 0.0,
        t * x * y + s * z, t * y * y + c, t * y * z - s * x, 0.0,
        t * x * z - s * y, t * y * z + s * x, t * z * z + c, 0.0)


def close(actual: tuple, expected: tuple) -> bool:
    return all(abs(a - e) <= 1e-12 for a, e in zip(actual, expected, strict=True))


def local_transforms_correct() -> bool:
    """A TRS node is translation after rotation after scale, a matrix node
    is column-major, and quaternions turn right-handedly: a quarter turn
    about +Y carries +X to -Z."""
    quarter = cook_gltf_nodes.local_matrix({"rotation": [0.0, math.sqrt(0.5), 0.0, math.sqrt(0.5)]})
    if not close((quarter[0], quarter[4], quarter[8]), (0.0, 0.0, -1.0)):
        return False
    translation = (1.0, 0.0, 0.0, 0.5, 0.0, 1.0, 0.0, -1.0, 0.0, 0.0, 1.0, 2.0)
    scale = (2.0, 0.0, 0.0, 0.0, 0.0, 3.0, 0.0, 0.0, 0.0, 0.0, 0.25, 0.0)
    for axis, angle in (((1.0, 2.0, 3.0), 1.1), ((-2.0, 0.5, 1.0), -2.5)):
        half = math.sin(angle / 2) / math.sqrt(sum(component * component for component in axis))
        quaternion = [component * half for component in axis] + [math.cos(angle / 2)]
        expected = cook_gltf_nodes.multiply(translation,
            cook_gltf_nodes.multiply(rotation(axis, angle), scale))
        trs = cook_gltf_nodes.local_matrix({"translation": [0.5, -1.0, 2.0], "rotation": quaternion,
            "scale": [2.0, 3.0, 0.25]})
        columns = [expected[row * 4 + column] for column in range(4) for row in range(3)]
        matrix = cook_gltf_nodes.local_matrix({"matrix": [*columns[0:3], 0.0, *columns[3:6], 0.0,
            *columns[6:9], 0.0, *columns[9:12], 1.0]})
        if not close(trs, expected) or not close(matrix, expected):
            return False
    return True


def with_nodes(extra: list):
    return lambda document: document["nodes"].extend(extra)


def node(index: int, **fields):
    return lambda document: document["nodes"][index].update(fields)


def trs_root(document: dict) -> None:
    """The root's matrix as translation and scale."""
    root = document["nodes"][0]
    del root["matrix"]
    root.update(translation=[1 / 6, 0.0, 0.0], scale=[0.5, 1.0, 1.0])


def alternating(count: int):
    """`count` root-level placements alternating red and green strips, then
    the blue strip: count + 1 subsets."""
    def mutate(document: dict) -> None:
        document["nodes"] = [{"children": list(range(1, count + 2))},
            *({"mesh": number % 2} for number in range(count)), {"mesh": 2}]
    return mutate


def same_slot(document: dict) -> None:
    """Sixteen more red placements after the two red strips."""
    start = len(document["nodes"])
    document["nodes"].extend({"mesh": 0, "translation": [0.0, 0.0, float(number)]} for number in range(16))
    document["nodes"][1]["children"] += list(range(start, start + 16))


REJECTED = {
    "a node with a camera": (node(4, camera=0), "unsupported node properties"),
    "a node with an undeclared skin": (node(2, skin=0), "mesh node skin requires"),
    "node morph weights": (node(2, weights=[0.5]), "unsupported node properties"),
    "node extras": (node(1, extras={}), "unsupported node properties"),
    "a node that is not an object": (lambda d: d["nodes"].__setitem__(3, [2]), "unsupported node properties"),
    "a matrix beside a translation": (node(0, translation=[0.0, 0.0, 0.0]), "excludes translation"),
    "a projective matrix": (lambda d: d["nodes"][0]["matrix"].__setitem__(3, 0.5), "must be affine"),
    "a row-major translation": (lambda d: d["nodes"][0].update(matrix=[0.5, 0.0, 0.0, 1 / 6, 0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]), "must be affine"),
    "a fifteen-number matrix": (lambda d: d["nodes"][0]["matrix"].pop(), "matrix must list 16"),
    "a non-unit rotation": (node(4, rotation=[0.0, 0.0, 2.0, 0.0]), "unit quaternion"),
    "a three-number rotation": (node(4, rotation=[0.0, 0.0, 1.0]), "rotation must list 4"),
    "a non-finite translation": (node(1, translation=[float("nan"), 0.0, 0.0]), "translation must list 3"),
    "a boolean scale": (node(5, scale=[True, 1.0, 1.0]), "scale must list 3"),
    "a string translation": (node(1, translation="0 0 0"), "translation must list 3"),
    "a zero scale": (node(3, scale=[1.0, 0.0, 1.0]), "finite and invertible"),
    "a collapsing parent": (node(1, scale=[0.0, 1.0, 1.0]), "finite and invertible"),
    "an overflowing world transform": (lambda d: [d["nodes"][index].update(scale=[1e200, 1.0, 1.0])
        for index in (1, 2)], "finite and invertible"),
    "an overflowing world translation": (lambda d: [node(1, scale=[1e200, 1.0, 1.0])(d),
        node(2, translation=[1e200, 0.0, 0.0], scale=[1e-200, 1.0, 0.5])(d)], "finite and invertible"),
    "a vertex beyond the float range": (node(5, translation=[1e39, 0.0, 0.0]), "outside the float range"),
    "a child index out of range": (lambda d: d["nodes"][1]["children"].append(6), "child index out of range"),
    "a string child": (lambda d: d["nodes"][1]["children"].append("2"), "child index out of range"),
    "children that are not a list": (node(1, children=2), "must list node indices"),
    "a node with two parents": (lambda d: d["nodes"][0]["children"].append(2), "at most one parent"),
    "a root that is its own child": (lambda d: d["nodes"][0]["children"].append(0), "at most one parent"),
    "an unreachable cycle": (with_nodes([{"children": [7]}, {"children": [6]}]), "belong to the scene"),
    "an orphaned mesh node": (with_nodes([{"mesh": 0}]), "belong to the scene"),
    "a scene naming a child": (lambda d: d["scenes"][0]["nodes"].append(4), "hierarchy roots"),
    "a scene naming a root twice": (lambda d: d["scenes"][0]["nodes"].append(0), "distinct node indices"),
    "an empty scene": (lambda d: d["scenes"][0].update(nodes=[]), "distinct node indices"),
    "a scene node out of range": (lambda d: d["scenes"][0].update(nodes=[9]), "distinct node indices"),
    "two scenes": (lambda d: d["scenes"].append({"nodes": [0]}), "exactly one scene"),
    "no default scene": (lambda d: d.pop("scene"), "exactly one scene"),
    "scene extras": (lambda d: d["scenes"][0].update(extras={}), "exactly one scene"),
    "an unplaced mesh": (lambda d: d["meshes"].append(deepcopy(d["meshes"][0])), "every mesh must be placed"),
    "a mesh index out of range": (node(3, mesh=3), "mesh index out of range"),
    "a boolean mesh index": (node(3, mesh=True), "mesh index out of range"),
    "257 nodes": (lambda d: d["nodes"].extend({} for _ in range(251)), "1 to 256 nodes"),
    "257 meshes": (lambda d: d["meshes"].extend(deepcopy(d["meshes"][0]) for _ in range(254)), "1 to 256 meshes"),
    "a camera list": (lambda d: d.update(cameras=[{"type": "orthographic"}]), "without cameras"),
    "an animation list": (lambda d: d.update(animations=[{}]), "require a skinned mesh"),
    "seventeen alternating subsets": (alternating(16), "more than 16 material subsets"),
    "mesh extras": (lambda d: d["meshes"][1].update(extras={}), "unsupported mesh properties"),
    "a morph target on a placed mesh": (lambda d: d["meshes"][2]["primitives"][0].update(targets=[{}]),
        "morph targets must provide"),
    "a mesh that is not an object": (lambda d: d["meshes"].__setitem__(1, []), "unsupported mesh properties"),
    "a second mesh without primitives": (lambda d: d["meshes"][1].update(primitives=[]), "1 to 16 primitives"),
}


def hierarchy_self_test(temporary: Path) -> int:
    cooked = []
    for label in ("first", "second"):
        path, result = cook_gltf_geometry.cook_geometry_package(
            SOURCE, "test/fixtures/node_hierarchy_panel.gltf", temporary / f"hierarchy-{label}.pkg")
        cooked.append((path.read_bytes(), result))
    if (cooked[0] != cooked[1] or cooked[0][1]["triangles"] != 8 or cooked[0][1]["positions"] != 16 or
            cooked[0][1]["subsets"] != 3 or b"triangles=8\n" not in cooked[0][0]):
        return fail("the node hierarchy panel cooked differently twice or with the wrong counts")
    document = cook_assets.read_gltf(SOURCE.read_bytes())
    buffer = cook_assets.source_bytes(SOURCE.parent, document)
    geometry = cook_gltf_geometry.normalized_geometry(document, buffer)
    if not baked_correctly(geometry):
        return fail("the node hierarchy panel baked to the wrong world-space strips")
    if not local_transforms_correct():
        return fail("a node's local transform composed or turned the wrong way")

    # Equivalent authorings bake to the same bytes.
    equivalent = {
        "a TRS root": trs_root,
        "a rounded rotation": node(4, rotation=[0.0, 0.0, 1.0005, 0.0]),
        "a negated rotation": node(4, rotation=[0.0, 0.0, -1.0, 0.0]),
    }
    for label, mutate in equivalent.items():
        variant = deepcopy(document)
        mutate(variant)
        if cook_gltf_geometry.normalized_geometry(variant, buffer) != geometry:
            return fail(f"{label} baked differently from the authored panel")

    # Identity transforms keep a single-node package's bytes, even a -0.0
    # coordinate and a unit normal that renormalizing would round.
    length = math.sqrt(166.0)
    exact = struct.pack("<6f", -0.0, 0.1, 3.0, 6.0 / length, 7.0 / length, 9.0 / length)
    if (cook_gltf_nodes.transform_points(exact, cook_gltf_nodes.IDENTITY) != exact or
            cook_gltf_nodes.transform_normals(exact, cook_gltf_nodes.IDENTITY) != exact):
        return fail("an identity placement rewrote vertex bytes")
    tile = ROOT / "examples/maze/assets/maze_tile.gltf"
    tile_document = cook_assets.read_gltf(tile.read_bytes())
    tile_buffer = cook_assets.source_bytes(tile.parent, tile_document)
    plain = cook_gltf_geometry.normalized_geometry(tile_document, tile_buffer)
    for fields in ({"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]},
            {"matrix": [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]}):
        variant = deepcopy(tile_document)
        variant["nodes"][0].update(fields)
        variant["nodes"].insert(0, {"name": "group", "children": [1]})
        variant["scenes"][0]["nodes"] = [0]
        if cook_gltf_geometry.normalized_geometry(variant, tile_buffer) != plain:
            return fail("an identity hierarchy changed the maze tile's bytes")

    # Adjacent placements on one slot share a subset, and sixteen subsets fit.
    variant = deepcopy(document)
    same_slot(variant)
    merged = cook_gltf_geometry.normalized_geometry(variant, buffer)
    if merged["subsets"] != [(0, 108, 0), (108, 6, 1), (114, 6, 2)] or merged["vertex_count"] != 80:
        return fail("adjacent placements on one slot did not merge")
    variant = deepcopy(document)
    alternating(15)(variant)
    if len(cook_gltf_geometry.normalized_geometry(variant, buffer)["subsets"]) != 16:
        return fail("sixteen placed subsets were not kept")

    # The running vertex and index totals are bounded across placements.
    bounds = (("MAX_VERTICES", 15, "position count"), ("MAX_INDICES", 23, "index count"))
    for name, bound, reason in bounds:
        saved = getattr(cook_gltf_geometry, name)
        setattr(cook_gltf_geometry, name, bound)
        try:
            cook_gltf_geometry.normalized_geometry(document, buffer)
            accepted = True
        except ValueError as error:
            accepted = reason not in str(error)
        finally:
            setattr(cook_gltf_geometry, name, saved)
        if accepted:
            return fail(f"placements past {name} were not rejected for their {reason}")

    for label, (mutate, reason) in REJECTED.items():
        variant = deepcopy(document)
        mutate(variant)
        try:
            cook_gltf_geometry.normalized_geometry(variant, buffer)
        except ValueError as error:
            if reason in str(error):
                continue
            return fail(f"rejected {label} for another reason: {error}")
        return fail(f"accepted {label}")
    return 0
