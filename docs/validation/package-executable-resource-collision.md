# Package resource collision with the executable

A focused reproduction using the existing macOS package fixture declared
`Game.bin` as an ordinary project resource. Packaging returned success, but
`Contents/Resources/Game.bin` contained the resource bytes instead of the built
executable. The resource copy ran after executable staging.

The packager now refuses resource paths whose first component names the generated
executable, including descendants and case variants. This reservation applies on
all hosts so a bundle remains valid on case-insensitive macOS filesystems. The
error identifies the offending manifest resource. Assembly occurs in the existing
staging directory; refusal preserves the published bundle and author resource.

Validation: `/opt/homebrew/bin/python3.14 -m unittest test_package_macos_app`
from `scripts/` passes 35 tests in 1.741 seconds. Retained output:
`build/validation/package-executable-resource-collision.log`. New controls cover
exact and lowercase overwrites, preservation of the previous executable and bundle
marker, unchanged author resource, and a descendant that treats the executable as
a directory. Existing packaging, relocation-launcher and rollback controls pass.
These controls use shell fixture executables; actual optimized native package and
GPU acceptance remain open. The Elisa prover does not model this Python filesystem
operation; no implementation-linked Elisa obligation count is claimed.

## Bundle destination and post-publication cleanup

A second reproduction used a regular author file at the `.app` destination.
Packaging replaced it with a new bundle, then raised `NotADirectoryError` while
removing its backup. The destination is now required to be a directory when it
exists, and this check runs before assembly. The author file remains unchanged.
After successful bundle replacement, backup cleanup uses best-effort removal;
cleanup failure leaves a recoverable previous bundle and does not report that the
already committed publication failed.

The full package suite now passes 37 tests in 1.802 seconds; output is
`build/validation/package-publication-controls.log`. New controls assert refusal
before assembly and preservation of the regular author file, plus successful
publication with a retained previous-bundle marker when backup cleanup is skipped.
Existing replacement-failure rollback controls also pass. Native/GPU and crash
acceptance remain open as described above.

## Declared-resource parent symlinks

A declared `linked/payload` resource bypassed the existing leaf-symlink check:
`linked` pointed outside the project, and packaging silently copied external bytes.
Resource staging now checks each declared path component before copying. It refuses
parent symlinks to both external and internal directories, preserving the existing
contract that declared resources must not use symlinks. The error names the link
and requested resource. This is a pre-copy path check, not a concurrent filesystem
race guarantee.

The package suite passes 38 tests in 1.894 seconds, retained in
`build/validation/package-resource-parent-symlink.log`. The new control exercises
both external and internal symlink targets, verifies the previous bundle marker
remains and no linked resource is published, and checks external source bytes are
unchanged. Native/GPU and clean-machine acceptance remain open.

## Explicit cooked resources

Valid manifests listing `build/cooked` or `build/cooked/player.pkg` failed with
`FileExistsError`: explicit staging created the destination that automatic cooked
staging subsequently required to be absent. Automatic cooked staging now merges
into its destination. Both copies use the same project-relative source; other
resource-directory copies retain their existing collision behavior.

The full package suite passes 39 tests in 2.660 seconds, retained in
`build/validation/package-explicit-cooked-resources.log`. Controls cover directory
and individual-file declarations, byte equality for both declared and additionally
cooked files, unchanged packaged executable and exclusion of undeclared assets.
The native optimized application/package gate remains open.
