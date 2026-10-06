"""Serialize a normalized skin rig and its clips into the cooked animation contract.

Format ``elisa-anim-v1`` (little-endian, read by ``src/animation/package.elisa``):

- header, 32 bytes: magic ``EANM``, version 1, total byte count, FNV-1a 32
  checksum of every byte from offset 16 to the end, rig id, joint count (1..64),
  clip count (0..24), metres per unit (f32, always 1.0);
- one 112-byte joint record per joint, parents first: joint id (FNV-1a of the
  joint name), parent index (-1 for a root), rest local TRS as ten f32
  (translation xyz, rotation xyzw, scale xyz), inverse bind as sixteen f32
  in column-major order;
- per clip, a 24-byte header (clip id, rig id, duration ticks, ticks per
  second, track count, event count), one track per joint in joint order
  (joint index, key count, then key count * (tick u32 + ten f32)), and
  event count * (event id, tick).

The rig id is the FNV-1a hash of the joint records, so a clip names exactly the
skeleton (identities, hierarchy, rest and bind pose) it was cooked against.
"""

from __future__ import annotations

import math
import struct

MAGIC = 0x4D4E4145
VERSION = 1
HEADER_BYTES = 32
JOINT_BYTES = 112
CLIP_HEADER_BYTES = 24
TRACK_HEADER_BYTES = 8
KEY_BYTES = 44
EVENT_BYTES = 8
MAX_JOINTS = 64
MAX_CLIPS = 24
MAX_KEYS_PER_CLIP = 262144
MAX_EVENTS_PER_CLIP = 32
MAX_EVENT_ID = 65535
MAX_BYTES = 64 * 1024 * 1024
BIND_TOLERANCE = 1.0e-3


def fnv1a(data: bytes) -> int:
    value = 0x811C9DC5
    for byte in data:
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return value


def stable_id(data: bytes) -> int:
    value = fnv1a(data)
    return value if value != 0 else 1


