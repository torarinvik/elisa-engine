#!/usr/bin/env bash
# Run the gate on a rev in ../elisa-engine-gate with a long process timeout, timestamping lines.
source $HOME/.cache/elisa-engine-tools/env.sh >/dev/null
REV=${1:-HEAD}; G="$PWD/../elisa-engine-gate"
git -C "$G" checkout -q --force --detach $(git rev-parse $REV) || exit 9
cd "$G"; elisascript --check scripts/check.elisascript || exit 8
# The launcher kills children busy for more than ~3 min, so warm the compile and proof caches with
# the same commands first; inside the gate they become cache hits (the test binaries still run there).
w0=$(date +%s)
python3 scripts/run_tests.py "$ELISA_COMPILER_BIN" -j 10 | tail -1 || exit 7
python3 scripts/prove_all.py "$ELISA_PROOF_BIN" -j 4 | tail -1 || exit 6
echo "warm-up $(( $(date +%s)-w0 ))s"
t0=$(date +%s)
elisascript scripts/check.elisascript 2>&1 | while IFS= read -r l; do printf '%5d %s\n' $(( $(date +%s)-t0 )) "$l"; done
echo "rc=${PIPESTATUS[0]} total=$(( $(date +%s)-t0 ))s"
