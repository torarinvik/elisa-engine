"""Crash-safe journaled persistence for the Elisa save document.

The Elisa save schema owns record meaning and checksums. This small file layer
owns only durable replacement: a canonical payload is written and fsynced to a
temporary file, a journal records its hash, and recovery completes or discards
the interrupted replacement before a caller loads the document.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile


def canonical_bytes(document: dict) -> bytes:
    return (json.dumps(document, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")


def _fsync_directory(directory: Path) -> None:
    descriptor = os.open(directory, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _write_synced(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())


def _paths(target: Path) -> tuple[Path, Path]:
    return target.with_name(target.name + ".tmp"), target.with_name(target.name + ".journal")


def save(target: Path, document: dict) -> str:
    payload = canonical_bytes(document)
    temporary, journal = _paths(target)
    digest = hashlib.sha256(payload).hexdigest()
    _write_synced(temporary, payload)
    journal_payload = canonical_bytes({"target": target.name, "temporary": temporary.name, "sha256": digest})
    _write_synced(journal, journal_payload)
    os.replace(temporary, target)
    _fsync_directory(target.parent)
    journal.unlink(missing_ok=True)
    _fsync_directory(target.parent)
    return digest


def recover(target: Path) -> bool:
    temporary, journal = _paths(target)
    if not journal.exists():
        return False
    try:
        metadata = json.loads(journal.read_text(encoding="utf-8"))
        if metadata.get("target") != target.name or metadata.get("temporary") != temporary.name:
            raise ValueError("journal target mismatch")
        payload = temporary.read_bytes()
        if hashlib.sha256(payload).hexdigest() != metadata.get("sha256"):
            raise ValueError("journal payload checksum mismatch")
        json.loads(payload.decode("utf-8"))
        os.replace(temporary, target)
        _fsync_directory(target.parent)
        journal.unlink(missing_ok=True)
        _fsync_directory(target.parent)
        return True
    except (OSError, ValueError, json.JSONDecodeError):
        temporary.unlink(missing_ok=True)
        journal.unlink(missing_ok=True)
        _fsync_directory(target.parent)
        return False


def load(target: Path) -> dict:
    recover(target)
    return json.loads(target.read_text(encoding="utf-8"))


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="elisa-save-") as directory:
        target = Path(directory) / "world.save"
        first = {"version": 2, "records": [{"id": 7, "fields": [1, 2]}]}
        second = {"version": 2, "records": [{"id": 7, "fields": [9, 4]}]}
        digest = save(target, first)
        assert digest == hashlib.sha256(canonical_bytes(first)).hexdigest()
        assert load(target) == first
        temporary, journal = _paths(target)
        payload = canonical_bytes(second)
        _write_synced(temporary, payload)
        _write_synced(journal, canonical_bytes({"target": target.name, "temporary": temporary.name, "sha256": hashlib.sha256(payload).hexdigest()}))
        assert recover(target) and load(target) == second
        _write_synced(temporary, b"corrupt")
        _write_synced(journal, canonical_bytes({"target": target.name, "temporary": temporary.name, "sha256": "bad"}))
        assert not recover(target) and load(target) == second and not journal.exists()
        print("save journal: commit, recovery, and corrupt-journal fallback passed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    parser.error("--self-test is required")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
