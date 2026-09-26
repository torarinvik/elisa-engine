"""Publish a complete shader library with rollback on replacement failure."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import tempfile

from package_macos_app import PackageError, SHADER_MANIFEST_NAME, shader_manifest


def reject_symlinks(root: Path) -> None:
    if root.is_symlink() or any(path.is_symlink() for path in root.rglob("*")):
        raise PackageError(f"shader library must not contain symbolic links: {root}")


def publish(shader_root: Path, compiled_root: Path, binaries: list[Path]) -> str:
    """Keep the previous tree and manifest intact until staging has succeeded.

    Publication uses two same-filesystem renames. Readers must not launch during
    the brief replacement window; if the second rename fails, restore the old
    tree. The backup is retained if rollback itself fails.
    """
    reject_symlinks(shader_root)
    reject_symlinks(compiled_root)
    with tempfile.TemporaryDirectory(prefix=".elisa-shader-stage-", dir=shader_root.parent) as folder:
        stage = Path(folder) / "shaders"
        if shader_root.exists():
            shutil.copytree(shader_root, stage)
        else:
            stage.mkdir()
        for source in binaries:
            destination = stage / "metal" / source.relative_to(compiled_root)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
        manifest = shader_manifest(stage)
        (stage / SHADER_MANIFEST_NAME).write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        # This sibling directory is deliberately outside TemporaryDirectory's
        # cleanup: a failed rollback must never erase the only surviving copy.
        backup = Path(tempfile.mkdtemp(prefix=".elisa-shader-backup-", dir=shader_root.parent))
        backup.rmdir()
        had_previous = shader_root.exists()
        if had_previous:
            os.replace(shader_root, backup)
        try:
            os.replace(stage, shader_root)
        except OSError:
            if had_previous:
                try:
                    os.replace(backup, shader_root)
                except OSError as rollback_error:
                    raise PackageError(
                        f"shader publication and rollback failed; previous library retained at {backup}"
                    ) from rollback_error
            raise
        if had_previous:
            shutil.rmtree(backup)
        return str(manifest["fingerprint"])