def _f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _quat_mul(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def _quat_rotate(q: tuple[float, ...], v: tuple[float, ...]) -> tuple[float, ...]:
    x, y, z, w = _quat_mul(_quat_mul(q, (*v, 0.0)), (-q[0], -q[1], -q[2], q[3]))
    return (x, y, z)


def _normalized(q: tuple[float, ...]) -> tuple[float, ...]:
    length = math.sqrt(sum(c * c for c in q))
    if not math.isfinite(length) or length <= 1.0e-12:
        raise ValueError("joint rest rotation has zero length")
    return tuple(c / length for c in q)


def _compose(parent: tuple[float, ...], child: tuple[float, ...]) -> tuple[float, ...]:
    """Match Geometry::transform_compose, which Pose::pose_evaluate uses."""
    scaled = tuple(parent[7 + axis] * child[axis] for axis in range(3))
    rotated = _quat_rotate(parent[3:7], scaled)
    position = tuple(parent[axis] + rotated[axis] for axis in range(3))
    rotation = _normalized(_quat_mul(parent[3:7], child[3:7]))
    scale = tuple(parent[7 + axis] * child[7 + axis] for axis in range(3))
    return (*position, *rotation, *scale)


def _matrix(value: tuple[float, ...]) -> list[float]:
    """Match Geometry::transform_to_matrix (column-major)."""
    x, y, z, w = _normalized(value[3:7])
    sx, sy, sz = value[7:10]
    return [(1 - 2 * (y * y + z * z)) * sx, 2 * (x * y + w * z) * sx, 2 * (x * z - w * y) * sx, 0.0,
        2 * (x * y - w * z) * sy, (1 - 2 * (x * x + z * z)) * sy, 2 * (y * z + w * x) * sy, 0.0,
        2 * (x * z + w * y) * sz, 2 * (y * z - w * x) * sz, (1 - 2 * (x * x + y * y)) * sz, 0.0,
        value[0], value[1], value[2], 1.0]


def _inverse(matrix: list[float]) -> list[float]:
    rows = [[matrix[column * 4 + row] for column in range(4)] + [1.0 if row == other else 0.0
        for other in range(4)] for row in range(4)]
    for column in range(4):
        pivot = max(range(column, 4), key=lambda row: abs(rows[row][column]))
        if abs(rows[pivot][column]) <= 1.0e-12:
            raise ValueError("joint rest pose is singular")
        rows[column], rows[pivot] = rows[pivot], rows[column]
        scale = rows[column][column]
        rows[column] = [value / scale for value in rows[column]]
        for row in range(4):
            if row != column and rows[row][column] != 0.0:
                factor = rows[row][column]
                rows[row] = [a - factor * b for a, b in zip(rows[row], rows[column])]
    return [rows[row][4 + column] for column in range(4) for row in range(4)]


def joint_records(skin: dict) -> bytes:
    joints = skin["joints"]
    if not 1 <= len(joints) <= MAX_JOINTS:
        raise ValueError(f"animation contract supports 1 to {MAX_JOINTS} joints")
    names = [joint["name"] for joint in joints]
    ids = [stable_id(name.encode("utf-8")) for name in names]
    if len(set(ids)) != len(ids):
        raise ValueError("animation contract joint names must have distinct identities")
    authored = skin.get("inverse_bind_matrices")
    bone_joint = {}
    if authored is not None:
        for bone, joint in enumerate(skin["cluster_joints"]):
            bone_joint[joint] = authored[bone * 16:bone * 16 + 16]
    models: list[tuple[float, ...]] = []
    records = bytearray()
    for index, joint in enumerate(joints):
        parent = joint["parent"]
        if type(parent) is not int or not -1 <= parent < index:
            raise ValueError("animation contract joints must list parents first")
        rest = tuple(float(component) for component in joint["rest"])
        if len(rest) != 10 or not all(math.isfinite(component) for component in rest):
            raise ValueError("joint rest transform must be ten finite floats")
        rest = (*rest[:3], *_normalized(rest[3:7]), *rest[7:10])
        model = rest if parent < 0 else _compose(models[parent], rest)
        models.append(model)
        inverse_bind = _inverse(_matrix(model))
        if index in bone_joint and any(abs(a - b) > BIND_TOLERANCE
                for a, b in zip(inverse_bind, bone_joint[index])):
            raise ValueError(f"joint {names[index]} inverse bind disagrees with its rest pose")
        records += struct.pack("<Ii10f16f", ids[index], parent, *rest, *inverse_bind)
    return bytes(records)


def clip_record(clip: dict, rig_id: int, joint_count: int) -> bytes:
    frames = clip["frames"]
    samples = clip["samples"]
    if frames < 2 or len(samples) != frames * joint_count * 10 or clip["sample_rate"] <= 0:
        raise ValueError("normalized clip does not match the rig")
    if frames * joint_count > MAX_KEYS_PER_CLIP:
        raise ValueError("animation clip exceeds the per-clip key bound")
    events = clip.get("events", [])
    if not isinstance(events, list) or len(events) > MAX_EVENTS_PER_CLIP:
        raise ValueError("animation clip exceeds the per-clip event bound")
    seen_events: set[int] = set()
    previous_tick = -1
    for event in events:
        if not isinstance(event, dict) or set(event) != {"id", "tick"}:
            raise ValueError("animation event must contain only id and tick")
        event_id, tick = event["id"], event["tick"]
        if (type(event_id) is not int or not 1 <= event_id <= MAX_EVENT_ID or
                type(tick) is not int or not 0 <= tick <= frames - 1 or tick < previous_tick or
                event_id in seen_events):
            raise ValueError("animation event id or tick is invalid")
        seen_events.add(event_id)
        previous_tick = tick
    record = bytearray(struct.pack("<6I", stable_id(clip["name"].encode("utf-8")), rig_id,
        frames - 1, clip["sample_rate"], joint_count, len(events)))
    for joint in range(joint_count):
        record += struct.pack("<2I", joint, frames)
        for frame in range(frames):
            start = (frame * joint_count + joint) * 10
            record += struct.pack("<I10f", frame, *(_f32(value) for value in samples[start:start + 10]))
    for event in events:
        record += struct.pack("<2I", event["id"], event["tick"])
    return bytes(record)


def encode(skin: dict | None, clips: list[dict]) -> bytes:
    """Return the cooked contract bytes for one skin rig and its clips."""
    if skin is None:
        raise ValueError("animation contract needs a skinned rig")
    if len(clips) > MAX_CLIPS:
        raise ValueError(f"animation contract supports at most {MAX_CLIPS} clips")
    joints = joint_records(skin)
    joint_count = len(joints) // JOINT_BYTES
    rig_id = stable_id(joints)
    body = bytearray(joints)
    clip_ids = set()
    for clip in clips:
        record = clip_record(clip, rig_id, joint_count)
        clip_id = struct.unpack_from("<I", record)[0]
        if clip_id in clip_ids:
            raise ValueError("animation contract clip names must have distinct identities")
        clip_ids.add(clip_id)
        body += record
    total = HEADER_BYTES + len(body)
    if total > MAX_BYTES:
        raise ValueError("animation contract exceeds the 64 MiB package bound")
    tail = struct.pack("<3If", rig_id, joint_count, len(clips), 1.0) + bytes(body)
    return struct.pack("<4I", MAGIC, VERSION, total, fnv1a(tail)) + tail
