"""Normalize bounded glTF joint animation into fixed-rate runtime clips."""

from __future__ import annotations

import base64
import bisect
import math
import struct

import cook_assets
import cook_gltf_nodes

SAMPLE_RATE = 30
MAX_CLIPS = 24
MAX_FRAMES = 3601
MAX_DURATION = (MAX_FRAMES - 1) / SAMPLE_RATE
ANIMATION_KEYS = {"name", "samplers", "channels"}
SAMPLER_KEYS = {"input", "output", "interpolation"}
CHANNEL_KEYS = {"sampler", "target"}
TARGET_KEYS = {"node", "path"}


def _accessor(document: dict, index: int, type_name: str, label: str) -> dict:
    accessors = document.get("accessors", [])
    if type(index) is not int or not 0 <= index < len(accessors):
        raise ValueError(f"{label} accessor index is out of range")
    value = accessors[index]
    if not isinstance(value, dict) or value.get("type") != type_name:
        raise ValueError(f"{label} accessor must be {type_name}")
    if value.get("componentType") != 5126 or value.get("normalized", False):
        raise ValueError(f"{label} accessor must be unnormalized float32")
    return value


def _values(document: dict, buffer: bytes, reference: int, type_name: str,
        label: str) -> list[tuple[float, ...]]:
    accessor = _accessor(document, reference, type_name, label)
    components = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[type_name]
    data = cook_assets.accessor_bytes(document, buffer, reference)
    values = [tuple(item) for item in struct.iter_unpack(f"<{components}f", data)]
    if len(values) != accessor.get("count") or any(not math.isfinite(value)
            for item in values for value in item):
        raise ValueError(f"{label} contains non-finite values")
    return values


def _weight_values(document: dict, buffer: bytes, reference: int, label: str) -> list[tuple[float, ...]]:
    accessors = document.get("accessors", [])
    if type(reference) is not int or not 0 <= reference < len(accessors):
        raise ValueError(f"{label} accessor is out of range")
    accessor = accessors[reference]
    if not isinstance(accessor, dict) or accessor.get("type") != "SCALAR":
        raise ValueError(f"{label} accessor must be SCALAR")
    component = accessor.get("componentType")
    normalized = accessor.get("normalized", False)
    formats = {5120: ("<b", 127.0, True), 5121: ("<B", 255.0, False),
        5122: ("<h", 32767.0, True), 5123: ("<H", 65535.0, False)}
    if component == 5126:
        if normalized:
            raise ValueError(f"{label} float accessor cannot be normalized")
        raw = [value[0] for value in struct.iter_unpack("<f", cook_assets.accessor_bytes(document, buffer, reference))]
        values = raw
    elif component in formats and normalized is True:
        format_string, maximum, signed = formats[component]
        raw = [value[0] for value in struct.iter_unpack(format_string,
            cook_assets.accessor_bytes(document, buffer, reference))]
        values = [max(-1.0, value / maximum) if signed else value / maximum for value in raw]
    else:
        raise ValueError(f"{label} accessor must be float32 or normalized 8/16-bit weights")
    if len(values) != accessor.get("count") or any(not math.isfinite(value) for value in values):
        raise ValueError(f"{label} contains non-finite values")
    return [(value,) for value in values]


def _normalize_quaternion(value: tuple[float, ...], label: str) -> tuple[float, ...]:
    length = math.sqrt(sum(component * component for component in value))
    if not math.isfinite(length) or length <= 1.0e-12:
        raise ValueError(f"{label} contains a zero-length rotation")
    return tuple(component / length for component in value)


