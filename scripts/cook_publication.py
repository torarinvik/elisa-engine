"""Restore a cook's previous output set when ordinary publication fails."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
from typing import Mapping
import uuid


class CookPublicationError(Exception):
    pass


def publish_cooked_outputs(staged: Mapping[Path, Path]) -> None:
    """Publish staged files; roll back completed replacements on failure.

    This protects against synchronous filesystem failures, not process crashes or
    concurrent writers. Backups stay beside each final file on its filesystem.
    """
    backups: dict[Path, Path | None] = {}
    published: list[Path] = []
    retain_backups = False
    try:
        for final in staged:
            if final.exists():
                backup = final.with_name(f".{final.name}.elisa-backup-{uuid.uuid4().hex}")
                backups[final] = backup
                shutil.copy2(final, backup)
            else:
                backups[final] = None
        for final, temporary in staged.items():
            os.replace(temporary, final)
            published.append(final)
    except BaseException as error:
        failures: list[str] = []
        for final in reversed(published):
            backup = backups[final]
            try:
                if backup is None:
                    final.unlink(missing_ok=True)
                else:
                    os.replace(backup, final)
            except OSError as restore_error:
                failures.append(f"{final}: {restore_error}; backup={backup}")
        if failures:
            retain_backups = True
            raise CookPublicationError("Cook output rollback failed; retain recovery backups: "
                + "; ".join(failures)) from error
        if isinstance(error, OSError):
            raise CookPublicationError(f"Could not publish cooked outputs; previous outputs restored: {error}") from error
        raise
    finally:
        if not retain_backups:
            for backup in backups.values():
                if backup is not None:
                    backup.unlink(missing_ok=True)
