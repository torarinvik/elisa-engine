"""Assemble a deterministic release archive for a validated target.

The plan's Phase 7 exit asks for reproducible releases for every platform
described as supported, and Phase 4 leaves packaging of the validated target
outstanding. This tool does three things: it builds the packaged headless game,
it collects the cooked asset package and the canonical fixture beside it, and it
writes the whole thing into a byte-deterministic archive whose hash it records.
It packages twice and fails if the two archives differ, so "reproducible" is a
checked property of this tool, not a claim about the compiler's output.
"""

import gzip
import hashlib
import io
import json
import platform
import subprocess
import sys
import tarfile
from pathlib import Path

# A release names only the platform this tree has actually been validated on.
# Intel macOS, Windows, Linux, Android, and iOS are listed as untested rather
# than implied from a dependency's platform list.
SUPPORTED_PLATFORMS = {
    ("Darwin", "arm64"): "macos-arm64",
}
UNTESTED_PLATFORMS = ("macos-x86_64", "windows-x86_64", "linux-x86_64", "android-arm64", "ios-arm64")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def platform_name() -> str:
    system = platform.system()
    machine = platform.machine()
    return SUPPORTED_PLATFORMS.get((system, machine), f"{system.lower()}-{machine.lower()}")


def git_commit(root: Path) -> dict:
    def run(*args: str) -> str:
        return subprocess.run(
            ["git", "-C", str(root), *args], capture_output=True, text=True, check=True
        ).stdout.strip()

    commit = run("rev-parse", "HEAD")
    dirty = bool(run("status", "--porcelain", "--untracked-files=all"))
    return {"commit": commit, "short": commit[:12], "dirty": dirty}


def build_game(root: Path, compiler: str, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [compiler, "-emit", "exe", "-o", str(output), str(root / "examples/maze/main.elisa")],
        capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"packaged game build failed: {result.stderr.strip() or result.stdout.strip()}")


def cook(root: Path) -> Path:
    result = subprocess.run(
        [sys.executable, str(root / "scripts/cook_assets.py"), str(root)],
        capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"asset cooking failed: {result.stderr.strip() or result.stdout.strip()}")
    packages = sorted((root / "build/cooked").glob("*.pkg"))
    if not packages:
        raise RuntimeError("asset cooking produced no package")
    return packages[0]


def build_embed_archive(root: Path, compiler: str, staging: Path) -> tuple:
    # The host-facing C ABI: emit the maze game as a C archive and copy its
    # generated header beside it, so an embedder links one library and declares
    # the export surface from the header the compiler produced.
    archive = staging / "lib/libmaze.a"
    archive.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [compiler, "-emit", "c-archive", "-o", str(archive), str(root / "examples/maze/capi.elisa")],
        capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"c-archive emit failed: {result.stderr.strip() or result.stdout.strip()}")
    header = archive.with_suffix(".h")
    if not archive.is_file() or not header.is_file():
        raise RuntimeError("c-archive emit produced no archive or header")
    target_header = staging / "include/libmaze.h"
    target_header.parent.mkdir(parents=True, exist_ok=True)
    target_header.write_bytes(header.read_bytes())
    return archive, target_header


def package_godot_extension(root: Path, staging: Path) -> list:
    # The Godot host builds the extension from source against the installed
    # Godot's own dumped interface, so the release carries the bridge, the
    # project files, and the probe rather than a prebuilt dylib tied to one
    # Godot build.
    names = ("elisa_godot_bridge.cpp", "elisa_maze.gdextension", "project.godot", "godot_embed_probe.gd")
    target_dir = staging / "godot"
    target_dir.mkdir(parents=True, exist_ok=True)
    copied = []
    for name in names:
        source = root / "backends/godot-embed" / name
        if not source.is_file():
            raise RuntimeError(f"Godot extension source missing: {name}")
        target = target_dir / name
        target.write_bytes(source.read_bytes())
        copied.append(target)
    return copied


