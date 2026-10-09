"""Generate the macOS launcher and record executable/source identity."""

from __future__ import annotations

import json
import re
import shlex
import stat
import subprocess
from pathlib import Path

from macos_bundle_support import SHADER_MANIFEST_NAME



def source_revision(project: Path) -> str:
    """Return the packaged project's git revision, marking uncommitted changes."""
    try:
        revision = subprocess.run(["git", "-C", str(project), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=True, timeout=10).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(project), "status", "--porcelain", "--", "."],
            capture_output=True, text=True, check=True, timeout=30).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    return f"{revision}-dirty" if dirty else revision


def executable_build_identity(executable: Path) -> str:
    sidecar = executable.with_name(executable.name + ".provenance.json")
    try:
        record = json.loads(sidecar.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return "unavailable"
    identity = record.get("build_identity") if isinstance(record, dict) else None
    if not isinstance(identity, str) or re.fullmatch(r"[0-9a-fA-F]{16}", identity) is None:
        return "unavailable"
    return identity.lower()


def write_launcher(path: Path, binary_name: str,
    window: tuple[str, int, int] = ("Elisa Engine", 1280, 720), identity: str = "",
    bundle_id: str = "org.elisa.application", build_identity: str = "unavailable") -> None:
    # The runtime reads its window settings from the environment, which the
    # build runner sets from elisa.project.json. A double-clicked bundle has
    # no runner, so the launcher supplies the same defaults while still
    # letting an explicitly exported value win. The identity line goes to
    # stderr first so every log names the build that produced it.
    title, width, height = window
    announce = f"printf '%s\\n' {shlex.quote(identity)} >&2 || :\n" if identity else ""
    crash_ips_pattern = shlex.quote(f"{path.name}*.ips")
    crash_legacy_pattern = shlex.quote(f"{path.name}*.crash")
    crash_identity = (f"if [ -z \"${{ELISA_BUILD_IDENTITY:-}}\" ]; then ELISA_BUILD_IDENTITY={shlex.quote(build_identity)}; fi\n"
        "export ELISA_BUILD_IDENTITY\n") if identity else ""
    script = f"""#!/bin/sh
set -eu
resources=\"$(CDPATH= cd -- \"$(dirname -- \"$0\")/../Resources\" && pwd)\"
cd \"$resources\"
: \"${{ELISA_PROJECT_TITLE:={shlex.quote(title)}}}\"
: \"${{ELISA_PROJECT_WIDTH:={width}}}\"
: \"${{ELISA_PROJECT_HEIGHT:={height}}}\"
export ELISA_PROJECT_TITLE ELISA_PROJECT_WIDTH ELISA_PROJECT_HEIGHT
# Local crash reports only: the runtime writes crash-PID.txt here on a fatal
# signal. An exported ELISA_CRASH_DIR wins; an uncreatable one turns them off.
if [ -z \"${{ELISA_CRASH_DIR:-}}\" ] && [ -n \"${{HOME:-}}\" ]; then
    ELISA_CRASH_DIR=\"$HOME/Library/Logs/{Path(binary_name).stem}\"
fi
if [ -n \"${{ELISA_CRASH_DIR:-}}\" ] && mkdir -p \"$ELISA_CRASH_DIR\" 2>/dev/null; then
    export ELISA_CRASH_DIR
else
    unset ELISA_CRASH_DIR
fi
{crash_identity}if [ -d \"$resources/shaders\" ]; then
    ELISA_ENGINE_SHADER_PATH=\"$resources/shaders\"
    export ELISA_ENGINE_SHADER_PATH
    if [ -f \"$resources/shaders/{SHADER_MANIFEST_NAME}\" ]; then
        ELISA_ENGINE_SHADER_MANIFEST=\"$resources/shaders/{SHADER_MANIFEST_NAME}\"
        export ELISA_ENGINE_SHADER_MANIFEST
    fi
fi
{announce}log_directory=\"${{HOME:-}}/Library/Logs/Elisa\"
log_directory=\"$log_directory\"/{shlex.quote(bundle_id)}
log_file=\"\"
if [ -n \"${{HOME:-}}\" ] && mkdir -p \"$log_directory\" 2>/dev/null; then
    log_file=\"$log_directory/latest.log\"
    if [ -f \"$log_file\" ]; then
        cp \"$log_file\" \"$log_directory/previous.log\" 2>/dev/null || :
    fi
    if ! {{
        printf '%s\\n' {shlex.quote(identity or 'Elisa application')}
        printf 'build_identity=%s\\n' {shlex.quote(build_identity)}
        printf 'started_utc=%s\\n' \"$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || printf unknown)\"
        printf '%s\\n' '--- process output ---'
    }} > \"$log_file\" 2>/dev/null; then
        log_file=\"\"
    fi
fi
if [ -n \"$log_file\" ]; then
    printf 'launcher_log=%s\\n' \"$log_file\" >&2 || :
    if \"$resources/{binary_name}\" \"$@\" >> \"$log_file\" 2>&1; then
        status=0
    else
        status=$?
    fi
    printf '\\nprocess_exit_status=%s\\n' \"$status\" >> \"$log_file\" 2>/dev/null || :
else
    if \"$resources/{binary_name}\" \"$@\"; then
        status=0
    else
        status=$?
    fi
fi
if [ \"$status\" -ne 0 ]; then
    home_directory=\"$(/usr/bin/printenv HOME 2>/dev/null || :)\"
    diagnostic_reports=\"$home_directory/Library/Logs/DiagnosticReports\"
    if [ -n \"$home_directory\" ] && [ -d \"$diagnostic_reports\" ]; then
        if [ -n \"$log_file\" ]; then
            {{
                printf '%s\\n' 'recent macOS crash report candidates (match timestamps to started_utc):'
                find \"$diagnostic_reports\" -type f \\
                    \\( -name {crash_ips_pattern} -o -name {crash_legacy_pattern} \\) \\
                    -mtime -1 -print
            }} >> \"$log_file\" 2>/dev/null || :
        else
            printf '%s\\n' 'recent macOS crash report candidates (match timestamps to this run):' >&2 || :
            find \"$diagnostic_reports\" -type f \\
                \\( -name {crash_ips_pattern} -o -name {crash_legacy_pattern} \\) \\
                -mtime -1 -print >&2 2>/dev/null || :
        fi
    fi
fi
printf 'Elisa process exit status: %s\\n' \"$status\" >&2 || :
exit \"$status\"
"""
    path.write_text(script, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
