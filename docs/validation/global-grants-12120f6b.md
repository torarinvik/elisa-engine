# Compiler grant and engine adoption evidence — 12120f6b

## Exact compiler product

The combined grant-adoption, nested permission-match repair and function-value
aggregate source is `12120f6b7148ce3f72ea8fba66be29b8cf2825d3`. A fresh Stage1
was seeded from clean Stage0 `735118cb`; source-tree SHA256 is
`58c14bb3905819a652cc7822274fa77cc615a30c7d3b31eb5c4d2be613f1e57e`, Stage1
SHA256 is `356d4a14433a90fb14dbf1cfac82e5856eb55205ab9b59da2c7b4eb03a757fbb`,
and matching runtime SHA256 is
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
Stage1 provenance and freshness pass.

On this exact product, the compiler-driver strict check exits zero with no
diagnostics. The actual CLI gate passes 24/24 controls, including ungranted
read/write/read-modify-write refusals and explicit `-permissive` bypasses. The
103-case authority reporter passes, and Stage0/Stage1 inferred Global rows
agree. The nested permission-match regression and 1,024-byte large-aggregate
O2 smoke pass. The aggregate smoke fixture needed local `Global.Read/Write`
scopes under the new default policy; that test-source update is uncommitted in
the compiler worktree.

Gen3 self-host passes stages A–D: 5/5 stage-A regressions, gen2-to-gen3
compilation, byte-identical gen3/gen4 fixpoint, and the same emitted object over
40 reproducibility runs. The actual-CLI report is
`build/validation/global-grant-12120-cli.json`; strict-check and gen3 logs are in
the compiler candidate worktree's `build/validation/` directory.

## Engine consumer adoption

This exact pair exposed missing local body grants in two engine sources that
already declared caller effects. `examples/maze/capi.elisa` had 49
direct-access/call diagnostics. Narrow body scopes now authorize those accesses
while retaining the effect signatures. Its strict check passes, and
`scripts/embed_probe.py` builds the C archive, links the native host, exercises
session/gameplay exports and SDL live input, and exits 0. Log:
`build/validation/global-grant-12120-engine/embed-probe.log`.

`test/viewport_gizmo.elisa` had 12 diagnostics across its global callback count,
callback invocations and callers. Narrow scopes now cover those stateful sites.
Its strict check passes and its built executable returns 0 with all assertions;
logs are in `build/validation/global-grant-12120-engine/`.

The full engine gate manifest has 216 Elisa entrypoints. Every entrypoint passes
`-emit check` on this exact Stage1. The uncached compile-and-run gate also passes
all 216 tests (0 cached, 0 remote, 29 seconds). The census report is
`build/validation/global-grant-gate-census-12120-final.json`; the gate output is
recorded by `scripts/run_tests.py`.

These results qualify the compiler candidate and the gate-manifest consumers;
they do not qualify the proof pair, every engine source outside that manifest,
or a current Studio app.
The callback hidden-region/result-arena repair is not in `12120f6b`; the
reproduced null-arena FBX crash remains open.

The compiler checkout has since advanced locally through `e6b5a0c4` and has an
uncommitted JSON grant change. No Stage1/runtime from that exact source state is
qualified yet. `12120f6b` is the last authenticated candidate, not the newest
compiler source; rerun this engine gate after the compiler owner produces a
clean matching product.
