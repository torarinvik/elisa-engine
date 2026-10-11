# Archived evidence: Earlier runtime and compiler checkpoints

Historical records moved from IMPLEMENTATION_EVIDENCE.md to keep the live release record readable. Preserve their original status and limitations.

- **Core runtime closure confirmed:** Stage1 builds its runtime from the vendored
  `elisacore_std`, packages that tree in installed snapshots, and checks the copy
  against Elisa-core's canonical runtime. Core commit `0076b655` syncs the five
  JSON parser grant rows already present in the current compiler checkout;
  `scripts/check_runtime_drift.sh` reports all 35 files in sync. Strict O0/O2
  validation and a refreshed 19-file grant inventory await the active compiler
  slot handoff, so the historical 546-warning count is stale.
- **Compiler baseline and incremental candidate — 2026-10-09:** the matched
  Stage0 explicit O0 `-emit check` of source revision
  `7e23b29746826c96c39a1e693c807e3648aa3714` completed with exit code 0. The
  compiler SHA256 is
  `d0a9d62f6147bc2cfb6ab9f2ad0fbf844508c42dc7f2e9941509d9d1f496536d`; total
  wall time was 2,169.739 seconds, user CPU 2,193.822 seconds, and peak RSS
  1,626,628 KiB. The `check_mutable_global_authority` phase consumed
  2,007,040.4 ms wall / 2,006,864.9 ms CPU. No object hash is expected for the
  check-only request. The exact identity, metrics, phase events and compiler log
  are on the remote compiler host under
  `/root/work/stage1-perf-20261009/Elisa-compiler/build/semantic-check-baseline-20261009/`.
  This closes the matched baseline run and confirms the authority phase is the
  main observed cost; it does not demonstrate the candidate speedup.
- **Incremental compiler candidate — incomplete (2026-10-09):** the
  source-fingerprinted combined O0 regression emitted a 17,117,280-byte object
  in 213.813 seconds. Its first runner did not link: a manually added runtime
  object duplicated symbols already in the test object, and an `R_X86_64_32`
  relocation was rejected by the default PIE link. Separate owner-index,
  callback-origin and snapshot-codec assertions pass. The malformed
  declaration-surface test exposed a fail-open cache case in which the final
  function remained cacheable. The initial invalidation-reset fix did not make
  the collector assertion pass. Helper-level record-array update and
  truncated-source lexer checks were reported passing, but a later collector
  fixture failed to link on unresolved callback-support symbols before its
  assertions ran. The subsequent r4 object emitted 17,131,864 bytes and its
  runner panicked at
  `test/parity/.semantic_index_codec_combined_smoke.elisa:355:43`, the
  `bodies_rejected == 2` assertion because `hot = 3` was conservatively
  rejected as unsupported. The r5 fix recognizes that literal local binding
  while leaving shadowed calls and mutable aliases on the fresh-check path.
  Its combined native fixture compiles, links and exits 0 across four focused
  suites: owner-index, callback-origin, snapshot-codec and function-fact
  collector. The malformed-surface control reports all four function records
  as `InvalidTokenSpan` and uncacheable, with zero direct facts. The r5 source
  manifest is `/tmp/semantic_index_codec_combined_o0_r5.manifest`; its test
  source SHA256 is `bfa38d1b9103b31809caa6e56cf7ae46b4a8df87ef803f93add2ba62d98b91fd`,
  implementation SHA256 is
  `5a9168086e10bc04edc6fa66754db4fcffcb6d1040d1825134ae91cfc7813535`,
  O0 object SHA256 is
  `309f083dd25bf9ed2ca3e14e09deeb79618e2ea4fa5d37bb7a8b3b38b7c24ed3`,
  and runner SHA256 is
  `ad890d70091147ef5dd7bc4eab8246cde4f3b54ad9fce63838f7a0d77dc4c1ce`.
  This qualifies the focused controls only; the prototype is not production
  wired, covers a small fraction of compiler functions, and has no measured
  performance gain or clean-build parity. Separate disk-persistence
  qualification passed 15 actual-process cases covering short reads/writes,
  corruption, oversized files and failed-write cleanup. The combined standard-
  library edits caught two syntax errors; both are fixed. The current combined
  runner compiled and linked, then exposed a test expectation error: after cache
  invalidation the original permission-annotation scan correctly returned true,
  while the assertion expected false. The assertion now checks agreement with
  the original scan; rerun evidence is pending. Shared-checker, trace and
  grant-source controls plus production
  fact replay remain pending; reuse is disabled. The current candidate direction is to reuse
  authority-checker facts rather than duplicate grant rules in a second
  analyzer. Earlier r2 manifest/logs are
  `/tmp/semantic_index_codec_combined_o0_r2.manifest`,
  `/tmp/semantic_index_codec_combined_o0_r2.log`, and
  `/tmp/semantic_index_codec_combined_link.log`; the r4 manifest and run logs
  are `/tmp/semantic_index_codec_combined_o0_r4.manifest`,
  `/tmp/semantic_index_codec_combined_run_r4.log`, and
  `/tmp/semantic_index_codec_combined_run_r4_unbuffered.log` on the compiler
  host.
