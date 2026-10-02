"""Shared fixtures for the elisa_build_run CLI tests."""

from __future__ import annotations

import unittest
from pathlib import Path
from unittest import mock
from contextlib import contextmanager


SCRIPT = Path(__file__).resolve().with_name("elisa_build_run.py")


def touch(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.touch()


def fake_cook_command(command: list[str], *, cwd: Path) -> int:
    """Materialize staged cooker outputs for runner contract checks."""
    for option in ("--output", "--texture-output"):
        if option not in command:
            continue
        output = Path(command[command.index(option) + 1])
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(("cooked:" + output.suffix).encode("ascii"))
    return 0


@contextmanager
def mocked_asset_cooker(runner):
    with mock.patch.object(runner, "run_command", side_effect=fake_cook_command) as run, \
            mock.patch.object(runner.asset_cooks, "_external_tool_identity",
                return_value={"toolchain": "fake"}):
        yield run


def assert_staged_output(test: unittest.TestCase, command: list[str], option: str,
    final_output: Path) -> None:
    staged = Path(command[command.index(option) + 1])
    test.assertEqual(staged.parent, final_output.parent.resolve())
    test.assertEqual(staged.suffix, final_output.suffix)
    test.assertIn(".elisa-cook-", staged.name)


def fake_native_paths(root: Path) -> tuple[Path, Path, Path, Path]:
    wicked_root = root / "Wicked checkout with spaces"
    wicked_source = wicked_root / "WickedEngine"
    wicked_build = root / "Wicked build with spaces"
    libraries = wicked_build / "WickedEngine"
    for relative in (
        "wiApplication.h", "wiAppleHelper.mm", "wiInput_Apple.mm",
        "libdxcompiler.dylib",
    ):
        touch(wicked_source / relative)
    (wicked_source / "shaders").mkdir(parents=True)
    for relative in (
        "libWickedEngine.a", "libJolt.a", "Utility/libUtility.a",
        "Utility/FAudio/libFAudio.a", "LUA/libLUA.a",
    ):
        touch(libraries / relative)

    sdl_root = root / "SDL3 SDK with spaces"
    touch(sdl_root / "include/SDL3/SDL.h")
    touch(sdl_root / "lib/libSDL3.dylib")
    brew_root = root / "Package prefix with spaces"
    touch(brew_root / "include/freetype2/ft2build.h")
    touch(brew_root / "include/harfbuzz/hb.h")
    for name in ("freetype", "harfbuzz", "zstd"):
        touch(brew_root / f"lib/lib{name}.dylib")
    return wicked_root, wicked_build, sdl_root, brew_root


def write_fake_tools(root: Path) -> tuple[Path, Path, Path]:
    log_dir = root / "fake tool logs"
    log_dir.mkdir()
    compiler = root / "fake Elisa compiler"
    compiler.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, pathlib, sys\n"
        "pathlib.Path(os.environ['FAKE_LOG_DIR'], 'compiler.json').write_text(json.dumps(sys.argv[1:]))\n"
        "if os.environ.get('FAKE_COMPILER_STATUS'): raise SystemExit(int(os.environ['FAKE_COMPILER_STATUS']))\n"
        "output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "output.write_bytes(b'fake archive')\n"
        "wrapper = pathlib.Path(sys.argv[-1])\n"
        "pathlib.Path(os.environ['FAKE_LOG_DIR'], 'wrapper.txt').write_text(wrapper.read_text())\n"
        "exports = ['fake_export'] if os.environ.get('FAKE_EXPORT') else []\n"
        "manifest = {'exported_functions': exports, 'exported_globals': [], 'exported_types': []}\n"
        "output.with_suffix('.elisa-abi.json').write_text(json.dumps(manifest))\n",
        encoding="utf-8",
    )
    linker = root / "fake native linker"
    linker.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, pathlib, sys\n"
        "pathlib.Path(os.environ['FAKE_LOG_DIR'], 'linker.json').write_text(json.dumps(sys.argv[1:]))\n"
        "output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "program = '#!/usr/bin/env python3\\nimport json, os\\nfrom pathlib import Path\\n'\n"
        "program += 'settings = {key: os.environ.get(key) for key in (\"ELISA_PROJECT_TITLE\", \"ELISA_PROJECT_WIDTH\", \"ELISA_PROJECT_HEIGHT\", \"ELISA_PROJECT_HIDDEN\", \"ELISA_PROJECT_ROOT\")}\\n'\n"
        "program += 'Path(os.environ[\"FAKE_LOG_DIR\"], \"ran.json\").write_text(json.dumps({\"cwd\": os.getcwd(), \"settings\": settings}))\\n'\n"
        "output.write_text(program)\n"
        "output.chmod(0o755)\n",
        encoding="utf-8",
    )
    compiler.chmod(0o755)
    linker.chmod(0o755)
    return compiler, linker, log_dir
