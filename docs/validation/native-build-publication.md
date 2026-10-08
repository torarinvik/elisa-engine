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
