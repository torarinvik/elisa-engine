# elisa-engine toolchain env (durable; the session scratchpad gets wiped).
export T=$HOME/.cache/elisa-engine-tools
cd "/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/elisa-engine"
export PATH=/opt/homebrew/bin:$HOME/.local/bin:$PATH DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_ALLOW_STALE_STAGE1=1 ELISA_COMPILER_BIN=$PWD/../Elisa-compiler/scripts/elisac_stage1.sh ELISA_PROOF_BIN=$PWD/../elisa-engine-proof/build/elisa-proof
# Local compute is the default. Set remote variables explicitly for a verified host.
# Current stage1 (df344e01, installed by the compiler session); the repo bin/ one predates eb70b30f and is ~100x slower on big tests.
export ELISA_STAGE1_BIN=$HOME/.elisac/stage1/bin/elisac-stage1
