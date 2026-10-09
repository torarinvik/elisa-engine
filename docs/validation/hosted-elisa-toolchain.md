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

The 2026-10-09 upstream `main` heads observed with `git ls-remote` are Elisa-core
`735118cbe842f6b53be91fecd2ec7c90b294ee15`, Elisa-compiler
`b11e9121c64d94bbc8881db58ae850bf4933ceb9`, Elisa Proof
`747621d6de1f3412f7ceaa6480d61ec07e21e46b`, and ElisaScript
`62928532273f32e1f5e891d7194707c706605ba9`. The lock's core, proof and
ElisaScript pins are earlier commits. They remain unchanged until the complete
tuple is qualified; advancing only those pins would not establish compatibility.
The lock records these observations in `upstream_ref_heads`, and `--plan`
reports them beside the selected pins so future qualification can review both
the pinned tuple and the exact upstream heads that were observed.

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
pass 17/17, including malformed roots, pins, states and blockers, selected
compiler/head consistency, and drift detection. `--plan --verify-upstream`
currently verifies all four recorded `refs/heads/main` heads with `git ls-remote`
and keeps build execution deferred. The cross-platform CI job runs that online
check once on Ubuntu and runs the policy suites on every matrix host. Local
CI-stage invocations and workflow YAML validation pass; hosted Actions results
have not yet been observed.

After the exact proof/compiler/runtime tuple passes those gates, update the
qualification state and enable `--fetch`/`--build` in the macOS job. The
provisioner keeps source checkouts, logs, and products under
`build/hosted-toolchain`, records each subprocess in a machine-readable report,
verifies detached source identities, and hashes all produced tools and the
prover's own build manifest.
