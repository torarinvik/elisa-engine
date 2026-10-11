# Archived evidence: Grant, proof and Studio integration checkpoints

Historical records moved from IMPLEMENTATION_EVIDENCE.md to keep the live release record readable. Preserve their original status and limitations.

- **Compiler policy — default grants and exact callee attribution are promoted; integration acceptance remains open:**
  Compiler `42fd1cbe` enforces `Global.Read` / `Global.Write` by default, with
  `-permissive` as the explicit bypass. Its installed Stage1 is freshness-checked;
  Stage2 self-host, 103 authority cases, Stage0/Stage1 parity, grouped-effect,
  CLI and strict-Unsafe controls pass. The broader Core fast suite still needs
  fixture grants and updated diagnostic expectations. Product hashes and the
  remaining qualification limits are in
  [global-grant evidence](docs/validation/global-mutable-grants.md).
  Exact callee identity fixes the previous same-leaf false positives. Keep
  grants narrow: do not adopt the old four Character Course or 71-row Studio
  provisional censuses. The 26 engine sample/test caller updates and project
  checks for world, save/swap, maze, audio, input and UI are recorded in the same
  evidence. On installed Stage1 `1505c598…`, the previously inventoried 89
  additional native, probe and example entrypoints outside the 216-test
  manifest now pass strict runtime-wrapped `-emit check` (89/89); the full
  uncached 216-test gate also passes after the caller repairs. This qualifies
  that installed baseline's engine consumers. Recheck on the newer compiler
  after qualification; proof integration, the inferred-row reporter, and fresh
  linked Course/Studio consumers remain open. Exact source hashes and results
  are in [the grant validation note](docs/validation/global-grant-entrypoint-census-1505c598.md).
  Consumer commit `1e98f075` resamples multi-key FBX tracks on a shared grid,
  and the bounded bridge regression passes with strict default grants. An old
  open Studio instance replayed its loaded 337-frame take through frame 248
  (~74%) without a crash, but the bundle predates the skin-cache fix and the
  installed compiler. On 2026-10-09, the consumer's native converter produced a
  fresh GLB from the high-block FBX and verified the original FBX bytes stayed
  unchanged; the current headless playback harness sampled, skinned and drew
  the full animation in three views without crashing. The current
  `build/MocapStudio.app` was relaunched, but GUI file selection/playback, exact
  bundle provenance and the separate 337-frame take remain unverified. This
  headless result is diagnostic evidence only. Verify full GUI playback,
  repeated redraw, malformed-input refusal and observer reveal on the exact
  current compiler/engine/UI tuple before closing consumer acceptance.
  Preserve exact callee, callback, default-argument, shadowing, and
  profiler/host-callback controls.
- **Character Course grant adoption on compiler `4655dbaa`:** the compiler tree
  used for that qualification had a provenance-checked Stage1/runtime pair.
  Its strict project-context check exposed 265 `Global.Read/Write` findings;
  local grant scopes now cover the affected course/runtime calls, with explicit
  returns preserving tail values. All five public course entrypoints pass
  strict `-emit check` with zero diagnostics. At that checkpoint, this was
  semantic evidence only; the course app had not been freshly linked or run. See
  [exact candidate and check records](docs/validation/global-grants-character-course-4655dbaa.md).
