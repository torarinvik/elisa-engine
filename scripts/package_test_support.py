"""Shared fixture helpers for macOS application packaging tests."""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path

import package_macos_app as packager


def touch(path: Path, content: bytes = b"x") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


class PackageAppTestSupport:
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        root = Path(self.tempdir.name)
        self.project = root / "Game with spaces"
        self.output = root / "Out dir" / "Game.app"
        touch(self.project / "build" / "game", b"#!/bin/sh\nexit 0\n")
        os.chmod(self.project / "build" / "game", 0o755)
        touch(self.project / "build" / "cooked" / "player.pkg")
        touch(self.project / "assets" / "audio" / "step.wav")
        touch(self.project / "assets" / "textures" / "trim.png")
        touch(self.project / "assets" / "source" / "rig.blend")
        touch(self.project / "assets" / ".git" / "HEAD")
        touch(self.project / "assets" / ".DS_Store")
        touch(self.project / "shaders" / "metal" / "basic.cso")
        touch(self.project / "shaders" / "metal" / "basic.wishadermeta")
        touch(self.project / "shaders" / packager.SHADER_GENERATED_INVENTORY_NAME)

    def tearDown(self) -> None:
        self.tempdir.cleanup()

    def write_manifest(self, extra: dict[str, object]) -> dict[str, object]:
        manifest: dict[str, object] = {
            "name": "Game", "main": "src/main.elisa", "output": "build/game",
            "application": {"title": "Game"},
        }
        manifest.update(extra)
        (self.project / "elisa.project.json").write_text(json.dumps(manifest), encoding="utf-8")
        return manifest

    def package(self, manifest: dict[str, object], compiled_shaders_only: bool = False) -> Path:
        return packager.package_app(self.project, self.project / "build" / "game",
            self.output, "Game", "org.elisa.game", "1.2.3", None,
            packager.manifest_resources(manifest, self.project),
            packager.manifest_window(manifest, self.project),
            notice_paths=packager.manifest_notices(manifest, self.project),
            compiled_shaders_only=compiled_shaders_only)

    def staged(self, app: Path) -> set[str]:
        resources = app / "Contents" / "Resources"
        return {str(path.relative_to(resources)) for path in resources.rglob("*")
            if path.is_file() and path.name != "package-provenance.json"}