- **Matched authority-check optimization — cold result qualified, reuse not
  measured:** a same-source O0 `-emit check` completed with exit 0 before and
  after the shared permission-parameter cache change. Both runs use the exact
  source-tree SHA256 `62ff41c43299e3afeda49001be8765afc724da95ac0a6cd1e77bbc9ad6962357`,
  build-recipe SHA256
  `a0a7f5b8753dc01e202828a80b16508facf840aa6ceea7ea2cec0fc597ba9ba5`, LLVM
  21.1.8, the same 32-job check command and runtime object SHA256
  `d2cbe85c8190dacf5b9f112d52009961cc24ce65ced5c715e198131e2f7fe6e5`. The
  baseline compiler SHA256 is
  `d0a9d62f6147bc2cfb6ab9f2ad0fbf844508c42dc7f2e9941509d9d1f496536d`; it
  completed in 2,169.739s wall / 2,193.822s CPU and peaked at 1,626,628 KiB
  RSS. The optimized compiler SHA256 is
  `6b3ea99d713025de64687eb9df67cc74e2b28ddd64b8cbaba816406c479256ee`; it
  completed in 800.425s wall / 827.244s CPU and peaked at 1,627,364 KiB RSS.
  That is a 63.1% wall-time and 62.3% CPU-time reduction with effectively flat
  peak memory. Its check log records inference at 309.109s, authority report
  traversal at 321.023s, read/write closure at 3ms, effect-row closure at
  12.322s and permission-parameter-cache work at 322ms. No object was requested;
  the run proves a cold semantic-check gain only, not incremental reuse. The
  prior 32m01s comparison is superseded. Baseline and optimized identities,
  metrics and logs are under
  `/root/work/stage1-perf-20261009/Elisa-compiler/build/semantic-check-baseline-20261009/`
  and
  `/root/work/stage1-perf-20261009/Elisa-compiler/build/candidate-stage1-r12-evidence-20261009/same-input-semantic-check-only-20261009/`.
- **Paired full-LLVM timing — inconclusive:** the same-source first pair was
  main 149s and candidate 156s. Across two paired runs, the candidate averages
  158.7s and main 155.7s, about 1.9% slower with substantial run-to-run
  variation. This does not establish a slowdown or satisfy the no-slowdown
  promotion gate. The emitted-IR comparison found one candidate-only carrier on
  `arena_dict_new__i64`, which takes an explicit arena. The owner traced this to
  treating any hidden generic ABI slot as a local result region and narrowed
  insertion to true caller-result slots. Explicit-arena and growable-reference
  controls pass; full qualification gates remain pending. Details are below.
