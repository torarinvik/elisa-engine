"""Apply bounded lossless meshoptimizer stream encoding to cooked package text."""

from __future__ import annotations

import base64

from cook_gltf_meshopt_streams import INDEX_STREAM, VERTEX_STREAM, encode_streams


def compress_core_geometry(package: bytes) -> tuple[bytes, dict[str, int]]:
    """Compress the four base geometry streams, preserving raw fallback fields."""
    try:
        lines = package.decode("ascii").splitlines()
        fields = dict(line.split("=", 1) for line in lines)
    except (UnicodeError, ValueError) as failure:
        raise ValueError("cooked geometry package is not a unique ASCII field list") from failure
    if len(fields) != len(lines):
        raise ValueError("cooked geometry package has duplicate fields")
    if "meshopt_codec" in fields or any(f"{name}_meshopt_b64" in fields for name in
            ("positions", "normals", "uvs", "indices")):
        raise ValueError("FBX cooker supplied already encoded geometry streams")
    try:
        vertex_count = int(fields["positions"])
        index_count = int(fields["indices"])
        expected_strides = {"position_stride": "12", "normal_stride": "12",
            "uv_stride": "8", "index_stride": "4"}
        if any(fields.get(name) != value for name, value in expected_strides.items()):
            raise ValueError("cooked geometry package has unsupported core stream strides")
        stream_data = {
            name: base64.b64decode(fields[f"{name}_b64"], validate=True)
            for name in ("positions", "normals", "uvs", "indices")
        }
        stream_strides = {"positions": 12, "normals": 12, "uvs": 8, "indices": 4}
        if "tangent_stride" in fields:
            if fields["tangent_stride"] != "16":
                raise ValueError("cooked geometry package has unsupported tangent stride")
            stream_data["tangents"] = base64.b64decode(fields["tangents_b64"], validate=True)
            stream_strides["tangents"] = 16
    except (KeyError, ValueError) as failure:
        raise ValueError("cooked geometry package is missing valid raw core streams") from failure
    streams = [
        (name, INDEX_STREAM if name == "indices" else VERTEX_STREAM,
            index_count if name == "indices" else vertex_count, stream_strides[name], data)
        for name, data in stream_data.items()
    ]
    encoded = encode_streams(streams)
    compressed = {name: encoded[name] for name, _kind, _count, _stride, raw in streams
        if len(encoded[name]) < len(raw)}
    if not compressed:
        return package, {"raw_bytes": sum(len(data) for data in stream_data.values()),
            "stored_bytes": sum(len(data) for data in stream_data.values()), "compressed_streams": 0}
    output_lines = []
    for line in lines:
        key = line.partition("=")[0]
        stream_name = key[:-4] if key.endswith("_b64") else ""
        if stream_name in compressed:
            output_lines.append(f"{stream_name}_meshopt_b64=" +
                base64.b64encode(compressed[stream_name]).decode("ascii"))
        else:
            output_lines.append(line)
        if key == "index_stride":
            output_lines.append("meshopt_codec=meshoptimizer-v1.2")
    output = ("\n".join(output_lines) + "\n").encode("ascii")
    return output, {"raw_bytes": sum(len(data) for data in stream_data.values()),
        "stored_bytes": sum(len(compressed.get(name, data)) for name, data in stream_data.items()),
        "compressed_streams": len(compressed)}
