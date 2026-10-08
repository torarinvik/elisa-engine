# Cook output file types — 2026-10-08

The shared declared-project-path validator now rejects existing non-file paths
for outputs and dependency bundles, as it already did for required source files.
This runs during declaration validation, before cooker invocation or publication.

Previously, a mesh-plus-texture cook targeting an existing directory reached
`os.replace` and raised an uncaught `IsADirectoryError`. When the texture was the
blocked destination, mesh publication happened first. The new regression fails
against that source for both destinations:
`build/validation/cook-directory-output-before.log`.

All 32 runner controls pass in 5.889 seconds using
`/opt/homebrew/bin/python3.14 -m unittest discover -s scripts -p test_elisa_build_run.py`;
log `build/validation/cook-directory-output-controls.log`. The new controls assert
no cooker invocation, unchanged companion output and directory contents, no cache
publication, and no temporary files for either mesh or texture destination errors.

An actual console-project CLI declaration with an image output directory exits 2
with an actionable path error and no traceback, preserving the source and directory:
`build/validation/cook-directory-cli.json`. No compiler or image decoder is needed
to reject that configuration.

This closes the pre-existing destination-type defect. Concurrent filesystem
replacement and arbitrary multi-output publication failures still need separate
transactional treatment; this preflight is not a general multi-file atomicity claim.
The full public-API author/cook/package rehearsal remains open.