- **Earlier ABI carrier experiment — superseded by the candidate below:** the fixture
  forces nested growable worker results through reallocation under ASan,
  confirms scalar generic calls need no region carrier, and confirms
  region-backed generic results retain one. At that snapshot, one additional
  generic ABI case was still being checked and a separate matched-check driver
  failed because wrappers put returned request buffers in short-lived regions.
  That old wrapper failure is superseded by the successful same-source cold
  semantic-check result above; it does not close the carrier-count, matched
  runtime, parity, or consumer gates.
- **Revised auto-region carrier candidate — focused controls pass:** the source
  now adds a hidden result carrier only when the selected generic declaration
  needs a caller-result slot; hidden arenas for grown reference parameters
  remain owned by their argument. Controls cover explicit-Arena-backed results
  and scalar generic mutation through a growable reference. The final source
  rebuilt from the current Stage0 seed with matching Stage1/runtime provenance;
  O0, O2 and ASan checks pass. The explicit-arena dictionary result and
  caller-owned growable array do not create a second local arena. The
  implementation and focused regressions are committed together as
  `ae59c06b`; its source-bearing commit also contains the focused diagnostic
  regressions. The earlier test-only commit `c588e96c` is no longer an ancestor
  of the active branch. Candidate Stage1 SHA256 is
  `4cb618414f706d64fd988d1d1dbdb502c056ba847ee0d040e526af38dab214b5`; runtime
  object SHA256 is
  `b3b6df516e81d96369549e0ddfd39edf132f6be50ff1835c7edf5921d792381a`.
  Revised self-host Stages A-D pass: Stage A 5/5; Gen2 emitted a
  20,813,872-byte Gen3; Gen3 and Gen4 object SHA256 values both equal
  `4292d9d5d340b5e79f8911b57412d7fc95dc5360181384b5bdcbf1049c09b639`; all 40
  repeated emissions match. The initial Gen2 attempt exceeded its 600-second
  timeout and was preserved; the extended rerun passed. Broad parity,
  carrier-count and matched-runtime gates remain pending. The exact candidate's
  backend-native smoke passes 567/567 compile-and-run controls at
  `build/result-slot-predicate-r2/backend_native_smoke-r2.log`. Its 37-check fast
  profile is running serially: source, annotated-list, C-bind and dependency
  parity have passed; AST parity reports 28 differences. Comparing the named
  failures with the current-main fast-gate log gives the same exact 28-fixture
  set. The baseline is from the clean `b11e9121` main-qualification worktree;
  its retained Stage1 product hashes to `a871c1d8…`. The gate log does not embed
  that product hash. Although it names the same Stage0 path as the candidate
  run, that Stage0 binary was rebuilt after the main log was written, so its
  test-time hash is unknown. Treat the AST result as a worktree-log comparison,
  not a product-matched result. The
  documentation parity passed in 2,064s: 405 of 733 comparable fixtures were
  byte-identical, exceeding the ratchet of 159. Format parity also passed in
  1,472s: 397 fixtures were byte-identical, exceeding the ratchet of 173. The
  header parity completed in 1,776s and exits 1: 353 fixtures were byte-identical,
  34 diverged, 515 were skipped and 186 were safe Stage1-only skeletons. The
  candidate log is `build/result-slot-predicate-r2/fast-gate-checks/emit_header_parity_smoke.sh.log`.
  Its exact 34 failed names and 186 safe-superset names match
  `build/current-main-emit-header.log`; both logs report the same totals. The
  older main log is timestamped 09:06, before the retained current-main Stage1
  product was written at 13:12 (SHA256 `a871c1d8…`), so the match is log-to-log
  and not product-identity-bound. Interface parity completed in 55s and exits 1
  with 61/333 fixture differences. The candidate and older
  `build/current-main-emit-iface.log` contain the same 61 failed fixture names
  and comparison details; only the Stage0 wrapper path differs. The current
  files at the old main Stage0 path and candidate Stage0 path hash to
  `1e34878d…` and `dd0974e2…`, respectively, despite both source trees being at
  commit `735118cb`. The candidate Stage0 was rebuilt after the main logs, and
  neither main log records its Stage0 or Stage1 product hash. Thus AST, header
  and interface are all worktree-log comparisons; rerun them on both products
  with one pinned, hashed Stage0. The fast profile has moved on to interpret
  parity. Stage B
  still needs a matched current-main comparison before any no-slowdown claim.
  The earlier `5be39b79`
  self-host result does not qualify this changed product. I am leaving the
  active branch untouched.
