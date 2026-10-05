# Hosted Elisa toolchain

`scripts/hosted_toolchain.lock.json` records full commit SHAs for Elisa-core,
Elisa-compiler, Elisa Proof, and ElisaScript. The revisions were compared with
`HEAD` and `refs/heads/main` using `git ls-remote` on 2026-10-05. Go is pinned
from the checked-out Elisa-core `compiler/go.mod` (`1.25.0`). The documented
bootstrap recipe is `make -C "$ELISA_CORE/compiler" build`, followed by
`scripts/elisac_stage1.sh --seed`; the prover uses `scripts/build.sh`, and the
ElisaScript executable is emitted by stage1 from
`src/driver/elisascript.elisa`.

## Current prerequisite

The lock's published compiler revision is `72a752820ab581d46bb17b3fb7158ba3879a16e3`.
The current ElisaScript source uses branch-local `machine over` transitions and
needs Elisa-compiler commit `955cde86f336dff0945e8918303e9f974cc97532`
(`Support branch-local machine-over transitions`). That exact commit exists in
the local `codex/edir-host-effects` checkout, but `git ls-remote` did not show
it under an upstream ref on 2026-10-05. It must not be fetched by assuming a
branch name or used as a hosted lock entry until it is published and verified.

For that reason `build_eligibility` is currently
`blocked_upstream_compatibility`. `--build` writes the actionable blocker to
`toolchain-manifest.json` and exits before fetching or compiling anything.
When the pin is ready, `--build` first runs `go version` and requires an exact
match with the lock before fetching any sources or starting a build stage. All
post-initialization errors are retained with a terminal `failed` or `blocked`
state. `--plan` is safe on every CI host and checks the full-SHA lock and
documented command graph; its JSON says `hosted_build_execution: deferred` and
explains the unpublished-compiler prerequisite. The cross-platform `cook` job
retains that preflight in
`build/ci/cook-<os>.json`; the macOS headless job remains limited to its existing
native dependency and sanitizer gates. No hosted toolchain build or Actions
execution is claimed yet.

Once the compatible compiler change is reachable from a verified upstream ref,
update the compiler SHA and clear the blocker in the lock. Then enable
`--fetch`/`--build` in the macOS job. The provisioner keeps source checkouts,
logs, and products under `build/hosted-toolchain`, records each subprocess in a
machine-readable report, verifies detached source identities, and hashes all
produced tools and the prover's own build manifest.
