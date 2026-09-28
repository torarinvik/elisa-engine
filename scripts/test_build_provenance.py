#!/usr/bin/env python3
"""Tests for source, tool and binary build identity sidecars."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from build_provenance import repository_identity, write_build_provenance


def git(path: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(path), *args], check=True,
        text=True, capture_output=True).stdout.strip()


def init_repo(path: Path, file_name: str = "tracked.txt") -> None:
    path.mkdir(parents=True, exist_ok=True)
    git(path, "init", "-q")
    git(path, "config", "user.name", "Build provenance fixture")
    git(path, "config", "user.email", "fixture@example.invalid")
    (path / file_name).write_text("initial\n", encoding="utf-8")
    git(path, "add", file_name)
    git(path, "commit", "-q", "-m", "fixture")


class BuildProvenanceTests(unittest.TestCase):
    def test_records_dirty_game_engine_backend_and_assets_revisions(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            project = root / "game"
            engine = root / "engine"
            wicked = root / "wicked"
            assets = project / "assets"
            for repository in (project, engine, wicked, assets):
                init_repo(repository)
            (project / "src").mkdir()
            main = project / "src/main.elisa"
            main.write_text("module Main\n", encoding="utf-8")
            (project / "elisa.project.json").write_text("{}\n", encoding="utf-8")
            (project / "tracked.txt").write_text("modified\n", encoding="utf-8")
            (assets / "new.glb").write_bytes(b"asset source")
            binary = project / "build/game"
            binary.parent.mkdir()
            binary.write_bytes(b"application")
            runtime = root / "runtime.o"
            runtime.write_bytes(b"runtime")
            compiler = root / "elisac"
            compiler.write_bytes(b"compiler")
            linked = root / "libWickedEngine.a"
            linked.write_bytes(b"wicked archive")

            sidecar = write_build_provenance(output=binary, project=project,
                main_source=main, engine_root=engine, wicked_root=wicked,
                compiler=str(compiler), cxx=str(compiler), runtime_object=runtime,
                native_artifacts=[linked], options={"native_optimize": False})
            record = json.loads(sidecar.read_text(encoding="utf-8"))

            self.assertEqual(record["schema"], 1)
            self.assertTrue(record["repositories"]["game"]["dirty"])
            self.assertTrue(record["repositories"]["assets"]["dirty"])
            self.assertFalse(record["repositories"]["engine"]["dirty"])
            self.assertEqual(record["repositories"]["game"]["commit"],
                git(project, "rev-parse", "HEAD"))
            self.assertEqual(record["tools"]["elisa_compiler"]["sha256"],
                record["tools"]["native_compiler"]["sha256"])
            self.assertEqual(record["native_link_artifacts"][0]["sha256"],
                hashlib.sha256(b"wicked archive").hexdigest())
            self.assertEqual(record["binary"]["sha256"],
                hashlib.sha256(b"application").hexdigest())

    def test_untracked_file_content_changes_repository_identity(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            repo = Path(folder) / "repo"
            init_repo(repo)
            untracked = repo / "generated.elisa"
            untracked.write_text("first\n", encoding="utf-8")
            first = repository_identity(repo)
            untracked.write_text("second\n", encoding="utf-8")
            second = repository_identity(repo)
            self.assertTrue(first["dirty"])
            self.assertNotEqual(first["untracked_files"][0]["sha256"],
                second["untracked_files"][0]["sha256"])


if __name__ == "__main__":
    unittest.main()
