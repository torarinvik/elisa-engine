#!/bin/bash
# usage: bootstrap.sh COMPILER OUT LLVMVER
set -e
cd /root/work/engine-compile
export ELISA_HOST_LINUX=1 ELISA_HOST_X86_64=1
C=$1; OUT=$2; V=$3
/usr/bin/time -f "driver %e s %M KB" $C -emit obj -O3 -o $OUT.o ec/src/driver/elisac.elisa
ELISA_STAGE1_RUNTIME_STD=1 $C -emit obj -O0 -o $OUT-rt0.o ec/elisacore_std/native_runtime_support.elisa
bash ec/scripts/write_profiler_hook_fallbacks.sh --host-callbacks > $OUT-rthooks.c
clang-$V -fno-builtin -c -o $OUT-rthooks.o $OUT-rthooks.c
clang-$V -r -o $OUT-runtime.o $OUT-rt0.o $OUT-rthooks.o
clang-$V -no-pie -Wl,--gc-sections -o $OUT $OUT.o $OUT-runtime.o -L/usr/lib/llvm-$V/lib -lLLVM -Wl,-rpath,/usr/lib/llvm-$V/lib -Wl,-z,stack-size=536870912
ls -la $OUT