- **Concrete generic Store dispatch gap — focused fix passes:** the compiler
  suite found that `darray.store_get()` did not reach concrete generic protocol
  dispatch, while the equivalent call through `[S: Store]` did. The isolated
  Core fix passes focused semantic tests, including a check that a signature
  grant cannot authorize the function body. Positive native assertions pass.
  The out-of-range control now passes with unbuffered output and asserts both
  `SIGABRT` and the exact panic message; the positive suite was independently
  rerun. On the separate updated Stage1 binary SHA256
  `6b3ea99d713025de64687eb9df67cc74e2b28ddd64b8cbaba816406c479256ee`, the
  full semantic check passed in 14m50s, but the native gate stopped when two
  generic `Store` calls failed emission: `test_store_total_lookup_valid@1198`
  and `store_total_lookup_invalid_entry@1207` both instantiate
  `incremental_store_test_get[darray[i32]]` and decline at the `store_get`
  call. The log is
  `build/incremental-tests/semantic-index-combined-stage1-r12/logs/compile.log`.
  The compiler owner traced the decline to missing Stage1 substitution for
  associated types such as `S.Handle` and `S.Elem` while specializing the
  generic helper. A compiler repair is in progress. Broader Core/runtime
  acceptance remains blocked on qualifying that fix on the exact product; this
  correctness gate is separate from cold-check timing.
- **Incremental reuse priority:** the compiler owner is first targeting reuse
  of unchanged functions' semantic analysis, with affected caller recomputation
  and permission propagation. Object caching and broad parallel emission remain
  deferred until cold/warm diagnostics and inferred grants match. Current
  prototype work evaluates one semantic-option tuple for analysis and its future
  cache key, and maps expanded-source declarations back to original file paths.
  Controls for duplicate include resumes and invalid source maps have been
  added; they are not yet reported passing. Static-generated source remains
  ineligible until it has a matching source map. The r12 combined runner
  exposed an assertion expectation error: after invalidation, the original
  permission-annotation scan correctly returned true. The assertion now
  compares against that scan, but the corrected combined rerun remains pending.
  Direct replay has since been added for effect rows, read/write requirements,
  both dependency graphs and diagnostics. Its next native gate must compare
  those results with the existing checker. This remains prototype
  qualification, not clean-build parity or measured reuse. Production reuse is
  still disabled.
