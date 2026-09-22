"""Normalize bounded glTF camera and punctual-light scene metadata."""

from __future__ import annotations

import base64
import math
import struct

import cook_gltf_nodes


MAX_CAMERAS = 16
MAX_LIGHTS = 32
LIGHT_EXTENSION = "KHR_lights_punctual"
CAMERA_KEYS = {"name", "type", "perspective", "orthographic"}
PERSPECTIVE_KEYS = {"aspectRatio", "yfov", "znear", "zfar"}
ORTHOGRAPHIC_KEYS = {"xmag", "ymag", "znear", "zfar"}
LIGHT_KEYS = {"name", "type", "color", "intensity", "range", "spot"}
SPOT_KEYS = {"innerConeAngle", "outerConeAngle"}


def finite(value, label: str, minimum: float | None = None) -> float:
    if type(value) not in (int, float) or not math.isfinite(value) or (minimum is not None and value <= minimum):
        raise ValueError(f"scene {label} must be finite and positive")
    return float(value)


def color(value, label: str) -> tuple[float, float, float]:
    if not isinstance(value, list) or len(value) != 3 or any(
            type(component) not in (int, float) or not math.isfinite(component) or component < 0.0
            for component in value):
        raise ValueError(f"scene {label} must list three finite nonnegative channels")
    return tuple(float(component) for component in value)


def _camera_values(camera: object) -> dict:
    if not isinstance(camera, dict) or set(camera) - CAMERA_KEYS or camera.get("type") not in (
            "perspective", "orthographic"):
        raise ValueError("camera metadata is unsupported")
    if camera["type"] == "perspective":
        values = camera.get("perspective")
        if not isinstance(values, dict) or set(values) - PERSPECTIVE_KEYS:
            raise ValueError("perspective camera metadata is unsupported")
        aspect = finite(values.get("aspectRatio", 1.0), "camera aspect")
        fov = finite(values.get("yfov"), "camera field of view")
        near = finite(values.get("znear"), "camera near clip")
        far = finite(values.get("zfar"), "camera far clip")
        if fov >= math.pi or far <= near:
            raise ValueError("perspective camera limits are invalid")
        return {"projection": 0, "x": fov, "y": aspect, "near": near, "far": far}
    values = camera.get("orthographic")
    if not isinstance(values, dict) or set(values) - ORTHOGRAPHIC_KEYS:
        raise ValueError("orthographic camera metadata is unsupported")
    xmag = finite(values.get("xmag"), "camera horizontal size")
    ymag = finite(values.get("ymag"), "camera vertical size")
    near = finite(values.get("znear"), "camera near clip")
    far = finite(values.get("zfar"), "camera far clip")
    if far <= near:
        raise ValueError("orthographic camera limits are invalid")
    return {"projection": 1, "x": xmag, "y": ymag, "near": near, "far": far}


def _light_values(light: object) -> dict:
    if not isinstance(light, dict) or set(light) - LIGHT_KEYS or light.get("type") not in (
            "directional", "point", "spot"):
        raise ValueError("light metadata is unsupported")
    if light["type"] != "spot" and "spot" in light:
        raise ValueError("only spot lights may define spot cone metadata")
    kind = {"directional": 0, "point": 1, "spot": 2}[light["type"]]
    range_value = finite(light["range"], "light range") if "range" in light else 0.0
    intensity = finite(light.get("intensity", 1.0), "light intensity")
    if range_value < 0.0 or intensity < 0.0:
        raise ValueError("light range and intensity must be nonnegative")
    channels = color(light.get("color", [1.0, 1.0, 1.0]), "light color")
    inner = outer = 0.0
    if "spot" in light:
        spot = light["spot"]
        if not isinstance(spot, dict) or set(spot) - SPOT_KEYS:
            raise ValueError("spot light metadata is unsupported")
        inner = finite(spot.get("innerConeAngle", 0.0), "spot inner cone")
        outer = finite(spot.get("outerConeAngle", math.pi / 4.0), "spot outer cone")
        if inner < 0.0 or outer < 0.0 or outer <= inner or outer > math.pi / 2.0:
            raise ValueError("spot light cone limits are invalid")
    return {"kind": kind, "intensity": intensity, "range": range_value,
        "color": channels, "inner": inner, "outer": outer}


