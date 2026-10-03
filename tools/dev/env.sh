# elisa-engine toolchain env (durable; the session scratchpad gets wiped).
export T=$HOME/.cache/elisa-engine-tools
cd "/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/elisa-engine"
export PATH=/opt/homebrew/bin:$HOME/.local/bin:$PATH DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_ALLOW_STALE_STAGE1=1 ELISA_COMPILER_BIN=$PWD/../Elisa-compiler/scripts/elisac_stage1.sh ELISA_PROOF_BIN=$PWD/../elisa-engine-proof/build/elisa-proof
# Remote workers on the 80-core / 38.4-CPU vast box (ssh9.vast.ai:15803).
export ELISA_PROOF_REMOTE="root@ssh9.vast.ai#15803:/root/work/elisa-engine-runner/elisa-proof-bin"
# Since stage1 df344e01 a cold 210-test compile is ~27 s on the Mac alone; ssh overhead beats the gain.
# export ELISA_COMPILE_REMOTE="root@ssh9.vast.ai#15803:/root/work/engine-compile/elisac-stage1"
export ELISA_REMOTE_CAP=4  # agreed share of ssh9 (elisa-proof 16, compiler 14, engine+boxing 8)
# Current stage1 (df344e01, installed by the compiler session); the repo bin/ one predates eb70b30f and is ~100x slower on big tests.
export ELISA_STAGE1_BIN=$HOME/.elisac/stage1/bin/elisac-stage1
