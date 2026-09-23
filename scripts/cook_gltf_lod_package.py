"""Cook immutable static glTF LOD packages and an atomic chain manifest."""

from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import tempfile

import cook_gltf_geometry
from elisa_package import write_geometry_package


MAX_LEVELS = 8
MANIFEST_FORMAT = "elisa-lod-chain-v1"
ERROR_METRIC = "meshoptimizer-relative-geometric-error"


def parse_lod_ratios(value: str) -> list[float]:
    parts = value.split(",")
    if not parts or len(parts) >= MAX_LEVELS or any(not part.strip() for part in parts):
        raise ValueError(f"LOD chain requires 1..{MAX_LEVELS - 1} simplification ratios")
    ratios = []
    for part in parts:
        try:
            ratio = float(part)
        except ValueError:
            raise ValueError(f"invalid LOD simplification ratio: {part!r}") from None
        if not math.isfinite(ratio) or not 0.0 < ratio < 1.0:
            raise ValueError("LOD simplification ratios must be finite and between 0 and 1")
        if ratios and ratio >= ratios[-1]:
            raise ValueError("LOD simplification ratios must be strictly descending")
        ratios.append(ratio)
    return ratios


def _atomic_write(path: Path, data: bytes) -> None:
    temporary_name = ""
    try:
        with tempfile.NamedTemporaryFile(prefix=f".{path.name}.", suffix=".tmp",
                dir=path.parent, delete=False) as output:
            temporary_name = output.name
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary_name, 0o644)
        os.replace(temporary_name, path)
    finally:
        if temporary_name and os.path.exists(temporary_name):
            os.unlink(temporary_name)


