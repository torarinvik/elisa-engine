# Preserve author files when selecting executable output

## Demonstrated gap

A fresh project with `host: console`, `main: main.elisa` and executable output
`main.elisa` or `elisa.project.json` reached the compiler. An inert compiler
returned 42 without touching files, proving that preflight admitted both
collisions safely. A real compiler/linker could replace the author's entry or
manifest. Evidence: `build/validation/project-output-collision-before.json`.

## Source repair

`resolve_project_paths` now rejects output resolving to the selected entry source
or project manifest before creating output directories, cooking or invoking the
compiler. Existing files are also compared by identity to catch hardlinks; path
resolution already catches symlink aliases. The CLI returns 2, names the protected
file and prints no traceback. Identity-check I/O failures become configuration
errors. Distinct executable outputs keep the existing behavior.

## Focused acceptance — 2026-10-08

`/opt/homebrew/bin/python3.14 -m unittest discover -s scripts -p test_elisa_build_run.py`
passes all 30 tests in 4.807s. New controls cover relative, normalized, absolute,
symlink and hardlink entry collisions, plus direct and hardlinked manifest
collisions. Each checks that the compiler was not invoked and both author files
remain byte-identical. A distinct output still reaches the inert compiler.
The existing asset-cook, console, native-link, provenance and failure controls
remain in the suite. Retained terminal output:
`build/validation/project-output-collision-suite-terminal.log` (terminal chunk,
not the complete initial output).

This closes a build-path validation gap; it does not establish the full ordinary
public-API application's author/cook/package acceptance. That rehearsal remains
open on the qualified toolchain, along with relocated package and hardware gates.
