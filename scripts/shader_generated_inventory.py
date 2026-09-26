"""Track compiler-owned Metal outputs without claiming custom shader files."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
import re

from package_macos_app import PackageError, SHADER_GENERATED_INVENTORY_NAME

INVENTORY_NAME = SHADER_GENERATED_INVENTORY_NAME
MAX_INVENTORY_BYTES = 1024 * 1024
MAX_GENERATED_FILES = 8192


def digest(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def reconcile(stage: Path, generated: dict[str, str]) -> None:
    inventory = stage / INVENTORY_NAME
    previous: dict[str, str] = {}
    if inventory.exists():
        if inventory.stat().st_size > MAX_INVENTORY_BYTES:
            raise PackageError("generated shader inventory exceeds its size limit")
        try:
            document = json.loads(inventory.read_text(encoding="utf-8"))
        except (UnicodeError, json.JSONDecodeError) as error:
            raise PackageError("invalid generated shader inventory") from error
        if (not isinstance(document, dict) or type(document.get("schema")) is not int
                or document.get("schema") != 1
                or not isinstance(document.get("files"), dict)):
            raise PackageError("unsupported generated shader inventory")
        previous = document["files"]
    for entries in (previous, generated):
        if len(entries) > MAX_GENERATED_FILES:
            raise PackageError("too many generated shader files")
        for name, checksum in entries.items():
            path = PurePosixPath(name)
            if (not name.startswith("metal/") or str(path) != name
                    or ".." in path.parts or "\\" in name or "\0" in name
                    or path.suffix != ".cso" or not isinstance(checksum, str)
                    or re.fullmatch(r"[0-9a-f]{64}", checksum) is None):
                raise PackageError("invalid generated shader inventory entry")
    # Validate all previous outputs before changing the staged tree. Edited
    # generated files require an explicit user decision instead of deletion.
    for name, checksum in previous.items():
        path = stage / name
        if path.exists() and (not path.is_file() or digest(path) != checksum):
            raise PackageError(f"generated shader was modified locally: {name}")
    for name in previous.keys() - generated.keys():
        (stage / name).unlink(missing_ok=True)
    encoded = json.dumps({"schema": 1, "files": generated},
        indent=2, sort_keys=True) + "\n"
    if len(encoded.encode("utf-8")) > MAX_INVENTORY_BYTES:
        raise PackageError("generated shader inventory exceeds its size limit")
    inventory.write_text(encoded, encoding="utf-8")
