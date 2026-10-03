#!/bin/bash
set -e; cd /root/work/elisa-engine-runner; C=/root/work/engine-compile/stage1-d8b5; V=19
export ELISA_HOST_LINUX=1 ELISA_HOST_X86_64=1
ELISA_STAGE1_RUNTIME_STD=1 $C -emit obj -O0 -o rt0.o Elisa-compiler/elisacore_std/native_runtime_support.elisa
bash Elisa-compiler/scripts/write_profiler_hook_fallbacks.sh --host-callbacks > rthooks.c
clang-$V -fno-builtin -c -o rthooks.o rthooks.c
clang-$V -c -O2 -o profile_hooks.o Elisa-compiler/test/parity/profile_hooks.c || true
/usr/bin/time -f "prover %e s %M KB" $C -emit obj -O2 -o prover.o elisa-proof/src/main.elisa
clang-$V -no-pie -o elisa-proof.new prover.o rt0.o rthooks.o -Wl,-z,stack-size=536870912 2>&1 | tail -5
mv elisa-proof.new elisa-proof-bin; ./elisa-proof-bin elisa-proof/examples/verified.elisa | tail -2
