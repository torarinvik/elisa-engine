#!/usr/bin/env python3
"""Check that the Elisa and C++ portable input-code tables agree, then
compile and run the SDL mapping regression (test/application_gamepad_codes.cpp)."""

from __future__ import annotations

import os
import re
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SDL_INCLUDE_CANDIDATES = ("/opt/homebrew/include", "/usr/local/include", "/usr/include")


def elisa_codes(text: str) -> dict[str, int]:
    return {name: int(value) for name, value in
        re.findall(r"^\s+((?:KEY|GAMEPAD)_[A-Z0-9_]+): i64 = (\d+)$", text, re.M)}


def native_codes(text: str) -> dict[str, int]:
    return {name: int(value) for name, value in
        re.findall(r"^\s+((?:KEY|GAMEPAD)_[A-Z0-9_]+) = (\d+),$", text, re.M)}


def elisa_key_enum_mapped(text: str) -> tuple[list[str], list[str]]:
    body = re.search(r"enum KeyboardKeyCode:\n((?:\s{12}\w+\n)+)", text)
    members = body.group(1).split() if body else []
    mapped = re.findall(r"KeyboardKeyCode\.(\w+): PortableInputCodes::KEY_", text)
    return members, mapped


def table_problems(elisa_text: str, native_text: str) -> list[str]:
    problems: list[str] = []
    elisa = elisa_codes(elisa_text)
    native = native_codes(native_text)
    if not elisa or not native:
        problems.append("no input codes found")
    for name in sorted(set(elisa) | set(native)):
        if elisa.get(name) != native.get(name):
            problems.append(f"{name}: Elisa {elisa.get(name)} vs native {native.get(name)}")
    values = list(elisa.values())
    if len(values) != len(set(values)):
        problems.append("duplicate Elisa code values")
    members, mapped = elisa_key_enum_mapped(elisa_text)
    if sorted(members) != sorted(mapped) or len(mapped) != len(set(mapped)):
        problems.append("KeyboardKeyCode members and keyboard_key_code arms differ")
    switch = native_text.split("constexpr int32_t keyboard_key_code", 1)[-1].split("default:", 1)[0]
    returned = re.findall(r"return (KEY_[A-Z0-9_]+);", switch)
    keys = {name for name in native if name.startswith("KEY_")}
    if sorted(returned) != sorted(keys):
        problems.append("native keyboard switch does not return every KEY_ exactly once")
    return problems


def self_test() -> list[str]:
    elisa = (ROOT / "src/runtime/application_input.elisa").read_text(encoding="utf-8")
    native = (ROOT / "native/application_input_codes.h").read_text(encoding="utf-8")
    if not table_problems(elisa.replace("KEY_SLASH: i64 = ", "KEY_SLASH: i64 = 9"), native):
        return ["a changed Elisa code was not reported"]
    if not table_problems(elisa, native.replace("    case SDLK_SLASH: return KEY_SLASH;\n", "")):
        return ["a missing native switch arm was not reported"]
    return []


def main() -> int:
    elisa = (ROOT / "src/runtime/application_input.elisa").read_text(encoding="utf-8")
    native = (ROOT / "native/application_input_codes.h").read_text(encoding="utf-8")
    problems = table_problems(elisa, native) + self_test()
    if problems:
        for problem in problems:
            print(f"Input code tables: {problem}")
        return 1
    include = next((path for path in SDL_INCLUDE_CANDIDATES if Path(path, "SDL3/SDL_keycode.h").exists()), None)
    if include is None:
        print("SDL3 headers not found; input code tables checked, native mapping test skipped.")
        return 0
    compiler = os.environ.get("CXX", "clang++")
    with tempfile.TemporaryDirectory(prefix="elisa-input-codes-") as temporary:
        executable = Path(temporary) / "input-codes"
        built = subprocess.run([compiler, "-std=c++17", "-I", include, "-I", str(ROOT / "native"),
            str(ROOT / "test/application_gamepad_codes.cpp"), "-o", str(executable)], check=False)
        if built.returncode != 0:
            print("Input code mapping test did not compile.")
            return built.returncode
        tested = subprocess.run([str(executable)], check=False)
        if tested.returncode != 0:
            print(f"Input code mapping test failed with status {tested.returncode}.")
            return tested.returncode
    print("Input code tables and SDL mapping tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