- **Current strict-effect regression — fresh-product targeted checks pass, wider
  promotion pending (2026-10-09):** the consumer owner traced a protocol-effect false positive
  to lowering: direct protocol parameters become synthetic generics such as
  `__protocol_arg_0`, with the bound in a generic side table. The compiler now
  resolves that exact bound and treats a method as pure only when the protocol
  declares it. The correction is committed as `5be39b79`; all three focused
  `distance_to` calls and 111 authority cases pass, and the void-poll regression
  passes at O0/O2 and ASan. Temporary tracing is removed and Stage0 semantic
  preflight passes. Compiler source HEAD is
  `5be39b79fff20fb78f1f996f885cef37b57e5514`; its Stage1 binary SHA256 is
  `0c1fbca28b883a1af227936c5117c9adafd87abb2e1874eed8b0ca409c200059`.
  Self-host Stages A-D pass: Stage A is 5/5, Gen2 emitted a 20,813,544-byte
  Gen3, Gen3/Gen4 are byte-identical (SHA256
  `f3bd017c1dfcfe70b13aaf88de6f73a8b4c623c290d514806c38175a428e9229`),
  and all 40 repeated emissions match. Stage B took about 13 minutes. The
  long Stage B time still needs a matched current-main comparison before a
  no-slowdown claim. The exact-product backend-native smoke passes 567/567
  compile-and-run checks. The exact-product fast gate exits 1 at
  `build/global-authority-qualification-5be39b79/fast_gate-r2.log`: AST parity
  differs in 28/579 fixtures; `resolve_smoke` has missing local Global grants;
  diagnostics diff reports 457 changed or missing diagnostics; semantic-
  internal diff has 63/3441 mismatches above baseline 54; semantic-acceptance
  diff has 4/587 disagreements. The matched main comparison reports the same
  AST, resolve and semantic mismatch sets and diagnostic identities, except for
  one candidate-only difference in
  `test/fixtures/diagnostics/fn_value_effect_row_arg.pos.elisa`: the candidate
  reports an argument-effect mismatch for `grow` (`can[Memory.Allocate]`) passed
  to an unannotated function parameter, while main emits only the memory-growth
  warning. A narrow regression now checks that passing the effectful function
  value does not produce a call-site warning while invoking it still propagates
  Memory; both focused diagnostic controls pass. The full fast gate and
  candidate/main comparison need rerunning after this change. The targeted
  `global_permissions_smoke` passes for local grants, propagation, trusted
  scopes, shadowing and grouped effects. Keep promotion blocked until the
  remaining difference is resolved or precisely qualified.
  Carrier-count and matched runtime comparisons remain pending. Proof and
  consumer acceptance still require the promoted exact tuple. The prior
  installed `1505c598` gate remains historical evidence for its exact tuple.
- **Newest Stage1 default-grant controls — focused semantics pass; full native
  qualification pending:** Stage1 binary SHA256
  `6b3ea99d713025de64687eb9df67cc74e2b28ddd64b8cbaba816406c479256ee` passes
  focused controls for local `Global.Read` and `Global.Write` grants, caller
  propagation, signature-only grants not authorizing bodies, mutable-global
  field writes, aliased writes, and mutable-global borrows requiring
  `Global.Write`. Negative controls reject missing local or caller grants;
  `trusted` continues to suppress the Unsafe requirement without suppressing
  Global authority. The `-permissive` CLI bypass controls also remain separate
  from strict acceptance. The full semantic check passed in 14m50s, but the
  native gate then stopped because the test helper's two generic `Store` calls
  were declined at `store_get` in `test_store_total_lookup_valid@1198` and
  `store_total_lookup_invalid_entry@1207`. The compiler owner is tracing this
  dispatch failure; it is a correctness blocker, not evidence of a performance
  regression. The compiler also reports substantial advisory effect-grant
  debt; that debt is not conflated with the focused mandatory Global-grant
  contract. This product does not yet qualify full native, Stage0/Stage1
  parity, proof, or performance gates.
- **Qualified baseline:** compiler `52d60fcf` and matching runtime pass 215
  uncached runtime tests and the shared gate. Production prover `f593c886`
  retains all 73 engine reports / 4,246 obligations; its full matrix failed.
- **Latest isolated proof repair:** clean prover `5c40d273`, paired generation
  `65851d44058c49fca2e0d06974214022`, retains all 73 uncached engine reports /
  4,277 obligations with independent replay and zero diagnostics, gaps or trusted
  assumptions. Original strict-order, signed-constant and return-branch CLI
  regressions pass. Full compatibility remains open; production is unchanged.
