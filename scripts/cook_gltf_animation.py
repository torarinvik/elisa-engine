"""Normalize bounded glTF joint animation into fixed-rate runtime clips."""

from __future__ import annotations

import bisect
import math
import struct

import cook_assets

SAMPLE_RATE = 30
MAX_CLIPS = 16
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


def _normalize_quaternion(value: tuple[float, ...], label: str) -> tuple[float, ...]:
    length = math.sqrt(sum(component * component for component in value))
    if not math.isfinite(length) or length <= 1.0e-12:
        raise ValueError(f"{label} contains a zero-length rotation")
    return tuple(component / length for component in value)


def _sample(track: tuple[list[float], list[tuple[float, ...]], str], time: float,
        label: str) -> tuple[float, ...]:
    times, values, interpolation = track
    if time <= times[0]:
        return values[0]
    if time >= times[-1]:
        return values[-1]
    right = bisect.bisect_right(times, time)
    left = right - 1
    if interpolation == "STEP":
        return values[left]
    span = times[right] - times[left]
    amount = (time - times[left]) / span
    first, second = values[left], values[right]
    if len(first) == 4:
        if sum(a * b for a, b in zip(first, second)) < 0.0:
            second = tuple(-component for component in second)
        result = tuple(a + (b - a) * amount for a, b in zip(first, second))
        return _normalize_quaternion(result, label)
    return tuple(a + (b - a) * amount for a, b in zip(first, second))


def _track(document: dict, buffer: bytes, sampler: dict, path: str,
        label: str) -> tuple[list[float], list[tuple[float, ...]], str]:
    input_reference = sampler.get("input")
    output_reference = sampler.get("output")
    output_type = "VEC4" if path == "rotation" else "VEC3"
    times = [value[0] for value in _values(document, buffer, input_reference, "SCALAR", f"{label} input")]
    values = _values(document, buffer, output_reference, output_type, f"{label} output")
    if not times or len(times) != len(values) or times[0] < 0.0 or any(
            right <= left for left, right in zip(times, times[1:])):
        raise ValueError(f"{label} key times must be finite and strictly increasing")
    interpolation = sampler.get("interpolation", "LINEAR")
    if interpolation not in ("LINEAR", "STEP"):
        raise ValueError(f"{label} interpolation must be LINEAR or STEP")
    if path == "rotation":
        values = [_normalize_quaternion(value, f"{label} rotation") for value in values]
    elif path == "scale" and any(component == 0.0 for value in values for component in value):
        raise ValueError(f"{label} scale must be nonzero")
    return times, values, interpolation


def normalize(document: dict, buffer: bytes, ordered_index: dict[int, int | list[int]],
        rest: list[tuple[float, ...]]) -> list[dict]:
    animations = document.get("animations", [])
    if not animations:
        return []
    if not isinstance(animations, list) or len(animations) > MAX_CLIPS:
        raise ValueError(f"runtime glTF cooker supports 1 to {MAX_CLIPS} animation clips")
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
            if type(node) is not int or node not in ordered_index or path not in ("translation", "rotation", "scale"):
                raise ValueError(f"{channel_label} must target a skin rig-node TRS path")
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
        duration = max(track[0][-1] for track in tracks.values())
        if not math.isfinite(duration) or duration <= 0.0 or duration > MAX_DURATION:
            raise ValueError(f"{label} duration exceeds the bounded runtime range")
        frame_count = max(2, math.ceil(duration * SAMPLE_RATE) + 1)
        samples: list[float] = []
        for frame in range(frame_count):
            time = min(duration, frame / SAMPLE_RATE)
            for joint_index, base in enumerate(rest):
                values = [base[:3], base[3:7], base[7:10]]
                for path_index, path in enumerate(("translation", "rotation", "scale")):
                    track = tracks.get((joint_index, path))
                    if track is not None:
                        values[path_index] = _sample(track, time, f"{label} joint {joint_index} {path}")
                values[1] = _normalize_quaternion(tuple(values[1]), f"{label} joint {joint_index} rotation")
                samples.extend((*values[0], *values[1], *values[2]))
        clips.append({"name": name, "duration": duration, "sample_rate": SAMPLE_RATE,
            "frames": frame_count, "samples": samples})
    return clips
