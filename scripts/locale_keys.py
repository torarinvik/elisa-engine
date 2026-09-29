#!/usr/bin/env python3
"""Message-key extraction and locale-file checking for UiLocaleData.

Keys are declared in Elisa source as `const MSG_NAME: i64 = N` (0 <= N < 32,
matching UiLocale::KEYS). Locale files use the UiLocaleData line syntax:
`<loc> <key>[:<category>] <text>`, `#` comments and blank lines.

  locale_keys.py extract SRC...                 list declared and used keys
  locale_keys.py check --loc FILE SRC...        errors exit 1, gaps are warnings
  locale_keys.py template --loc FILE --locale nb SRC...
                                                missing lines, English as comment
  locale_keys.py --self-test
"""
from __future__ import annotations

import argparse
import re
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

LOCALES = ("en", "nb", "ru", "ar", "ja")
CATEGORIES = ("zero", "one", "two", "few", "many", "other")
KEYS = 32
MAX_TEXT = 96
DECLARE = re.compile(r"\bconst\s+(MSG_[A-Z0-9_]+)\s*:\s*i64\s*=\s*(\d+)\b")
USE = re.compile(r"\bMSG_[A-Z0-9_]+\b")
PLACEHOLDER = re.compile(r"\{(\d)\}")


@dataclass
class Keys:
    ids: dict[str, int] = field(default_factory=dict)
    used: set[str] = field(default_factory=set)
    errors: list[str] = field(default_factory=list)


def extract(paths: list[Path]) -> Keys:
    keys = Keys()
    owner: dict[int, str] = {}
    for path in paths:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            code = line.split("#", 1)[0]
            declared = DECLARE.search(code)
            if declared:
                name, value = declared.group(1), int(declared.group(2))
                where = f"{path}:{number}"
                if value >= KEYS:
                    keys.errors.append(f"{where}: {name} = {value} is outside 0..{KEYS - 1}")
                elif name in keys.ids and keys.ids[name] != value:
                    keys.errors.append(f"{where}: {name} redeclared as {value}")
                elif value in owner and owner[value] != name:
                    keys.errors.append(f"{where}: {name} reuses id {value} of {owner[value]}")
                else:
                    keys.ids[name] = value
                    owner[value] = name
                continue
            keys.used.update(USE.findall(code))
    for name in sorted(keys.used - set(keys.ids)):
        keys.errors.append(f"{name} is used but never declared")
    return keys


@dataclass
class Entries:
    text: dict[tuple[str, int, str], str] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)


def parse_loc(path: Path) -> Entries:
    entries = Entries()
    try:
        source = path.read_bytes().decode("utf-8")
    except UnicodeDecodeError as exc:
        entries.errors.append(f"{path}: not UTF-8 ({exc.reason} at byte {exc.start})")
        return entries
    for number, raw in enumerate(source.split("\n"), 1):
        line = raw.rstrip("\r")
        where = f"{path}:{number}"
        if not line.strip() or line.startswith("#"):
            continue
        match = re.fullmatch(r"([a-z]{2}) (\d{1,2})(?::([a-z]+))? (.+)", line)
        if not match:
            entries.errors.append(f"{where}: expected '<loc> <key>[:<category>] <text>'")
            continue
        locale, key, category, text = match.group(1), int(match.group(2)), match.group(3) or "other", match.group(4)
        if locale not in LOCALES:
            entries.errors.append(f"{where}: unknown locale {locale}")
        elif key >= KEYS:
            entries.errors.append(f"{where}: key {key} is outside 0..{KEYS - 1}")
        elif category not in CATEGORIES:
            entries.errors.append(f"{where}: unknown plural category {category}")
        elif len(text) > MAX_TEXT:
            entries.errors.append(f"{where}: text has {len(text)} codepoints, limit {MAX_TEXT}")
        elif (locale, key, category) in entries.text:
            entries.errors.append(f"{where}: duplicate {locale} {key}:{category}")
        else:
            entries.text[(locale, key, category)] = text
    return entries


def placeholders(text: str) -> set[str]:
    return set(PLACEHOLDER.findall(text))


def check(keys: Keys, entries: Entries) -> tuple[list[str], list[str]]:
    errors = list(keys.errors) + list(entries.errors)
    warnings: list[str] = []
    names = {value: name for name, value in keys.ids.items()}
    english = {key for (locale, key, _) in entries.text if locale == "en"}
    for name, value in sorted(keys.ids.items(), key=lambda item: item[1]):
        if value not in english:
            errors.append(f"{name} ({value}) has no English text; missing-key fallback would show nothing")
    for (locale, key, category), text in sorted(entries.text.items()):
        if key not in names:
            errors.append(f"{locale} {key}:{category} has no declared MSG_ constant")
            continue
        base = entries.text.get(("en", key, "other"))
        if base is not None and placeholders(text) != placeholders(base):
            errors.append(f"{locale} {names[key]}:{category} placeholders {sorted(placeholders(text))} differ from English {sorted(placeholders(base))}")
    for locale in LOCALES[1:]:
        missing = [names[key] for key in sorted(names) if (locale, key, "other") not in entries.text]
        if missing:
            warnings.append(f"{locale} falls back to English for {len(missing)} key(s): {', '.join(missing)}")
    for name in sorted(set(keys.ids) - keys.used):
        warnings.append(f"{name} is declared but never used")
    return errors, warnings