- **Current prover blocker:** clean `10ae0267` and immutable compiler
  `fb8e0927` build paired generation `649d72a317c342dba3607ca7e77493b8`
  in 64.92 seconds. Five original CLI regressions pass. Engine inventory fails:
  `proof/action_input_context.elisa` alone exceeds the unchanged 3 GiB cap in
  0.31 seconds. The historical pair processes the same source at 52,416 KiB
  with 265/265 proved and replayed; this comparison diagnoses a regression and
  does not qualify the new implementation. Minimize and separate prover-source
  changes from compiler code generation/runtime before another full run.
  Subsequent fixed-source/frontend/runtime O0 comparison isolates emitted-code
  behavior: current emitter peaks at 757,284,864 bytes versus 17,203,200 bytes
  historically on a self-contained 25-obligation case. Recent prover changes and
  a borrowed-array hypothesis are ruled out; 90,033 allocations of 8 KiB dominate
  current cumulative allocation. Repair the owning compiler's allocation/lifetime
  path, then repeat the original engine input and inventory; mixed historical
  products are diagnostic only. Frame-pointer attribution now identifies
  `proof_kernel_replay_expr_equal`'s 32-byte-pair worklist: its 256-element
  literal allocates 8 KiB directly in the static arena. The method-only repair
  `220ae3f9` passes native O0/O2 controls but leaves the original memory result
  unchanged. Lexical receiver typing for builtin `darray.pop`/`push` was the
  causal follow-up, preserving conservative treatment of custom, generic,
  ambiguous and unknown calls plus genuine global publication. Typed builtin
  repair `d69f219b` and custom-pop dispatch correction `e791ec50` now pass semantic
  preflight and official seed provenance. Three native ownership controls pass at
  O0/O2, including 100,000 worklists, real global publication and retained custom
  receiver storage. The unchanged 25-obligation checker/replay harness peaks at
  16,089,088 bytes versus 757,284,864 before, with source/frontend/runtime/O0 fixed.
  This proves the causal improvement on that diagnostic harness; it emits no full
  normalized JSON, so exact report-byte equality is not established. Current paired
  prover build, original engine input and full inventory still require acceptance.
  Preserve explicit compile-slot handoffs
  ([evidence](../elisa-engine-proof-counterexample/docs/validation/dispatcher-budget-repair.md)).
- **Compatibility throughput:** retention/provenance repairs are committed in
  isolated prover `4d9f3a8d`. A diagnostic census covered 1,174 inputs with nine
  timeouts, but used mutable compiler inputs. Snapshot-backed census/matrix
  acceptance waits for the first-input memory repair; retain every input and
  original budget.
- **Strict-negative repair:** isolated prover `5077910c` evaluates exact,
  non-wrapping unsigned subtraction at widths 8/16/32 for diagnostic
  counterexamples. Focused controls pass, including overloaded-operator refusal;
  the original return-branch fixture replays 23/23 and its strict negative is
  disproved. Clean paired build and original return-branch CLI acceptance pass; full
  compatibility remains open.
- **Consumer mesh bounds:** skin/draw source guards are implemented and pass
  O0/O2 malformed-input controls, focused AddressSanitizer and the existing GLB
  loader regression. All 73 engine reports now independently replay 4,277
  obligations; the original 4,246 are retained. Actual mocap consumer integration
  remains open ([evidence](docs/validation/mesh-overlay-shapes.md)).
- **Native release blocker:** capture and Character Course relaunch have focused
  repairs, but effect lifecycle memory remains intermittently above the original
  8 MiB allowance after the full renderer sequence. Heap/GPU and VM-domain
  diagnostics are present; opt-in region-tag observation now has focused mapping
  and overflow controls ([region evidence](docs/validation/effect-memory-vm-regions.md)).
  No root cause or leak repair is established. Preserve
  the failed full-gate evidence ([native evidence](docs/validation/counted-fill-memory-growth.md)).
- **Bounded package relocation:** the physics-interactables native self-test
  packages and launches twice from a relocated macOS bundle with source and
  Homebrew access denied. New `package-provenance.json` records the package
  manifest SHA256 separately from binary build provenance, the allowlist and
  shader settings, and hashes/sizes for all other bundle payload files. The
  existing saved executable uses compiler Stage1 `5888942e…` and runtime object
  `013d3174…`; this is not a fresh build on the current candidate tuple. Focused
  packaging tests and the 600-line source gate pass. Interactive gameplay,
  fresh author/cook/build, signing/legal, clean-machine, and full Q02/Q07
  acceptance remain open ([evidence](docs/validation/physics-interactables-package.md)).
