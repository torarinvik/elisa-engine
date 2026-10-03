# Fast-iteration tooling (2026-10-03)

- `env.sh`: the Mac toolchain env. It uses current stage1 via `ELISA_STAGE1_BIN=$HOME/.elisac/stage1/bin/elisac-stage1`; the repo `bin/` stage1 predates Elisa-compiler eb70b30f and is about 100x slower on large tests. It also sets the remote proof host and `ELISA_REMOTE_CAP` (4 CPUs, our agreed share of the shared vast box).
- `gate_timed.sh <rev>`: runs the gate on a rev in `../elisa-engine-gate` and timestamps each line. It warms the caches first because the stock elisascript launcher kills children after about 3 minutes.
- `v80`: an ssh wrapper for the vast box `ssh9.vast.ai:15803`, which is cgroup v1 with a 38.4-CPU quota.
- `remote_stage1_bootstrap.sh` and `remote_prover_build.sh`: build a Linux stage1 and a Linux engine prover natively on the box.
- `scripts/prove_all.py`: cached, parallel proofs. Cache misses go to the hosts in `ELISA_PROOF_REMOTE`.
- `scripts/run_tests.py` with `scripts/gate_tests.json`: cached, parallel gate tests, scheduled slowest first. Remote compiles go through `scripts/remote_compile.py` and `ELISA_COMPILE_REMOTE`, which is off by default.

Measured with stage1 df344e01:
- Cold 210-test compile: about 30 s.
- Fully cold gate: about 81 s.
- Warm gate: about 36 s.
- Cold proofs: about 15 s.