def _sample(track: tuple[list[float], list[tuple[float, ...]], str], time: float,
        label: str, quaternion: bool = False) -> tuple[float, ...]:
    times, values, interpolation = track
    cubic = interpolation == "CUBICSPLINE"
    value_at = lambda index: values[index][1] if cubic else values[index]
    if time <= times[0]:
        return value_at(0)
    if time >= times[-1]:
        return value_at(len(times) - 1)
    right = bisect.bisect_right(times, time)
    left = right - 1
    if interpolation == "STEP":
        return value_at(left)
    span = times[right] - times[left]
    amount = (time - times[left]) / span
    if cubic:
        incoming, first, outgoing = values[left]
        next_incoming, second, _ = values[right]
        squared = amount * amount
        cubed = squared * amount
        result = tuple((2.0 * cubed - 3.0 * squared + 1.0) * start +
            (cubed - 2.0 * squared + amount) * span * out_tangent +
            (-2.0 * cubed + 3.0 * squared) * end +
            (cubed - squared) * span * in_tangent
            for start, out_tangent, end, in_tangent in
                zip(first, outgoing, second, next_incoming))
        return _normalize_quaternion(result, label) if quaternion else result
    first, second = values[left], values[right]
    if quaternion:
        dot = sum(a * b for a, b in zip(first, second))
        if dot < 0.0:
            second = tuple(-component for component in second)
            dot = -dot
        if dot > 0.9995:
            result = tuple(a + (b - a) * amount for a, b in zip(first, second))
        else:
            angle = math.acos(max(-1.0, min(1.0, dot)))
            sine = math.sin(angle)
            first_weight = math.sin((1.0 - amount) * angle) / sine
            second_weight = math.sin(amount * angle) / sine
            result = tuple(a * first_weight + b * second_weight
                for a, b in zip(first, second))
        return _normalize_quaternion(result, label)
    return tuple(a + (b - a) * amount for a, b in zip(first, second))


def _track(document: dict, buffer: bytes, sampler: dict, path: str,
        label: str, morph_count: int = 0) -> tuple[list[float], list[tuple[float, ...]], str]:
    input_reference = sampler.get("input")
    output_reference = sampler.get("output")
    interpolation = sampler.get("interpolation", "LINEAR")
    if interpolation not in ("LINEAR", "STEP", "CUBICSPLINE"):
        raise ValueError(f"{label} interpolation must be LINEAR, STEP, or CUBICSPLINE")
    input_accessor = _accessor(document, input_reference, "SCALAR", f"{label} input")
    times = [value[0] for value in _values(document, buffer, input_reference, "SCALAR", f"{label} input")]
    if not times:
        raise ValueError(f"{label} input must contain at least one key time")
    bounds = (input_accessor.get("min"), input_accessor.get("max"))
    if any(not isinstance(bound, list) or len(bound) != 1 or
            type(bound[0]) not in (int, float) or not math.isfinite(bound[0]) or
            abs(bound[0]) > cook_gltf_nodes.FLOAT_MAX
            for bound in bounds):
        raise ValueError(f"{label} input accessor must define scalar min and max bounds")
    rounded_bounds = tuple(struct.unpack("<f", struct.pack("<f", bound[0]))[0]
        for bound in bounds)
    if rounded_bounds != (min(times), max(times)):
        raise ValueError(f"{label} input accessor bounds do not match its key times")
    if path == "weights":
        if morph_count <= 0:
            raise ValueError(f"{label} targets a mesh without morph targets")
        flat = _weight_values(document, buffer, output_reference, f"{label} output")
        sample_group = 3 if interpolation == "CUBICSPLINE" else 1
        if len(flat) != len(times) * morph_count * sample_group:
            raise ValueError(f"{label} morph weights do not match the key and target counts")
        values = []
        for key in range(len(times)):
            groups = [tuple(item[0] for item in flat[(key * sample_group + group) * morph_count:
                (key * sample_group + group + 1) * morph_count]) for group in range(sample_group)]
            values.append(tuple(groups) if sample_group == 3 else groups[0])
    else:
        output_type = "VEC4" if path == "rotation" else "VEC3"
        values = _values(document, buffer, output_reference, output_type, f"{label} output")
        sample_group = 3 if interpolation == "CUBICSPLINE" else 1
        if len(values) != len(times) * sample_group:
            raise ValueError(f"{label} output count does not match its input keys")
        if sample_group == 3:
            values = [tuple(values[key * 3:key * 3 + 3]) for key in range(len(times))]
    if not times or len(times) != len(values) or times[0] < 0.0 or any(
            right <= left for left, right in zip(times, times[1:])):
        raise ValueError(f"{label} key times must be finite and strictly increasing")
    if path == "rotation":
        if interpolation == "CUBICSPLINE":
            values = [(incoming, _normalize_quaternion(value, f"{label} rotation"), outgoing)
                for incoming, value, outgoing in values]
        else:
            values = [_normalize_quaternion(value, f"{label} rotation") for value in values]
    elif path == "scale" and any(component == 0.0 for value in values
            for component in (value[1] if interpolation == "CUBICSPLINE" else value)):
        raise ValueError(f"{label} scale must be nonzero")
    return times, values, interpolation


