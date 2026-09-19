# Elisa standard library

`elisa_math.elisa` is an unmodified copy of `elisacore_std/elisacore_math.elisa`
from Elisa-compiler revision `c2922aa2714bfa0263a94b2f315c7372d0544b63`.
SHA-256: `5dfe642f3ce8d3d79e84a10aca45c7bc85e950ee4ad4ebeaf9c67c5597a08c7b`.

The local copy keeps engine builds independent of a sibling compiler checkout.
Update it from upstream rather than maintaining a separate implementation.
On Linux and FreeBSD consumers of the libm bindings must link with `-l m`;
macOS supplies them in libSystem.
