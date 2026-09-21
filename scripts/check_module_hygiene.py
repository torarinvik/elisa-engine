"""Check namespace rules for production Elisa modules.

Production code qualifies dependencies through their module names.  Test and
example files may use imports to keep fixtures readable; the engine sources
must keep their public surface explicit.
"""

from pathlib import Path
import re
import sys


SOURCE_ROOT = Path("src")
EXAMPLE_ROOT = Path("examples")
TEST_ROOT = Path("test")
ELISA_SUFFIX = ".elisa"
PUBLIC_INCLUDE_BUNDLES = {Path("src/runtime/public.elisa")}
TOP_LEVEL_MODULE = re.compile(r"^module\s+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)\s*:")
USING_DIRECTIVE = re.compile(r"^\s*using\s+[A-Za-z_][A-Za-z0-9_]*\s*$")
INCLUDE_DIRECTIVE = re.compile(r'^\s*include\s+"([^"\r\n]+)"\s*$')
LEGACY_CONSTRUCTOR = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*_new\s*\(")
FUNCTION_START = re.compile(r"^(\s*)def\s+[A-Za-z_][A-Za-z0-9_]*\s*\(")
REFERENCE_PARAMETER = re.compile(
    r"\b([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(?:mutable\s+)?[A-Za-z_][A-Za-z0-9_:]*(?:\[[^\]]*\])?&"
)


def rereferenced_parameters(lines: list[str]) -> list[tuple[int, str]]:
    """Find `&name` where `name` is already a reference parameter.

    The stage1 compiler accepts `&param` in call arguments and binds it to the
    parameter's own storage instead of the referenced value, so reads and
    writes silently hit the wrong memory. Pass the reference as `param`.
    """
    findings: list[tuple[int, str]] = []
    index = 0
    while index < len(lines):
        start = FUNCTION_START.match(lines[index])
        if start is None:
            index += 1
            continue
        indent = len(start.group(1))
        signature = lines[index]
        last = index
        while not signature.rstrip().endswith(":") and last + 1 < len(lines):
            last += 1
            signature += " " + lines[last].strip()
        names = REFERENCE_PARAMETER.findall(signature.split("->")[0])
        patterns = [(name, re.compile(rf"&{name}(?![A-Za-z0-9_.\[])")) for name in names]
        body = last + 1
        while body < len(lines):
            line = lines[body]
            if line.strip() and len(line) - len(line.lstrip()) <= indent:
                break
            code = line.split("#", 1)[0]
            for name, pattern in patterns:
                if pattern.search(code):
                    findings.append((body + 1, name))
            body += 1
        index = body
    return findings


def policy(root: Path) -> dict[str, object]:
    source_root = root / SOURCE_ROOT
    violations: list[str] = []
    module_names: dict[str, Path] = {}

    source_paths = sorted(source_root.rglob(f"*{ELISA_SUFFIX}"))
    source_paths.extend(sorted((root / EXAMPLE_ROOT).rglob(f"*{ELISA_SUFFIX}")))
    for path in source_paths:
        lines = path.read_text(encoding="utf-8").splitlines()
        relative = path.relative_to(root)
        is_production = relative.parts[0] == SOURCE_ROOT.name
        if is_production:
            if relative in PUBLIC_INCLUDE_BUNDLES:
                includes = 0
                for line_number, line in enumerate(lines, start=1):
                    stripped = line.strip()
                    if not stripped or stripped.startswith("#"):
                        continue
                    include = INCLUDE_DIRECTIVE.match(line)
                    if include is None:
                        violations.append(
                            f"{relative}:{line_number}: public include bundles may contain only include directives"
                        )
                        continue
                    includes += 1
                    target = (path.parent / include.group(1)).resolve()
                    if not target.is_relative_to(source_root.resolve()) or not target.is_file():
                        violations.append(
                            f"{relative}:{line_number}: include must resolve to a source file under src/"
                        )
                if includes == 0:
                    violations.append(f"{relative}: public include bundle must include at least one module")
            else:
                module = next(
                    (match for line in lines if (match := TOP_LEVEL_MODULE.match(line))),
                    None,
                )
                if module is None:
                    violations.append(f"{relative}: missing top-level module declaration")
                else:
                    name = module.group(1)
                    previous = module_names.get(name)
                    if previous is not None:
                        violations.append(
                            f"{relative}: module {name} duplicates {previous.relative_to(root)}"
                        )
                    module_names[name] = path

        for line_number, line in enumerate(lines, start=1):
            if is_production and USING_DIRECTIVE.match(line):
                violations.append(
                    f"{relative}:{line_number}: production modules must qualify dependencies"
                )
            if LEGACY_CONSTRUCTOR.search(line):
                violations.append(
                    f"{path.relative_to(root)}:{line_number}: use named constructor syntax instead of *_new(...)"
                )

    for path in [*source_paths, *sorted((root / TEST_ROOT).rglob(f"*{ELISA_SUFFIX}"))]:
        lines = path.read_text(encoding="utf-8").splitlines()
        for line_number, name in rereferenced_parameters(lines):
            violations.append(
                f"{path.relative_to(root)}:{line_number}: pass reference parameter {name} as `{name}`, not `&{name}`"
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
