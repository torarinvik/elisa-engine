"""Read a project's explicit Metal shader permutation selection."""
import json
from pathlib import Path, PurePosixPath

from package_macos_app import PackageError

MAX_SELECTION_BYTES = 1024 * 1024
MAX_PERMUTATIONS = 8192


def read_selection(path: Path | None) -> set[str] | None:
    if path is None:
        return None
    if path.stat().st_size > MAX_SELECTION_BYTES:
        raise PackageError("shader permutation selection exceeds its size limit")
    try:
        entries = json.loads(path.read_text(encoding="utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise PackageError("shader permutation selection must be a JSON array") from error
    if not isinstance(entries, list) or not entries or len(entries) > MAX_PERMUTATIONS:
        raise PackageError("shader permutation selection must be a nonempty bounded array")
    selected = set()
    for entry in entries:
        if not isinstance(entry, str):
            raise PackageError("shader permutation names must be strings")
        relative = PurePosixPath(entry)
        if (relative.is_absolute() or ".." in relative.parts or str(relative) != entry
                or "\\" in entry or "\0" in entry or relative.suffix != ".cso"
                or entry in selected):
            raise PackageError(f"invalid or duplicate shader permutation: {entry!r}")
        selected.add(entry)
    return selected


def select_binaries(root: Path, binaries: list[Path], selected: set[str] | None) -> list[Path]:
    if selected is None:
        return binaries
    available = {path.relative_to(root).as_posix(): path for path in binaries}
    missing = selected - available.keys()
    if missing:
        raise PackageError("requested shader permutations were not compiled: " + ", ".join(sorted(missing)))
    return [available[name] for name in sorted(selected)]