def template(keys: Keys, entries: Entries, locale: str) -> list[str]:
    lines: list[str] = []
    for name, value in sorted(keys.ids.items(), key=lambda item: item[1]):
        if (locale, value, "other") in entries.text:
            continue
        base = entries.text.get(("en", value, "other"), "")
        lines.append(f"# {name}: {base}")
        lines.append(f"{locale} {value} ")
    return lines


def run(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="?", choices=("extract", "check", "template"))
    parser.add_argument("sources", nargs="*", type=Path)
    parser.add_argument("--loc", type=Path)
    parser.add_argument("--locale", choices=LOCALES[1:])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_intermixed_args(argv)
    if args.self_test:
        self_test()
        print("locale_keys self-test passed.")
        return 0
    if not args.command or not args.sources:
        parser.error("a command and at least one source are required")
    keys = extract(args.sources)
    if args.command == "extract":
        for name, value in sorted(keys.ids.items(), key=lambda item: item[1]):
            print(f"{value}\t{name}\t{'used' if name in keys.used else 'unused'}")
        for message in keys.errors:
            print(f"error: {message}", file=sys.stderr)
        return 1 if keys.errors else 0
    if args.loc is None:
        parser.error("--loc is required")
    entries = parse_loc(args.loc)
    if args.command == "template":
        if args.locale is None:
            parser.error("--locale is required")
        print("\n".join(template(keys, entries, args.locale)))
        return 0
    errors, warnings = check(keys, entries)
    for message in warnings:
        print(f"warning: {message}", file=sys.stderr)
    for message in errors:
        print(f"error: {message}", file=sys.stderr)
    return 1 if errors else 0


def self_test() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        src = root / "strings.elisa"
        src.write_text(
            "const MSG_START: i64 = 0\nconst MSG_COINS: i64 = 1  # count\nconst MSG_OLD: i64 = 2\n"
            "def draw() -> i64:\n    label(MSG_START) + label(MSG_COINS)  # MSG_GHOST in a comment\n",
            encoding="utf-8")
        loc = root / "course.loc"
        loc.write_text(
            "# strings\nen 0 Start\nen 1:one {0} coin\nen 1 {0} coins\nen 2 Old\r\n"
            "nb 0 Begynn nå\nnb 1 {0} mynter\nru 1:few {0} монеты\n", encoding="utf-8")
        keys = extract([src])
        assert keys.ids == {"MSG_START": 0, "MSG_COINS": 1, "MSG_OLD": 2}, keys.ids
        assert keys.errors == [], keys.errors
        errors, warnings = check(keys, parse_loc(loc))
        assert errors == [], errors
        assert any(w.startswith("nb falls back to English for 1 key(s): MSG_OLD") for w in warnings), warnings
        assert "MSG_OLD is declared but never used" in warnings, warnings
        assert template(keys, parse_loc(loc), "nb") == ["# MSG_OLD: Old", "nb 2 "]

        # Errors: undeclared use, id clash, out-of-range id, placeholder drift,
        # orphan text, bad category, duplicate, missing English, bad UTF-8.
        bad = root / "bad.elisa"
        bad.write_text("const MSG_A: i64 = 3\nconst MSG_B: i64 = 3\nconst MSG_C: i64 = 40\nx(MSG_Z)\n", encoding="utf-8")
        bad_keys = extract([bad])
        joined = "\n".join(bad_keys.errors)
        assert "MSG_B reuses id 3 of MSG_A" in joined and "MSG_C = 40" in joined and "MSG_Z is used but never declared" in joined, joined
        drift = root / "drift.loc"
        drift.write_text("en 0 Start\nen 1 {0} coins\nnb 1 mynter\nen 9 Orphan\nen 0:lots x\nen 0 Again\n", encoding="utf-8")
        errors, _ = check(keys, parse_loc(drift))
        joined = "\n".join(errors)
        for needle in ("placeholders [] differ", "en 9:other has no declared", "unknown plural category lots",
                       "duplicate en 0:other", "MSG_OLD (2) has no English text"):
            assert needle in joined, (needle, joined)
        broken = root / "broken.loc"
        broken.write_bytes(b"en 0 \xff\n")
        assert "not UTF-8" in parse_loc(broken).errors[0]
        assert run(["check", "--loc", str(drift), str(src)]) == 1
        assert run(["check", "--loc", str(loc), str(src)]) == 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