- **Previous 4655dbaa engine grant gate:** installed Stage1
  `cfcdc1fa99752f417c4267f125ed3419be26e569115fb28df3027095cb7e1da2` and
  runtime `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`
  match the provenance-checked compiler source tree. All five Character Course
  entrypoints pass strict semantic checking, and the uncached engine gate passes
  all 216 tests (0 cached, 0 remote, 36 seconds). The separate Maze C-ABI
  embedding probe also passes on this unchanged pair, including archive
  emission, native linking, session/gameplay exports and live SDL input. This
  closed those engine gates on the previous tuple; proof-pair qualification and
  fresh Course/Studio native consumer acceptance remain open. Full command and
  identity are recorded in
  [the 4655dbaa record](docs/validation/global-mutable-grants.md#current-4655dbaa-engine-gate).
- **Current b11e9121 Global-grant gate:** installed Stage1
  `1505c598a71e76d0d7f1a201cdf458320960d9f024531eca2c4bff19f5c08c24` and
  runtime `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`
  match the installed provenance record for source tree
  `40b306621d1e1aa7cdd68a73179360ba1b6b9ba61aae2d491cd3087d4b819fff`. The
  compiler's direct CLI smoke passes default denials, exact grants and
  `-permissive` bypasses. All five Character Course entrypoints pass strict
  checks, the uncached engine gate passes 216/216 in 27 seconds, and the
  separate C-ABI embedding probe passes, including live SDL input. The
  compiler's inferred-row parity helper still has a report-format mismatch;
  proof-pair and fresh Course/Studio app acceptance remain open. See
  [current grant validation](docs/validation/global-mutable-grants.md#current-b11e9121-engine-gate).
- **Existing Maze consumer grant/package acceptance:** The headless game route,
  native SDL3/Metal app build and scripted run, and all nine relocated-package
  controls pass on the saved `b11e9121` Stage1 product
  `5888942e…` with runtime `013d3174…`. The consumer now declares and scopes
  the required `Global.Read/Write` grants through world construction and native
  runtime calls. Executable and bundle hashes, commands, Python runtime and
  remaining limits are in
  [the Maze acceptance record](docs/validation/maze-global-grants.md).
- **Public example grant adoption:** the environmental-effects scene and public
  entrypoint, physics-interactables runtime and both entrypoints, including its
  hidden self-test, now declare and locally scope the default global grants.
  The changed entrypoints pass strict semantic checks on saved `b11e9121`
  Stage1 `5888942e…` with runtime `013d3174…`. Environmental-effects passes its
  native SDL3/Metal visual smoke, changing 2,580/2,304,000 pixels; the physics
  hidden native self-test builds and exits 0. Exact binaries, hashes, commands
  and limits are in [the example grant record](docs/validation/global-grant-public-examples.md).
  The 89 additional runtime-wrapped engine entrypoints now pass on the installed
  baseline; proof integration, fresh Course/Studio builds and rechecking on the
  newer compiler candidate remain open.
- **Engine grant regression guard:** `scripts/run_tests.py` now performs two
  identity-pinned preflights before compiling engine tests: the direct
  default-grant/`-permissive` controls and strict runtime-wrapped compilation of
  every native, probe and example entrypoint outside the gate manifest. On
  installed Stage1 `1505c598a71e76d0d7f1a201cdf458320960d9f024531eca2c4bff19f5c08c24`,
  they pass 42/42 CLI controls and 89/89 strict entrypoint checks. Reports are
  `build/validation/global-grant-cli-qualification-1505c598a71e.json` and
  `build/validation/global-grant-entrypoint-qualification-1505c598.json`.
  Twenty Python controls cover inventory discovery, wrapper construction,
  exact product pinning, source and recursive include stability, denial
  matching, and aborting before engine work on failure. With both preflights enabled, the normal runner passes
  all 216 tests in an uncached 73-second run; the focused `anim-state`
  executable also builds and passes. The portable `cook` workflow runs the
  grant-harness and hosted-toolchain policy suites on Windows, macOS and Linux;
  the current local runs pass 20/20 and 17/17, and the workflow YAML parses.
  Lock schema controls reject malformed JSON types without permitting an
  invalid eligibility state. The hosted lock records all four verified `main`
  heads, rejects a compiler pin that differs from its recorded head, and the
  plan's optional online verification detects upstream drift. The current
  four-ref verification passes; hosted Actions results have not yet been
  observed.
- **New compiler grant check — partial:** compiler source `7e23b297` installed
  Linux Stage1 `1dfd59a9a4b4d1c2ff07a9ac79d0d604cd4fed6e410b5f1941c019f4be7cbf68`
  with runtime `ec97692e90583cb64aab3c0c0724daf5c6441373e3712d7cf23681670ca3602c`.
  On this exact product, the 42 CLI controls and all 89 strict runtime-wrapped
  engine entrypoints pass; reports are
  `build/validation/global-grant-cli-qualification-1dfd59a9.json` and
  `build/validation/global-grant-entrypoint-qualification-1dfd59a9.json`.
  The compiler's Gen3/Gen4 objects are byte-identical, and 40/40 repeated
  emissions match. Promotion remains blocked: the final JSON API O0/O2 check
  audits 14 transitive standard-library modules and finds 884 missing local
  grants across 250 functions. Per-function repairs and the rerun are in
  progress. The candidate has not passed the full engine runtime suite.
- **Local callback-branch engine gate:** compiler source `c3fc94b1a3d6c525ba52a455fb8acd9ef81de2cd`
  builds Stage1 `1621d550fdd2efc988662c563601e7cab50c23caea7fefe45c7b57e254bf6da3`
  with runtime `de964b7e5b764337b9332534627e16bc4dc181037daba62c2e40d1392502512f`;
  its provenance check passes. The uncached command
  `python3 scripts/run_tests.py /private/tmp/Elisa-compiler-fbx-latest/bin/elisac-stage1 -j 2 --no-cache`
  passes 42/42 CLI controls, 89/89 strict entrypoint checks, and all 216 engine
  tests in 68 seconds. This is a separate tuple from compiler source
  `7e23b297`; that performance candidate's standard-library audit remains open.
- **Earlier callback-region repair — separately open:** compiler worktree branch
  `codex/fbx-worker-region-latest` is rebased at `b719dbd5` over installed
  upstream `b11e9121`. Its fresh Stage1 product
  `9acf976ee979bec4ad4819b724ba02b3f9d53f7bc6c8d4fde1e01156b79bd6e5`, runtime
  `de964b7e5b764337b9332534627e16bc4dc181037daba62c2e40d1392502512f`, and
  source-tree SHA256
  `77167812ecd7112b81436c6d7590959b3b4de473ad52ae89966b91b22dc3abf8` pass the
  installed provenance check. The proof rebuild against this tuple failed on
  missing default `Global.Read/Write` annotations in proof/compiler sources;
  it did not produce a qualified proof binary. A fresh O0 diagnostic bundle was
  built with engine source `1e98f075`, a dirty UI worktree, and executable SHA256
  `51bda2b78342a27e550727f53222356193b5ff56e2158941a2f47e8a342cb9f9`. Its
  sealed input record matches compiler `b719dbd5` and runtime
  `de964b7e…`. The bundle opened to “Workspace available,” but this does not
  establish playback acceptance. Later matching-bundle crash reports fault in
  `Studio.clip.rehome` while drawing; see the separate current-crash finding
  below. The O2 build was stopped after more than 13 minutes in LLVM AArch64
  DAGCombiner without an object. Focused UAF/scalar-carrier regressions and
  callback-path consumer qualification remain open.
- **Current Studio global-rehome failure — compiler regression in progress:** the Oct 9
  reports `MocapStudio-2026-10-09-062450.ips` and
  `MocapStudio-2026-10-09-063522.ips` both carry executable UUID
  `5CCF2C5E…`, matching the current diagnostic bundle. Both fault at
  `Studio.clip.rehome+1160` while the view is drawing. The consumer owner traced
  the invalid imported-GLB document byte pointer to
  `src/studio/app/app_edit_transactions.elisa::publish_candidate`: it moves an
  affine `StudioModel::Clip` into global `clip`, but the generated assignment
  appears to omit global rehome while the candidate's backing buffers belong to
  a temporary caller arena. The owner has since added a candidate fix in the
  active compiler worktree: it detects owning by-value parameters stored in
  globals even when the callee allocates nothing. The focused fixture exercises
  an owning optional aggregate crossing a short-lived caller region. The owner
  committed the compiler change as `806772c7`; its rebuilt Stage1 SHA256 is
  `a8eb8b5947b4941dd723a3381483c4c3fdafa986a7170dd8f4109708d9d2c391`, with
  matching runtime SHA256
  `de964b7e5b764337b9332534627e16bc4dc181037daba62c2e40d1392502512f`. The
  exact command `bash test/parity/global_store_auto_region_smoke.sh` now passes:
  its optional owning aggregate survives the caller-region close at O0, O2 and
  under AddressSanitizer. This qualifies the focused compiler regression only;
  it does not qualify large nested global rehomes. The preserved
  `nested-global-rehome.elisa` stress fixture in
  `/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/mocap-cleaner/build/studio-runtime-e24-20261008/exact-o0-llvm-20261008/`
  stores and repeatedly redraws 512 `Point` values through an optional global
  `darray`. Its O0, O2 and ASan compile steps succeed, but all three recorded
  executions exit 1; a final O0 rerun also exits 1. The retained source SHA256
  is `cd85877fb1bd55d84404ba0056b6d6bc97e1c09fe616018b36614cff175895c3`, and
  the source-closure record identifies compiler product
  `607f7168c759381f14b467af93c74997181e3dce600a8c7de7d05f5410aca9c6`.
  This older stress run is not evidence about the current candidate, and it
  contains no GLB document; require the current fix to replay it before the
  consumer crash can be treated as resolved. The Studio app has not yet been
  rebuilt or retested. The crash-report bundle
  was compiled with earlier compiler revision `b719dbd5`, so the reports do not
  yet test the `806772c7` fix; rebuild the same app after compiler qualification.
  An earlier version of the compiler patch also passed the native backend smoke
  (567/567 checks). The tightened source's fast profile selects 37 of 591
  total checks and is still active and unqualified. Against the fetched Stage0 oracle,
  `emit_ast_parity_smoke.sh` reports 28/579 differences,
  `emit_iface_parity_smoke.sh` 61/333, interpretation 17 divergences (781
  skipped), and header parity 34 divergences (515 skipped). Test-run parity has
  11 identical cases and one divergence; packed output has 362 exact results,
  two divergences and 33 Stage1 rejections. Real-slice parity fails 0/5 with an
  invalid-call diagnostic and a missing arena-cache-lock-release symbol. Format
  parity passed after 2,490 seconds and doc parity after 3,404 seconds; header
  parity failed after 2,992 seconds. These failures
  have not yet been compared with the exact parent-baseline Stage1, so regression
  status is unknown. The merged Stage1→Gen2 self-host build has since completed,
  and Gen2 passes five regression probes. Gen2 then compiled the full merged source into a
  25,669,472-byte Gen3 object in about 19 minutes. Gen3 is being linked; the
  byte-identical Gen3→Gen4 comparison and 40-run determinism follow, while
  merged-head JSON O0/O2, parity resolution, and the Studio app rebuild remain
  pending. The current log is
  `/private/tmp/Elisa-compiler-fbx-latest/build/global-store-fast-gate.log`.
  Earlier 567/567 native checks do not qualify this tightened build.
  The current chooser leaves Open disabled with the FBX selected, so playback
  could not be repeated through that flow. This compiler/global-rehome
  investigation is separate from the earlier `path_copy` hidden-region failure.
- **Current compiler/Studio handoff update — fresh-product targeted checks pass,
  promotion pending:** the consumer owner fetched compiler upstream and confirmed
  the FBX repair branch includes `main` at `b11e9121`. The protocol-dispatch
  correction is committed as `5be39b79`. The focused collision smoke passes
  all three `distance_to` calls; the 111-case authority suite and void-poll
  regression at O0/O2 and ASan also pass. Temporary traces are removed and
  Stage0 semantic preflight passes. Self-host Stage A passes 5/5 and produced
  Gen2. The Stage1 binary SHA256 is
  `0c1fbca28b883a1af227936c5117c9adafd87abb2e1874eed8b0ca409c200059` from
  source HEAD `5be39b79fff20fb78f1f996f885cef37b57e5514`. Self-host Stages A-D
  pass: Stage A is 5/5, Gen2 emitted a 20,813,544-byte Gen3, Gen3/Gen4 are
  byte-identical (SHA256
  `f3bd017c1dfcfe70b13aaf88de6f73a8b4c623c290d514806c38175a428e9229`),
  and all 40 repeated emissions match. Stage B took about 13 minutes; its
  runtime needs a matched current-main comparison. The exact-product backend-
  native smoke passes 567/567 compile-and-run checks. The fast-gate matrix is
  running at `build/global-authority-qualification-5be39b79/fast_gate.log`;
  carrier-count and matched runtime comparisons remain pending. No same-bundle
  Studio retry or GUI playback result is available; keep the two ownership
  failures as separate gates.
- **Studio fixture grant follow-up — focused pass, final tuple pending:** the
  consumer owner traced two fixture build failures to missing local
  `Global.Read/Write` grants around scene/model calls. Commit `a21c2e75`
  updates `mocap-cleaner/test/studio_capture.elisa` and
  `mocap-cleaner/test/studio_correction.elisa`; all seven focused Studio
  fixtures pass with the r3 compiler product. The focused protocol-effect fix
  now passes its collision smoke, but it arrived after that product; repeat
  these controls on the refreshed source-matched compiler before promotion.
  Full app rebuild and playback remain open.
- **Renderer lifecycle diagnosis — prepared, not run:** engine `fa451328` has
  1,186 revalidated input hashes and unchanged compiler/runtime and
  Wicked/Jolt/SDL3 identities. `scripts/run_process_budget.py` enforces an
  aggregate 3 GiB process-group RSS cap and 180-second timeout; all six focused
  controls pass, including descendant termination and monitor failure cleanup.
  The exact native command, log path, and watchdog report path are in the
  ignored `build/validation/effect-memory-vm-regions-next-run-preparation.json`.
  The run is held until the active Studio/UI tasks release the native slot.
- **Earlier Studio compiler integration record — app/backend acceptance remains open:**
  after the 54-diagnostic baseline in
  `mocap-cleaner/build/studio-semantic-after-global-scopes-2.elisa.log`, the
  consumer owner reports the latest strict semantic check passed with zero
  diagnostics. Studio source fixes
  are committed at `mocap-cleaner` `b79608a7`; the matching AppKit/UI effect and
  protocol conformance changes are committed at `elisa-ui` `65f370f3`. Compiler
  source was `34574304`; its verified Stage1 product hash was
  `f4c9d54c759f098b294c2146c28c372fa465da6e4cb7f2b48135cbe1e0f189c4`. That
  product is historical and must not be reused. The app build first correctly
  refused the existing proof binary because its frontend pin was `2a3dce66`.
  A later exploratory proof build reached compiler source and exposed missing
  strict `Global.Read/Write` grants. Its captured output was truncated; the
  visible diagnostics include `symbols.elisa`, `symbols_hash_index.elisa`,
  `parser_machine_states.elisa`, and additional compiler files, so the old
  “45 total / five hash-index” count is not a complete inventory. Compiler
  source has since advanced through `35479edc`, `f2b86c32`, `9d4ec255`, and
  `b8400426` (generic join results, local index shadowing, result-reader
  collisions, and empty global dynamic-array materialization). A clean detached
  snapshot at `19294e83` now has a verified Stage1/runtime pair: Stage1 SHA256
  `752fe51db92ed27d3ef133e0f3e94b757e98964727b5702b7e6af02c7925bec5`, runtime
  SHA256 `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`,
  with source provenance checked. Its 103-case mutable-global authority smoke
  and Stage0/Stage1 permission parity pass; the ordinary SDK package SHA256 is
  `d9664cd42f15cfafadd13bfbe5d110c3c24a6070860b18e27b25d3f4edda1d6e`. These
  checks qualify the compiler product and SDK package, not the proof pair or a
  Studio app. Keep local grants narrow, retain default strict
  enforcement, and use `-permissive` only for explicit bypass controls. The
  documented runtime-checks fallback also declines two backend
  declarations, so it does not qualify a prover. Historical build-only app
  compiles declined 25, then 17 constructs on compiler `35479edc`; those counts
  are superseded. The latest verified consumer check uses the `19294e83` Stage1
  (`752fe51d…`) and runtime (`013d3174…`): strict app semantics pass, while
  code generation declines six constructs—five call sites using the same
  fixed-size generic helper and the exported AppKit file-drop callback. The
  generic join regression still passes at O0/O2 for scalar and 32-byte results.
  At this `19294e83` checkpoint no Studio package had been produced or launched.
  A later `b719dbd5` O0 diagnostic bundle exists; its current crash evidence is
  recorded above and it is not accepted. The `ec41ca95` consumer snapshot passes
  compiler freshness, export-alias regression, native
  ABI/pin checks and strict semantic preflight according to its owner. Earlier O2
  attempts exceeded the 4 GiB and 6 GiB RSS guards; one 8 GiB retry was stopped
  after UI inputs changed. On the later stable 718-input snapshot, the 8 GiB
  guard stopped O2 after 65 minutes at 8.04 GiB. The owner retried that same
  snapshot with a 12 GiB guard under compiler `ec41ca95`; the run was stopped
  after 21 minutes when free swap fell to 360 MiB. It produced no object and no
  compiler diagnostic. A later O2 attempt on consumer compiler `9d2cf1b` was
  stopped after roughly 20 minutes in LLVM's AArch64 SelectionDAG/DAGCombiner
  and produced no object. The sealed O0 diagnostic app opens the file picker,
  then crashes importing the supplied high-block FBX: the packaged
  `load_fbx_import_job` wrapper passes source pointer `1` to a 9,232-byte copy.
  The caller passes the 9,232-byte `Job` through a function value. Normal
  declared functions use indirect aggregate parameters and hidden sret results
  at 1,024 bytes, but the function-value call emitter currently declares direct
  aggregate parameters. Compiler fix `37091274` now emits the indirect argument
  and sret conventions for large function-value calls, declines unsupported
  large-aggregate closures, and adds a native regression. The rebuilt Stage1
  provenance now points at that exact commit; its focused 1,024-byte callback
  test returns the expected value (process exit 42). I cherry-picked the
  source-and-test commit into the grant-adoption branch as `12120f6b`; that
  combined compiler product was subsequently freshly built and qualified, as
  recorded in the exact candidate section below.
  The mocap-cleaner owner rebuilt Studio against `37091274` to retry the
  original FBX import/playback path. The first O2 run stopped at the wrapper's
  4 GiB guard; a 12 GiB attempt first exposed an identity mismatch between the
  checkout compiler and installed Stage1. After correcting selection, exact
  Stage1/runtime checks passed, but O2 reached LLVM's AArch64 DAGCombiner at
  about 28.4 GB physical memory and was stopped without an object. Mocap-cleaner
  commit `0a39989b` adds `STUDIO_OPT_LEVEL`, keeping O2 as default. A sealed O0
  diagnostic app now builds against compiler SHA256
  `ad0e16c9eddea0132a6ffee252f3dab4a3008dd805c042e1cb01cd48791a78f1` and
  matching runtime SHA256
  `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
  The supplied FBX SHA256 remains
  `50048a8a08f307d378e83d976462addcac62b5529bf60ae690da200d9d9f4485`; the
  app still quits when that FBX is opened. The fresh crash report
  (`MocapStudio-2026-10-09-042258.ips`) faults at `arena_alloc + 0x58` while
  loading through a null arena pointer. `StudioFbxImportWorker.path_copy` saves
  its hidden region parameter from x1 and forwards it to `__elisa_darray_grow`;
  the generic callback call does not forward the callback's hidden region slot.
  This earlier report supports a separate region ABI gap beyond the focused
  aggregate regression. Later matching-bundle reports fault at
  `Studio.clip.rehome+1160` instead; do not conflate the two call paths or claim
  the region repair fixes the current document-byte ownership failure.
  Compiler branch commit `d5a9b58a` adds callback region metadata and task-owned
  result arena transfer. The mocap owner reports four core-file conflicts when
  integrating it with the newer compiler and is reconciling them in an isolated
  worktree. No repaired Stage1/runtime or Studio retry is qualified yet.
  This consumer product does not include the combined
  default-grant source. The
  default-grant adoption branch contains upstream update `53ae9363` as merge
  `cfbb8a8b`. That exact Stage1 product and matching runtime were rebuilt and
  authenticated: Stage1 SHA-256
  `95d2f62660acb57fdb4d5f07623f5c6483487885cad9511defb0a4f7836350fd`, source
  tree `9934c633f646b9dbaf73edac7dd0fc0d7391808ea70c3cb6988556ce197db5d5`,
  runtime `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
  It passes the 90.01-second strict source check (1.63 GB maximum RSS), 103
  authority controls, all 24 actual-CLI cases including `-permissive`, Stage0 /
  Stage1 permission-row parity, Stage1 freshness, and the gen3 self-host fixpoint.
  The shared compiler checkout then added source fix `9d2cf1b` for match-arm
  values yielded by `can` / `trusted` blocks, with a focused fixture and smoke
  gate. It is merged into the grant-adoption branch as `107f5e14`; the `cfbb8a8`
  product is historical for this combined source. That intermediate source was
  superseded by `12120f6b`, whose exact Stage1/runtime is qualified below.
  The six declines above belong to `19294e83` and are historical until a new compile
  reports otherwise. Compiler source
  has advanced through fixed-array `usize` inference (`ca06f6cb`) and effectful
  export-alias resolution (`ec41ca95`). The clean `ec41ca95` main checkout now
  has a provenance-checked Stage1 product (`acc5c27c…`) and matching runtime
  (`013d3174…`); the corrected actual-CLI grant gate passes 24/24 controls on
  that exact product. The earlier `19294e83` pair does not qualify the newer
  source. The isolated grant branch starts at `ec41ca95` and has localized
  grants in 93 compiler source files plus an indexed permission-parameter
  lookup. Its first full strict check ran for over four
  hours against pre-scope Stage1 `358a2d3b` and was stopped while CPU-bound
  in `ga_generic_call_rows`; a five-second sample placed 2,915 of 3,203
  main-thread samples there. That hot path repeatedly scanned all annotations
  to classify generic permission parameters. The source now builds that name
  set once and uses the indexed predicate. Seeding exposed 29 locals declared
  inside narrow `can` blocks but used outside; they now use outer mutable locals
  assigned within the grant scope. Compiler source commit `a8976bf3` contains
  these changes and passed the exact strict and behavioral gates below. The
  merged source `cfbb8a8b` passes the same current gates; the older grant baseline
  remains historical. The exact branch Stage1 seed passes; product
  SHA-256 `a1fc208f…`, source tree `bd8d8d67…`, matching runtime
  `013d3174…`. The indexed exact-product strict scan now exits 0 with no
  diagnostics in 160.90 seconds; `/usr/bin/time -l` records 914,341,888-byte
  max RSS and 1,615,072,784-byte peak footprint. Its log is
  `/private/tmp/global-grant-adoption-a8976-strict.log`. The older 1,225 findings across 87 files remain only the
  pre-migration baseline. On exact product `a1fc208f…`, the authority reporter
  passes 103/103 cases, actual-CLI default enforcement and explicit
  `-permissive` controls pass 24/24, Stage0/Stage1 inferred-row parity passes,
  and the freshness assertion passes. The parity oracle binary is
  `dd0974e2…`. Proof integration has a captured baseline of 1,166 proof-source
  findings across 106 files: 1,128 effectful calls and 38 direct global accesses.
  Expression-local call grants and narrow direct-write scopes are applied in the
  proof worktree and pass `git diff --check`; exact-pair build and replay
  qualification remain open. The separate 1,271 compiler-source findings from
  an exploratory unpinned checker invocation are excluded. Repair only backend
  declines reproduced on the clean current Studio tuple before playback/redraw/reveal acceptance.
  Preserve `-permissive` only as an explicit compiler-test bypass, never as the
  consumer build mode.
- **Shipping client:** the guide rig refresh is committed at `a30d3af9`.
  Current rig, sound and cell generator checks pass on this checkout. The
  historical relocated Character Course bundle still needs a fresh optimized
  build/package acceptance; older green runs do not qualify the replacement
  toolchain or package.