def world_matrices(document: dict) -> list[tuple]:
    nodes = document.get("nodes", [])
    parents = [None] * len(nodes)
    locals_ = []
    for index, node in enumerate(nodes):
        children = node.get("children", [])
        for child in children:
            if child == index or parents[child] is not None:
                raise ValueError("a node must have at most one parent")
            parents[child] = index
        locals_.append(cook_gltf_nodes.local_matrix(node))
    roots = document["scenes"][0]["nodes"]
    if any(parents[root] is not None for root in roots):
        raise ValueError("scene nodes must be hierarchy roots")
    result = [None] * len(nodes)
    pending = [(root, cook_gltf_nodes.IDENTITY) for root in reversed(roots)]
    while pending:
        index, parent = pending.pop()
        result[index] = cook_gltf_nodes.multiply(parent, locals_[index])
        pending.extend((child, result[index]) for child in reversed(nodes[index].get("children", [])))
    return result


def normalize(document: dict, buffer: bytes) -> dict:
    used = set(document.get("extensionsUsed", []))
    required = set(document.get("extensionsRequired", []))
    if used - {LIGHT_EXTENSION} or required - {LIGHT_EXTENSION} or not required <= used:
        raise ValueError("runtime scene importer encountered an unsupported extension")
    cameras = document.get("cameras", [])
    if not isinstance(cameras, list) or len(cameras) > MAX_CAMERAS:
        raise ValueError(f"runtime scene importer accepts at most {MAX_CAMERAS} cameras")
    extensions = document.get("extensions", {})
    if not isinstance(extensions, dict):
        raise ValueError("scene extensions must be an object")
    light_extension_root = extensions.get(LIGHT_EXTENSION, {})
    if not isinstance(light_extension_root, dict):
        raise ValueError("punctual light extension must be an object")
    lights_root = light_extension_root.get("lights", [])
    if not isinstance(lights_root, list) or len(lights_root) > MAX_LIGHTS:
        raise ValueError(f"runtime scene importer accepts at most {MAX_LIGHTS} lights")
    if lights_root and LIGHT_EXTENSION not in used:
        raise ValueError("punctual lights need KHR_lights_punctual")
    camera_values = [_camera_values(camera) for camera in cameras]
    light_values = [_light_values(light) for light in lights_root]
    worlds = world_matrices(document)
    normalized_cameras = []
    normalized_lights = []
    for node_index, node in enumerate(document.get("nodes", [])):
        if "camera" in node:
            reference = node["camera"]
            if type(reference) is not int or not 0 <= reference < len(cameras):
                raise ValueError("node camera index is out of range")
            camera = camera_values[reference]
            normalized_cameras.append({"node": node_index, **camera, "transform": worlds[node_index]})
        extension = node.get("extensions", {})
        light_extension = extension.get(LIGHT_EXTENSION, {}) if isinstance(extension, dict) else {}
        if light_extension:
            reference = light_extension.get("light")
            if type(reference) is not int or not 0 <= reference < len(lights_root):
                raise ValueError("node light index is out of range")
            light = light_values[reference]
            normalized_lights.append({"node": node_index, **light, "transform": worlds[node_index]})
    if len(normalized_cameras) > MAX_CAMERAS or len(normalized_lights) > MAX_LIGHTS:
        raise ValueError("scene metadata exceeds the runtime bound")
    return {"cameras": normalized_cameras, "lights": normalized_lights}


def _encoded(values: tuple[float, ...]) -> str:
    return base64.b64encode(struct.pack(f"<{len(values)}f", *values)).decode("ascii")


def lines(metadata: dict) -> list[str]:
    cameras, lights = metadata["cameras"], metadata["lights"]
    result = [f"camera_count={len(cameras)}"]
    for index, camera in enumerate(cameras):
        result += [f"camera_{index}_node={camera['node']}", f"camera_{index}_projection={camera['projection']}",
            f"camera_{index}_x={camera['x']!r}", f"camera_{index}_y={camera['y']!r}",
            f"camera_{index}_near={camera['near']!r}", f"camera_{index}_far={camera['far']!r}",
            f"camera_{index}_transform_b64={_encoded(camera['transform'])}"]
    result.append(f"light_count={len(lights)}")
    for index, light in enumerate(lights):
        result += [f"light_{index}_node={light['node']}", f"light_{index}_kind={light['kind']}",
            f"light_{index}_intensity={light['intensity']!r}", f"light_{index}_range={light['range']!r}",
            f"light_{index}_inner={light['inner']!r}", f"light_{index}_outer={light['outer']!r}",
            f"light_{index}_color_b64={_encoded(light['color'])}",
            f"light_{index}_transform_b64={_encoded(light['transform'])}"]
    return result
