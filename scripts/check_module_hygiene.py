"""Check namespace rules for production Elisa modules.

Production code qualifies dependencies through their module names.  Test and
example files may use imports to keep fixtures readable; the engine sources
must keep their public surface explicit.
"""

from pathlib import Path
import re
import sys


SOURCE_ROOT = Path("src")
ELISA_SUFFIX = ".elisa"
TOP_LEVEL_MODULE = re.compile(r"^module\s+([A-Za-z_][A-Za-z0-9_]*)\s*:")
USING_DIRECTIVE = re.compile(r"^\s*using\s+[A-Za-z_][A-Za-z0-9_]*\s*$")


def policy(root: Path) -> dict[str, object]:
    source_root = root / SOURCE_ROOT
    violations: list[str] = []
    module_names: dict[str, Path] = {}

    for path in sorted(source_root.rglob(f"*{ELISA_SUFFIX}")):
        lines = path.read_text(encoding="utf-8").splitlines()
        module = next(
            (match for line in lines if (match := TOP_LEVEL_MODULE.match(line))),
            None,
        )
        if module is None:
            violations.append(f"{path.relative_to(root)}: missing top-level module declaration")
        else:
            name = module.group(1)
            previous = module_names.get(name)
            if previous is not None:
                violations.append(
                    f"{path.relative_to(root)}: module {name} duplicates {previous.relative_to(root)}"
                )
            module_names[name] = path

        for line_number, line in enumerate(lines, start=1):
            if USING_DIRECTIVE.match(line):
                violations.append(
                    f"{path.relative_to(root)}:{line_number}: production modules must qualify dependencies"
                )

    return {
        "status": "passed" if not violations else "failed",
        "modules": len(module_names),
        "violations": [str(item) for item in violations],
    }


def main() -> int:
    result = policy(Path(__file__).resolve().parent.parent)
    violations = result["violations"]
    if violations:
        print("Module hygiene policy failed:", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1

    print(f"Module hygiene policy passed ({result['modules']} production modules).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