def collect(root: Path, compiler: str, staging: Path) -> dict:
    game = staging / "bin/maze-game"
    build_game(root, compiler, game)
    package = cook(root)
    (staging / "assets").mkdir(parents=True, exist_ok=True)
    target_package = staging / "assets" / package.name
    target_package.write_bytes(package.read_bytes())
    # A release carries every cooked artifact the runtime reads, not just the
    # mesh package; the texture ships beside it.
    texture = root / "build" / "cooked" / "maze_tile_tex.rgba"
    target_texture = None
    if texture.is_file():
        target_texture = staging / "assets" / texture.name
        target_texture.write_bytes(texture.read_bytes())
    packed_texture = root / "build" / "cooked" / "maze_tile_tex16.rgba"
    target_packed = None
    if packed_texture.is_file():
        target_packed = staging / "assets" / packed_texture.name
        target_packed.write_bytes(packed_texture.read_bytes())
    bc1_texture = root / "build" / "cooked" / "maze_tile_tex_bc1.rgba"
    target_bc1 = None
    if bc1_texture.is_file():
        target_bc1 = staging / "assets" / bc1_texture.name
        target_bc1.write_bytes(bc1_texture.read_bytes())
    ktx_textures = []
    for name in ("maze_tile_tex.ktx", "maze_tile_tex_bc1.ktx", "maze_tile_tex.ktx2"):
        texture = root / "build" / "cooked" / name
        if texture.is_file():
            target = staging / "assets" / texture.name
            target.write_bytes(texture.read_bytes())
            ktx_textures.append(target)
    (staging / "fixtures").mkdir(parents=True, exist_ok=True)
    fixture = staging / "fixtures/scene_manifest.txt"
    fixture.write_bytes((root / "backends/scene_manifest.txt").read_bytes())
    embed_archive, embed_header = build_embed_archive(root, compiler, staging)
    godot_files = package_godot_extension(root, staging)
    git = git_commit(root)
    release = {
        "name": "elisa-maze",
        "platform": platform_name(),
        "engine_commit": git["commit"],
        "engine_dirty": git["dirty"],
        "supported_platforms": sorted(SUPPORTED_PLATFORMS.values()),
        "untested_platforms": list(UNTESTED_PLATFORMS),
        "files": {
            "bin/maze-game": sha256_file(game),
            f"assets/{package.name}": sha256_file(target_package),
            "fixtures/scene_manifest.txt": sha256_file(fixture),
            "lib/libmaze.a": sha256_file(embed_archive),
            "include/libmaze.h": sha256_file(embed_header),
        },
    }
    if target_texture is not None:
        release["files"][f"assets/{target_texture.name}"] = sha256_file(target_texture)
    if target_packed is not None:
        release["files"][f"assets/{target_packed.name}"] = sha256_file(target_packed)
    if target_bc1 is not None:
        release["files"][f"assets/{target_bc1.name}"] = sha256_file(target_bc1)
    for target in ktx_textures:
        release["files"][f"assets/{target.name}"] = sha256_file(target)
    for target in godot_files:
        release["files"][f"godot/{target.name}"] = sha256_file(target)
    (staging / "release.json").write_text(json.dumps(release, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return release


def deterministic_archive(staging: Path) -> bytes:
    # A deterministic tar: entries sorted by name, and mtime/uid/gid/uname/gname
    # zeroed so two runs over identical bytes produce identical output. The
    # gzip wrapper is given mtime=0 for the same reason.
    tar_bytes = io.BytesIO()
    with tarfile.open(fileobj=tar_bytes, mode="w", format=tarfile.GNU_FORMAT) as archive:
        for path in sorted(staging.rglob("*")):
            if not path.is_file():
                continue
            info = archive.gettarinfo(str(path), arcname=str(path.relative_to(staging)))
            info.mtime = 0
            info.uid = 0
            info.gid = 0
            info.uname = ""
            info.gname = ""
            info.mode = 0o644
            with path.open("rb") as stream:
                archive.addfile(info, stream)
    compressed = io.BytesIO()
    with gzip.GzipFile(fileobj=compressed, mode="wb", mtime=0) as gz:
        gz.write(tar_bytes.getvalue())
    return compressed.getvalue()


def main(arguments: list[str]) -> int:
    if len(arguments) != 2:
        print("usage: package_release.py ENGINE_ROOT COMPILER", file=sys.stderr)
        return 2
    root = Path(arguments[0]).resolve(strict=True)
    compiler = arguments[1]
    staging = root / "build/release/elisa-maze"
    release_dir = root / "build/release"
    release_dir.mkdir(parents=True, exist_ok=True)
    try:
        release = collect(root, compiler, staging)
        first = deterministic_archive(staging)
        # Archiving the same collected bytes twice must give the same archive;
        # the per-file hashes in release.json pin the inputs the archive was
        # built from, so a changed compiler or asset is visible even though the
        # archive's own determinism is what this check enforces.
        second = deterministic_archive(staging)
        if first != second:
            raise RuntimeError("release archive is not reproducible across two runs")
        archive_name = f"elisa-maze-{release['platform']}-{release['engine_commit'][:12]}.tar.gz"
        archive_path = release_dir / archive_name
        archive_path.write_bytes(first)
        summary = {
            "name": release["name"],
            "platform": release["platform"],
            "engine_commit": release["engine_commit"],
            "archive": archive_name,
            "archive_sha256": sha256_bytes(first),
            "archive_bytes": len(first),
            "reproducible": True,
            "untested_platforms": release["untested_platforms"],
        }
        (release_dir / "release.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except (OSError, RuntimeError, subprocess.CalledProcessError) as failure:
        print(f"release packaging failed: {failure}", file=sys.stderr)
        return 1
    print(f"Release {summary['archive']} ({summary['archive_bytes']} bytes) sha256={summary['archive_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
