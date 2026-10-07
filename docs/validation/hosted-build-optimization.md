# Hosted build optimization — 2026-10-07

While rebuilding the updated Character Course, the command log showed that
`elisa_build_run.py --optimize` passed `-O2` only to the native C++ link. Its
Elisa hosted archive command had no optimization flag. Earlier records that
said native `-O2` remain accurate; they do not establish Elisa `-O2`.

`compile_archive` now receives the same resolved optimization choice as the
native link, including the existing `ELISA_NATIVE_OPTIMIZE=1` environment path.
It passes `-O2` on request and still sets `ELISA_RUNTIME_OBJ=none`, so the engine
linker owns the one runtime object. Console builds already forwarded `-O2`.
CLI help now states that both Elisa and native code are optimized.

`/opt/homebrew/bin/python3.14 scripts/test_elisa_build_run.py` passes 29 tests.
The new regression verifies optimized/default archive commands and separate
runtime ownership. Full CLI cases retain failure atomicity and cooking checks.
Log: `build/validation/build-run-optimization-tests.log`. Source-length and
module-hygiene checks plus diff whitespace pass. This Python command forwarding
change adds no Elisa runtime policy; it is checked by command assertions rather
than an implementation-linked Elisa proof. Compiler optimization correctness
and actual packaged runtime behavior require the subsequent real build/run.