def cook_lod_chain(source_path: Path, asset_path: str, output_base: Path,
        ratios: list[float], textures: dict[str, Path] | None = None,
        dependencies: list[str] | None = None, generate_lightmap_uv: bool = False,
        lightmap_resolution: int = 1024, lightmap_padding: int = 4) -> tuple[Path, list[dict]]:
    """Cook full and reduced packages; publish the manifest only after all levels."""
    import cook_gltf_package

    asset_path = cook_gltf_geometry.safe_asset_path(asset_path)
    output_base = output_base.expanduser().resolve()
    suffix = output_base.suffix.lower()
    if suffix not in (".pkg", ".elpk"):
        raise ValueError("LOD output base must end in .pkg or .elpk")
    if ratios != parse_lod_ratios(",".join(str(ratio) for ratio in ratios)):
        raise ValueError("LOD ratios are not in canonical descending order")
    textures = textures or {}
    dependencies = dependencies or []
    if (textures or dependencies) and suffix != ".elpk":
        raise ValueError("textures and dependencies in an LOD chain require an .elpk output base")

    manifest_path = output_base.with_name(f"{output_base.stem}.lod.json")
    output_base.parent.mkdir(parents=True, exist_ok=True)
    pending: list[tuple[Path, bytes]] = []
    levels = []
    previous_error = 0.0
    seen_digests = set()
    source_digest = ""
    object_extent = 0.0
    with tempfile.TemporaryDirectory(prefix="elisa-gltf-lod-chain-",
            dir=output_base.parent) as temporary_name:
        temporary = Path(temporary_name)
        for level_index, ratio in enumerate([None, *ratios]):
            geometry_path, result = cook_gltf_package.cook_geometry_package(
                source_path, asset_path, temporary / f"geometry-{level_index:02d}.pkg",
                allow_textures=suffix == ".elpk", simplify_ratio=ratio,
                generate_lightmap_uv=generate_lightmap_uv,
                lightmap_resolution=lightmap_resolution, lightmap_padding=lightmap_padding)
            images = result["images"]
            if images.keys() & textures.keys():
                raise ValueError("external texture names overlap source material image sections")
            images.update((name, path.read_bytes()) for name, path in textures.items())
            if suffix == ".elpk":
                package_path = temporary / f"level-{level_index:02d}.elpk"
                write_geometry_package(package_path, geometry_path.read_bytes(), images, dependencies)
            else:
                if images:
                    raise ValueError("material textures require an .elpk LOD chain")
                package_path = geometry_path

            package_bytes = package_path.read_bytes()
            package_digest = hashlib.sha256(package_bytes).hexdigest()
            if package_digest in seen_digests:
                raise ValueError("two LOD levels produced identical package bytes")
            seen_digests.add(package_digest)
            if source_digest and source_digest != result["source_sha256"]:
                raise ValueError("LOD levels were cooked from different source generations")
            source_digest = result["source_sha256"]
            if level_index == 0:
                object_extent = result["position_extent"]
            error = 0.0 if result["lod"] is None else result["lod"]["maximum_error"]
            relative_error_budget = max(previous_error, error)
            previous_error = relative_error_budget
            package_name = (f"{output_base.stem}.lod-{level_index:02d}-"
                f"{package_digest[:16]}{suffix}")
            target = output_base.parent / package_name
            pending.append((target, package_bytes))
            levels.append({
                "index": level_index,
                "package": package_name,
                "sha256": package_digest,
                "byte_size": len(package_bytes),
                "triangles": result["triangles"],
                "vertices": result["positions"],
                "attribute_bytes": result["attribute_bytes"],
                "simplify_ratio": 1.0 if ratio is None else ratio,
                "relative_error": error,
                "relative_error_budget": relative_error_budget,
            })

        for target, package_bytes in pending:
            if target.exists() and target.read_bytes() != package_bytes:
                raise ValueError(f"content-addressed LOD package collision: {target.name}")
            if not target.exists():
                _atomic_write(target, package_bytes)
        manifest = {
            "format": MANIFEST_FORMAT,
            "asset_path": asset_path,
            "source_sha256": source_digest,
            "object_extent": object_extent,
            "error_metric": ERROR_METRIC,
            "level_count": len(levels),
            "levels": levels,
        }
        manifest_bytes = (json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
        if len(manifest_bytes) > 1024 * 1024:
            raise ValueError("LOD chain manifest exceeds its 1 MiB bound")
        _atomic_write(manifest_path, manifest_bytes)
    return manifest_path, levels


def self_test() -> int:
    """Verify deterministic variants, bounded metadata and ratio rejection."""
    import cook_gltf_lod

    import sys

    with tempfile.TemporaryDirectory(prefix="elisa-lod-chain-test-") as temporary_name:
        temporary = Path(temporary_name)
        source = cook_gltf_lod.write_test_source(temporary)
        base = temporary / "grid.pkg"
        try:
            manifest_path, levels = cook_lod_chain(source, "test/lod-grid.gltf", base,
                parse_lod_ratios("0.5,0.25"))
            manifest_bytes = manifest_path.read_bytes()
            manifest = json.loads(manifest_bytes)
            if (manifest.get("format") != MANIFEST_FORMAT or
                    manifest.get("error_metric") != ERROR_METRIC or len(levels) != 3 or
                    not math.isfinite(manifest.get("object_extent", 0.0)) or
                    manifest["object_extent"] <= 0.0 or
                    manifest.get("level_count") != len(levels) or
                    manifest.get("levels") != levels or
                    [level["index"] for level in levels] != [0, 1, 2] or
                    not levels[0]["triangles"] > levels[1]["triangles"] > levels[2]["triangles"] or
                    any(levels[index]["relative_error_budget"] > levels[index + 1]["relative_error_budget"]
                        for index in range(len(levels) - 1))):
                raise ValueError("LOD manifest levels or error thresholds are invalid")
            for level in levels:
                package_path = temporary / level["package"]
                package_bytes = package_path.read_bytes()
                if (len(package_bytes) != level["byte_size"] or
                        hashlib.sha256(package_bytes).hexdigest() != level["sha256"]):
                    raise ValueError("LOD package size or content digest does not match the manifest")
            repeated_path, repeated_levels = cook_lod_chain(source, "test/lod-grid.gltf", base,
                parse_lod_ratios("0.5,0.25"))
            if (repeated_path != manifest_path or repeated_levels != levels or
                    repeated_path.read_bytes() != manifest_bytes):
                raise ValueError("repeated LOD chain cook changed package identities or manifest bytes")
        except (OSError, RuntimeError, ValueError, KeyError, TypeError) as failure:
            print(f"glTF LOD package self-test failed: {failure}", file=sys.stderr)
            return 1
        for invalid in ("", "1.0", "0", "nan", "0.25,0.5", "0.5,0.5", "0.5,"):
            try:
                parse_lod_ratios(invalid)
            except ValueError:
                continue
            print(f"glTF LOD package self-test failed: accepted ratios {invalid!r}", file=sys.stderr)
            return 1
    print(f"glTF LOD package self-test passed: {len(levels)} deterministic levels, "
        f"{levels[0]['triangles']} -> {levels[-1]['triangles']} triangles; manifests and content hashes verified")
    return 0
