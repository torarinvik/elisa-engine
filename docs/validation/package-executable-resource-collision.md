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