def morph_defaults(document: dict, placement_records: list[tuple], morph_count: int) -> list[float]:
    """Return authored node/mesh defaults in cooked placement order."""
    if morph_count == 0:
        return []
    defaults = []
    meshes = document.get("meshes", [])
    nodes = document.get("nodes", [])
    for mesh_index, node_index, _ in placement_records:
        node = nodes[node_index]
        mesh = meshes[mesh_index]
        values = node.get("weights", mesh.get("weights", [0.0] * morph_count))
        if (not isinstance(values, list) or len(values) != morph_count or
                any(type(value) not in (int, float) or not math.isfinite(value) or
                    abs(value) > cook_gltf_nodes.FLOAT_MAX for value in values)):
            raise ValueError("morph weights must contain one finite number per target")
        defaults.extend(float(value) for value in values)
    return defaults


def normalize(document: dict, buffer: bytes, ordered_index: dict[int, int | list[int]],
        rest: list[tuple[float, ...]], placement_records: list[tuple] | None = None,
        morph_count: int = 0) -> list[dict]:
    animations = document.get("animations", [])
    if not animations:
        return []
    if not isinstance(animations, list) or len(animations) > MAX_CLIPS:
        raise ValueError(f"runtime glTF cooker supports 1 to {MAX_CLIPS} animation clips")
    placements = [] if placement_records is None else placement_records
    node_placements = {node_index: placement for placement, (_, node_index, _) in enumerate(placements)}
    default_weights = morph_defaults(document, placements, morph_count)
    clips: list[dict] = []
    names: set[str] = set()
    for animation_index, animation in enumerate(animations):
        label = f"animation {animation_index}"
        if not isinstance(animation, dict) or set(animation) - ANIMATION_KEYS:
            raise ValueError(f"{label} has unsupported properties")
        name = animation.get("name", f"animation_{animation_index}")
        if not isinstance(name, str) or not name or name in names:
            raise ValueError(f"{label} name must be unique and nonempty")
        names.add(name)
        samplers = animation.get("samplers", [])
        channels = animation.get("channels", [])
        if not isinstance(samplers, list) or not isinstance(channels, list) or not channels:
            raise ValueError(f"{label} needs at least one channel")
        tracks: dict[tuple[int, str], tuple[list[float], list[tuple[float, ...]], str]] = {}
        morph_tracks: dict[int, tuple[list[float], list[tuple[float, ...]], str]] = {}
        for channel_index, channel in enumerate(channels):
            channel_label = f"{label} channel {channel_index}"
            if not isinstance(channel, dict) or set(channel) - CHANNEL_KEYS:
                raise ValueError(f"{channel_label} has unsupported properties")
            sampler_index = channel.get("sampler")
            if (type(sampler_index) is not int or not 0 <= sampler_index < len(samplers) or
                    not isinstance(samplers[sampler_index], dict) or
                    set(samplers[sampler_index]) - SAMPLER_KEYS):
                raise ValueError(f"{channel_label} sampler is invalid")
            target = channel.get("target")
            if not isinstance(target, dict) or set(target) - TARGET_KEYS:
                raise ValueError(f"{channel_label} target is invalid")
            node = target.get("node")
            path = target.get("path")
            if type(node) is not int:
                raise ValueError(f"{channel_label} target node is invalid")
            source_nodes = document.get("nodes", [])
            if node < 0 or node >= len(source_nodes) or not isinstance(source_nodes[node], dict):
                raise ValueError(f"{channel_label} target node is out of range")
            if path == "weights":
                if node not in node_placements or morph_count == 0:
                    raise ValueError(f"{channel_label} must target a placed mesh with morph targets")
                placement = node_placements[node]
                if placement in morph_tracks:
                    raise ValueError(f"{channel_label} duplicates a mesh morph-weight track")
                morph_tracks[placement] = _track(document, buffer, samplers[sampler_index],
                    path, channel_label, morph_count)
                continue
            if path not in ("translation", "rotation", "scale") or node not in ordered_index:
                raise ValueError(f"{channel_label} must target a skin rig-node TRS path or mesh weights")
            if "matrix" in source_nodes[node]:
                raise ValueError(f"{channel_label} cannot animate TRS on a matrix-authored node")
            rig_indices = ordered_index[node]
            if type(rig_indices) is int:
                rig_indices = [rig_indices]
            if not rig_indices or any(type(index) is not int or not 0 <= index < len(rest)
                    for index in rig_indices):
                raise ValueError(f"{channel_label} targets an invalid skin rig node")
            if any((index, path) in tracks for index in rig_indices):
                raise ValueError(f"{channel_label} duplicates a skin rig-node TRS track")
            track = _track(document, buffer, samplers[sampler_index], path, channel_label)
            for rig_index in rig_indices:
                tracks[(rig_index, path)] = track
        all_tracks = [*tracks.values(), *morph_tracks.values()]
        if not all_tracks:
            raise ValueError(f"{label} has no supported animation channels")
        duration = max(track[0][-1] for track in all_tracks)
        if not math.isfinite(duration) or duration <= 0.0 or duration > MAX_DURATION:
            raise ValueError(f"{label} duration exceeds the bounded runtime range")
        frame_count = max(2, math.ceil(duration * SAMPLE_RATE) + 1)
        samples: list[float] = []
        morph_samples: list[float] = []
        for frame in range(frame_count):
            time = min(duration, frame / SAMPLE_RATE)
            for joint_index, base in enumerate(rest):
                values = [base[:3], base[3:7], base[7:10]]
                for path_index, path in enumerate(("translation", "rotation", "scale")):
                    track = tracks.get((joint_index, path))
                    if track is not None:
                        values[path_index] = _sample(track, time,
                            f"{label} joint {joint_index} {path}", path == "rotation")
                values[1] = _normalize_quaternion(tuple(values[1]), f"{label} joint {joint_index} rotation")
                samples.extend((*values[0], *values[1], *values[2]))
            for placement in range(len(placements)):
                track = morph_tracks.get(placement)
                if track is None:
                    start = placement * morph_count
                    morph_samples.extend(default_weights[start:start + morph_count])
                else:
                    morph_samples.extend(_sample(track, time, f"{label} placement {placement} weights"))
        clips.append({"name": name, "duration": duration, "sample_rate": SAMPLE_RATE,
            "frames": frame_count, "samples": samples, "morph_weights": morph_samples})
    return clips


