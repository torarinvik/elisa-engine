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
