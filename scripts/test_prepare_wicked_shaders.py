#!/usr/bin/env python3
"""Focused tests for isolated Wicked shader preparation."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import prepare_wicked_shaders as prepare


class PrepareWickedShadersTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        root = Path(self.tempdir.name)
        self.project = root / "Project with spaces"
        self.project.mkdir()
        self.wicked = root / "WickedEngine"
        source = self.wicked / "WickedEngine"
        source.mkdir(parents=True)
        (source / "libdxcompiler.dylib").write_bytes(b"dxcompiler")
        (source / "libmetalirconverter.dylib").write_bytes(b"converter")
        self.compiler = root / "offline compiler"

    def tearDown(self) -> None:
        self.tempdir.cleanup()

    def write_compiler(self, exit_code: int = 0) -> None:
        self.compiler.write_text(
            "#!/usr/bin/env python3\n"
            "from pathlib import Path\n"
            "import sys\n"
            "assert sys.argv[1:] == ['metal', 'quiet']\n"
            f"if {exit_code}: raise SystemExit({exit_code})\n"
            "root = Path('shaders/metal')\n"
            "root.mkdir(parents=True, exist_ok=True)\n"
            "(root / 'basic.cso').write_bytes(b'new shader')\n"
            "(root / 'nested').mkdir(exist_ok=True)\n"
            "(root / 'nested' / 'permutation.cso').write_bytes(b'permutation')\n"
            "(root / 'basic.wishadermeta').write_bytes(b'private source paths')\n",
            encoding="utf-8")
        self.compiler.chmod(0o755)

    @mock.patch.object(prepare.sys, "platform", "darwin")
    def test_success_stages_permutations_and_writes_manifest(self) -> None:
        self.write_compiler()
        shader_root = self.project / "shaders"
        (shader_root / "metal").mkdir(parents=True)
        (shader_root / "metal" / "custom.cso").write_bytes(b"custom")
        (shader_root / "hlsl6").mkdir()
        (shader_root / "hlsl6" / "existing.cso").write_bytes(b"hlsl")

        result = prepare.prepare(self.project, self.wicked, None, self.compiler)

        self.assertEqual(result, 0)
        self.assertEqual((shader_root / "metal" / "basic.cso").read_bytes(), b"new shader")
        self.assertEqual((shader_root / "metal" / "nested" / "permutation.cso").read_bytes(),
            b"permutation")
        self.assertEqual((shader_root / "metal" / "custom.cso").read_bytes(), b"custom")
        self.assertEqual((shader_root / "hlsl6" / "existing.cso").read_bytes(), b"hlsl")
        self.assertFalse((shader_root / "metal" / "basic.wishadermeta").exists())
        manifest = json.loads((shader_root / prepare.SHADER_MANIFEST_NAME).read_text())
        self.assertEqual(manifest["backends"], ["hlsl6", "metal"])
        self.assertEqual([item["path"] for item in manifest["files"]], [
            "hlsl6/existing.cso", "metal/basic.cso", "metal/custom.cso",
            "metal/nested/permutation.cso"])

    @mock.patch.object(prepare.sys, "platform", "darwin")
    def test_compile_failure_preserves_existing_library_and_manifest(self) -> None:
        self.write_compiler(exit_code=3)
        shader_root = self.project / "shaders"
        backend = shader_root / "metal"
        backend.mkdir(parents=True)
        original_shader = backend / "basic.cso"
        original_shader.write_bytes(b"previous shader")
        manifest = shader_root / prepare.SHADER_MANIFEST_NAME
        manifest.write_text("previous manifest\n", encoding="utf-8")

        result = prepare.prepare(self.project, self.wicked, None, self.compiler)

        self.assertEqual(result, 3)
        self.assertEqual(original_shader.read_bytes(), b"previous shader")
        self.assertEqual(manifest.read_text(encoding="utf-8"), "previous manifest\n")

    @mock.patch.object(prepare.sys, "platform", "darwin")
    def test_symlinked_shader_root_is_rejected(self) -> None:
        self.write_compiler()
        outside = Path(self.tempdir.name) / "outside"
        outside.mkdir()
        (self.project / "shaders").symlink_to(outside, target_is_directory=True)

        result = prepare.prepare(self.project, self.wicked, None, self.compiler)

        self.assertEqual(result, 2)
        self.assertEqual(list(outside.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
