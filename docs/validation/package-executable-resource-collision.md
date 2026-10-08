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
