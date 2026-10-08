# Native executable and runtime-link publication

The native build runner previously replaced the destination executable before
staging Wicked runtime links. A collision on the second runtime link could fail
that build after changing the first link and replacing the previous executable.

Runtime staging now validates both destinations and creates all replacement links
and exact previous-link backups before mutation. Native builds publish runtime
links first and replace the executable last. A failed link or executable replacement
restores changed links in reverse order. A failed rollback retains the recovery
directory, previous symlink contents and destination mapping, and reports its path.
Regular files beside the executable are never replaced by runtime-link staging.
Optional-library removal participates in the same rollback. Reserved library names
are refused as executable names. Cleanup after committed publication is best effort.

Validation: `/opt/homebrew/bin/python3.14 -m unittest test_elisa_build_run`, run
from `scripts/`, passes 41 controls in 4.294 seconds. Retained output:
`build/validation/native-publication-controls.log`. New controls cover a second-link
failure, final executable failure after optional-link removal, second-link regular
file collision, failed rollback recovery and successful publication. Existing
build/cook/console-publication controls remain included. The initial success
assertion needed macOS path-alias normalization; it did not reveal a source failure.

This is host filesystem publication behavior; the Elisa prover does not model
Python filesystem operations. Fault controls establish rollback outcomes. A real
fresh optimized application/package rehearsal remains pending the qualified
compiler tuple. Concurrent observers can see intermediate runtime-link changes;
this operation does not claim atomic visibility across multiple paths. Crash or
power-loss recovery is not established.

## Provenance joins the publication transaction

Native builds previously wrote provenance after executable publication and treated
manifest failure as build success. A stale sidecar could then identify the previous
executable. The runner now prepares provenance from the staged binary, recording
its final output path, before changing runtime links. Manifest preparation failure
returns a build error and preserves the previous executable, sidecar and links.
The sidecar publishes after runtime links and before the executable. Failed final
publication restores the previous regular sidecar, exact symlink target, or absence.

`/opt/homebrew/bin/python3.14 -m unittest test_elisa_build_run test_build_provenance`
from `scripts/` passes 48 tests in 5.479 seconds. Output:
`build/validation/native-provenance-controls.log`. Controls inject preparation,
sidecar replacement and final executable failures, validate directory/missing
sidecar refusal, observe publication order, and verify staged-byte hash/size with
final-path identity without modifying old output. The runner control confirms the
linker completed but the application was not run after manifest preparation failed.
Existing failed-rollback recovery controls remain included. These are host Python
filesystem controls, with no Elisa proof obligation count; the prover does not model
these operations. Actual optimized package acceptance and crash/concurrent-observer
limitations above remain open.

## Registered gate coverage

`scripts/native_unit_tests.py` now invokes the build-runner, build-provenance and
VM-region report controls as part of its existing fail-fast native-facing unit
stage. Executing these registered commands directly passes 46, 3 and 3 tests,
respectively. Logs: `build/validation/registered-build-publication-controls.log`,
`registered-build-provenance-controls.log` and `registered-vm-report-controls.log`.
This closes manual-only coverage for these source repairs. The full native-unit
stage, hosted CI and actual renderer acceptance were not run by this registration
change; the stage's later C++/GPU-related prerequisites retain their own gates.

The same stage now also registers `test_cook_publication.py` on every host and
`test_package_macos_app.py` on macOS. Their most recent focused acceptance is
retained in `build/validation/cook-cleanup-after.log` (four cook controls included
in the 50-test combined run) and `package-directory-symlinks.log` (41 package
controls). Registration retains fail-fast dispatch and does not relabel focused
controls as full native-stage or hosted execution. macOS package behavior still
requires its own native and clean-machine acceptance.
