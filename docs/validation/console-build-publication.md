# Console build publication — 2026-10-08

## Source repair

`scripts/elisa_build_run.py` now compiles console projects in a temporary directory
beside the final executable and replaces the destination only after successful
compilation produces a file. Failed attempts and success-without-output retain
the previous executable. Temporary artifacts are cleaned on every return; staged
publication errors become actionable build configuration errors. The native
Application build already stages its executable.

## Evidence

The regression fails twice against the previous implementation: a compiler that
writes before exiting 42 replaces the old executable, and success without a new
output incorrectly returns zero when the previous executable exists. Log:
`build/validation/console-publication-before.log`.

All 31 build-runner controls pass in 5.122 seconds, including publication failure,
missing output, successful replacement, executable permissions, temporary cleanup,
argument forwarding and existing author-file collision/asset-cook controls. Command:
`/opt/homebrew/bin/python3.14 -m unittest discover -s scripts -p test_elisa_build_run.py`.
Log: `build/validation/console-publication-controls.log`.

A fresh console project also uses the actual frozen compiler/runtime `52d60fcf`
with `--optimize`. Its executable exits 7; a subsequent undefined-name build
fails with status 1, leaves executable bytes identical, and the retained program
still exits 7. No temporary outputs remain. Evidence:
`build/validation/console-publication-real-compiler.json` and adjacent log.

This qualifies console executable publication, not the full native public-API
project cooking/packaging rehearsal. That acceptance and replacement compiler
qualification remain open. Atomic replacement covers the executable only; it is
not a multi-artifact transaction or a crash-durability guarantee.
