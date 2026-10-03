#!/usr/bin/env python3
"""Regression checks for staging compiler metadata out of native runtime links."""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import elisa_build_run as runner


class RuntimeLinkMetadataTests(unittest.TestCase):
    def test_nm_parser_keeps_definitions_and_ignores_undefined_symbols(self) -> None:
        output = (
            "libapp.a(module.o): 0000000000000000 D ___lsp_decl_name.4\n"
            "libapp.a(module.o):                  U _external_function\n"
            "0000000000000010 T _game_entry\n"
        )
        self.assertEqual(runner.parse_defined_global_symbols(output), {
            "___lsp_decl_name.4", "_game_entry",
        })

    def test_archive_compile_does_not_bundle_runtime_twice(self) -> None:
        original = os.environ.get("ELISA_RUNTIME_OBJ")
        with mock.patch.dict(os.environ, {"ELISA_RUNTIME_OBJ": "/runtime.o"}), \
                mock.patch.object(runner, "run_command", return_value=0) as run:
            runner.compile_archive("compiler", Path("entry.elisa"), Path("entry.a"))
        self.assertEqual(run.call_args.kwargs["env"]["ELISA_RUNTIME_OBJ"], "none")
        self.assertEqual(os.environ.get("ELISA_RUNTIME_OBJ"), original)

    def test_runtime_staging_removes_only_duplicate_metadata(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa runtime labels ") as temporary_directory:
            root = Path(temporary_directory)
            runtime, archive = root / "runtime.o", root / "application.a"
            runtime.write_bytes(b"runtime object")
            archive.write_bytes(b"application archive")
            fake_nm, fake_nmedit = root / "nm", root / "nmedit"
            fake_nm.touch()
            fake_nmedit.touch()
            runtime_symbols = (
                "0000 D ___lsp_decl_name.4\n0001 D ___lsp_decl_name.8\n0002 T _helper\n"
            )
            archive_symbols = (
                "application.a(module.o): 0000 D ___lsp_decl_name.4\n"
                "application.a(module.o): 0001 D ___lsp_decl_name.12\n"
            )
            calls = [
                subprocess.CompletedProcess([], 0, runtime_symbols, ""),
                subprocess.CompletedProcess([], 0, archive_symbols, ""),
                subprocess.CompletedProcess([], 0, "", ""),
                subprocess.CompletedProcess([], 0, "0002 T _helper\n", ""),
            ]
            with mock.patch.object(runner.shutil, "which",
                    side_effect=lambda name: str(fake_nm if name == "nm" else fake_nmedit)), \
                    mock.patch.object(runner.subprocess, "run", side_effect=calls) as run:
                staged = runner.runtime_object_for_link(runtime, archive, root)

            self.assertNotEqual(staged, runtime)
            self.assertEqual((root / "elisacore_runtime_duplicate_symbols.txt").read_text(),
                "___lsp_decl_name.4\n")
            self.assertEqual(run.call_count, 4)


if __name__ == "__main__":
    unittest.main()
