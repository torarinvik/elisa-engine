# Hosted Elisa toolchain

`scripts/hosted_toolchain.lock.json` records full commit SHAs for Elisa-core,
Elisa-compiler, Elisa Proof, and ElisaScript. The revisions were compared with
`HEAD` and `refs/heads/main` using `git ls-remote` on 2026-10-09. Go is pinned
from the checked-out Elisa-core `compiler/go.mod` (`1.25.0`). The documented
bootstrap recipe is `make -C "$ELISA_CORE/compiler" build`, followed by
`scripts/elisac_stage1.sh --seed`; the prover uses `scripts/build.sh`, and the
ElisaScript executable is emitted by stage1 from
`src/driver/elisascript.elisa`.

## Current prerequisite

The compiler pin is `b11e9121c64d94bbc8881db58ae850bf4933ceb9`, verified on
upstream `main` on 2026-10-09. This clears the earlier unpublished
machine-over compiler prerequisite. Hosted builds remain blocked because the
exact pinned proof/compiler/runtime tuple is not qualified with default
`Global.Read`/`Global.Write` enforcement. A proof rebuild against callback
compiler `b719dbd5` failed because proof/compiler sources lack required grants.
Do not enable hosted builds or use `-permissive` until the pinned proof pair
passes a strict build, independent replay, the original 265 obligations, five
CLI regressions, and all 73 engine reports.

The lock records `build_eligibility: blocked_qualification` and a typed,
actionable blocker. `--build` writes it to `toolchain-manifest.json` and exits
before fetching or compiling anything. Once the proof/compiler/runtime tuple is
qualified, `--build` first runs `go version` and requires an exact match with
the lock before fetching any sources or starting a build stage. All
post-initialization errors are retained with a terminal `failed` or `blocked`
state. `--plan` is safe on every CI host and checks the full-SHA lock and
documented command graph; its JSON says `hosted_build_execution: deferred` and
explains the current qualification blocker. The cross-platform `cook` job
retains that preflight in
`build/ci/cook-<os>.json`; the macOS headless job remains limited to its existing
native dependency and sanitizer gates. No hosted toolchain build or Actions
execution is claimed yet.

The lock reader also rejects malformed JSON shapes and field types with a
controlled error: the root and repository pins must be objects, revisions,
URLs, eligibility, and blocker fields must have their declared string types,
and a ready lock cannot retain a blocker. The focused provisioner controls
pass 13/13, including malformed roots, pins, states and blockers. The
cross-platform CI job runs those controls alongside the global-grant harness;
the workflow YAML and local CI-stage invocations validate, while no hosted
Actions result is claimed yet.

After the exact proof/compiler/runtime tuple passes those gates, update the
qualification state and enable `--fetch`/`--build` in the macOS job. The
provisioner keeps source checkouts, logs, and products under
`build/hosted-toolchain`, records each subprocess in a machine-readable report,
verifies detached source identities, and hashes all produced tools and the
prover's own build manifest.
