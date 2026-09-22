"""Enforce the repository's maximum engine-source file length."""

from pathlib import Path
import sys


MAX_SOURCE_LINES = 600
SOURCE_ROOTS = ("src", "test", "proof", "examples", "scripts", "native", "backends", "docs")
SOURCE_SUFFIXES = (".elisa", ".elisascript", ".cpp", ".h", ".inc", ".py", ".gd", ".md")


def source_files(root: Path):
    plan = root / "Elisa_Engine_Architecture_and_Plan.md"
    if plan.is_file():
        yield plan
    for directory in SOURCE_ROOTS:
        base = root / directory
        if base.is_dir():
            yield from sorted(
                path
                for path in base.rglob("*")
                if path.is_file() and path.suffix in SOURCE_SUFFIXES
            )


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    violations = []
    for path in source_files(root):
        line_count = sum(1 for _ in path.open(encoding="utf-8"))
        if line_count > MAX_SOURCE_LINES:
            violations.append((path.relative_to(root), line_count))
    if violations:
        for path, line_count in violations:
            print(f"{path}: {line_count} lines (maximum {MAX_SOURCE_LINES})", file=sys.stderr)
        return 1
    print(f"Source-length policy passed ({MAX_SOURCE_LINES}-line maximum).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