def package_lines(clips: list[dict], joint_count: int, placement_count: int,
        morph_count: int) -> list[str]:
    """Encode validated clips for the bounded cooked-geometry package."""
    lines = [f"animation_clips={len(clips)}"]
    if len(clips) > MAX_CLIPS:
        raise ValueError("normalized animation clip count exceeds the runtime bound")
    for index, clip in enumerate(clips):
        samples = clip["samples"]
        morph_samples = clip["morph_weights"]
        frames = clip["frames"]
        if (not isinstance(clip["name"], str) or not clip["name"] or frames < 2 or frames > MAX_FRAMES or
                clip["sample_rate"] <= 0 or len(samples) != joint_count * frames * 10 or
                len(morph_samples) != placement_count * morph_count * frames):
            raise ValueError("normalized animation clip does not match its rig and morph placements")
        name = clip["name"].encode("utf-8")
        if len(name) > 0xFFFFFFFF:
            raise ValueError("animation name is too long")
        encoded_name = base64.b64encode(struct.pack("<I", len(name)) + name).decode("ascii")
        lines += [f"animation_{index}_name_b64={encoded_name}",
            f"animation_{index}_duration_seconds={clip['duration']!r}",
            f"animation_{index}_sample_rate={clip['sample_rate']}",
            f"animation_{index}_frames={frames}"]
        if joint_count:
            lines += [f"animation_{index}_transform_stride=40",
                f"animation_{index}_samples_b64=" + base64.b64encode(
                    struct.pack(f"<{len(samples)}f", *samples)).decode("ascii")]
        if morph_count:
            lines += [f"animation_{index}_morph_stride=4",
                f"animation_{index}_morph_samples_b64=" + base64.b64encode(
                    struct.pack(f"<{len(morph_samples)}f", *morph_samples)).decode("ascii")]
    return lines
