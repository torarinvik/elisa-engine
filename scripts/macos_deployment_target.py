"""Read deployment requirements from Mach-O load-command output."""
import re

# The pinned Wicked backend creates MTL4 command queues unconditionally.
METAL_MINIMUM_MACOS = (26, 0, 0)


def deployment_targets(load_commands: str) -> list[tuple[int, int, int]]:
    targets = []
    for command in re.split(r"Load command \d+", load_commands):
        if re.search(r"\bcmd LC_BUILD_VERSION\b", command):
            if re.search(r"\bplatform (?:1|MACOS|macOS)\s", command) is None:
                raise ValueError("Mach-O contains a non-macOS build target")
            match = re.search(r"\bminos (\d+(?:\.\d+){0,2})\s", command)
        elif re.search(r"\bcmd LC_VERSION_MIN_MACOSX\b", command):
            match = re.search(r"\bversion (\d+(?:\.\d+){0,2})\s", command)
        else:
            continue
        if match is None:
            raise ValueError("Mach-O deployment target is malformed")
        components = [int(value) for value in match[1].split(".")]
        targets.append(tuple(components + [0] * (3 - len(components))))
    if not targets:
        raise ValueError("Mach-O has no macOS deployment target")
    return targets


def format_version(version: tuple[int, int, int]) -> str:
    return f"{version[0]}.{version[1]}" + (f".{version[2]}" if version[2] else "")
