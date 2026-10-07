# Compiler 63585c5f installation

On 2026-10-07 the pulled compiler revision
`63585c5f56bd3c18d9ff983503bac8f005df40b4` was rebuilt from a Git archive in
`build/compiler-63585c5f`. The snapshot has its own Git metadata identifying
that exact commit. The compiler checkout was clean when inspected.

This revision includes the semantic owner-index and precondition/store-through
hash-set performance changes. Their effect on engine workloads is not yet
measured.

The clean Stage0 seed completed with an 8 GiB memory guard after another chat's
6 GiB seed hit its limit. The resulting compiler and runtime were installed
through `scripts/install_stage1.sh` into the immutable `~/.elisac/stage1`
snapshot. The source freshness/provenance check passed before installation.

| Product | SHA256 |
| --- | --- |
| Stage1 | `76f9b14ce3ea16b3abd730177e608d4aa43bbc5457561b1b298efe7af497b905` |
| Runtime | `02d868eb68739e517830684eb89bb820bec16f8d07f92ff7bbacee35ef188cb6` |

Evidence: `build/validation/compiler-63585c5f-seed.log` and the installed
`SNAPSHOT` and `bin/elisac-stage1.provenance.json` records.

Both strict O2 prover products are being rebuilt with this compiler and an
archived parser source at the same revision. Until that build and subsequent
checks complete, the established engine qualification remains the earlier
compiler's 68/73 proof sweep. Installation does not establish compatibility,
performance gains, or completion of the shared gate.
