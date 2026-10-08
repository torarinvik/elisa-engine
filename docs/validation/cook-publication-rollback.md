# Cook output rollback — 2026-10-08

Asset cooks now prepare file backups beside each destination before publishing a
cook's output set. If a later replacement fails, earlier replacements are restored
and newly created outputs removed. Cache records are updated only after successful
publication; hashes are computed from staged files before replacements begin.
Publication errors become actionable configuration errors. Temporary outputs and
successful backups are cleaned. If rollback itself fails, recovery backups remain
and the diagnostic names the affected paths.

The old sequential replacement algorithm fails the new second-replacement controls
for both existing and absent first outputs (`build/validation/cook-publication-before.log`).
The focused helper suite passes three tests, covering old/absent files, permissions,
successful publication, cleanup and failed-rollback backup retention. Integration
also injects the second mesh/texture publication failure and asserts byte-identical
previous outputs/cache and no temporary files. All 36 collected runner controls
pass in 4.905 seconds:
`/opt/homebrew/bin/python3.14 -m unittest discover -s scripts -p test_elisa_build_run.py`.
Log: `build/validation/cook-publication-integrated-controls.log`.

The actual optimized image-cook project rehearsal was rerun successfully on this
source, covering fresh/cached/edited/corrupted/malformed input cases:
`build/validation/cook-publication-real-rehearsal.log` and
[rehearsal evidence](ordinary-image-cook-rehearsal.md).

This protects against synchronous filesystem failures, including an interrupted
Python operation for which rollback can still run. It does not make multiple files
atomically visible to readers, establish crash durability, or serialize concurrent
writers. A cache-write failure can leave successfully published outputs requiring
recooking; the cache never authenticates unchecked new bytes. Full native project
package acceptance remains open.

## Backup cleanup preserves the transaction outcome

A new injected `Path.unlink` refusal reproduced a cleanup exception after all
cooked outputs had already been published. The publication helper now treats
backup cleanup as best effort: refused deletion retains previous bytes and does
not replace a successful publication result (or mask the original rollback error).
The caller can therefore record cache state for the output that actually committed.

The new control fails against the previous source, with output retained in
`build/validation/cook-cleanup-before.log`. After repair,
`/opt/homebrew/bin/python3.14 -m unittest test_cook_publication test_elisa_build_run`
from `scripts/` passes 50 tests in 5.185 seconds; output:
`build/validation/cook-cleanup-after.log`. It verifies new bytes published, staged
file consumed and old bytes recoverable in the retained backup. Existing later
replacement failure and failed-rollback controls pass. No crash/concurrent-writer
atomicity or native package acceptance is claimed. This is Python filesystem
behavior outside Elisa prover modeling.
