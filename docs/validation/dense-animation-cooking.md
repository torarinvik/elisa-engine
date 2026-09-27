# Dense animation cooking

GLB asset declarations can specify `animation_sample_rate: 30`, `60`, or `120`.
The cooker sets Blender's conversion rate before importing glTF, avoiding loss
of dense contact corrections through a default 24 Hz intermediate. Compatible
external FBX animation keys are retimed when import changes the scene rate.
Omitting the GLB option retains the existing conversion behavior.

The FBX importer samples authored rates above 30 Hz at their integer ceiling,
up to 120 Hz. Lower rates keep the existing 30 Hz normalization. Animated input
above 120 Hz is rejected, rather than silently downsampled. Frame and total
sample-float budgets remain unchanged; long dense clips can exceed those limits
and fail explicitly. Runtime playback already uses each clip's stored rate.

Evidence: 17 build-command tests, GLB container self-test and FBX cooker
self-test pass. Both 120 Hz boxing libraries cook with 16 named clips and
unchanged durations. All 52 deform-bone positions and complete skin matrices
match their authored candidate at every cooked sample; worst position error is
below 0.004 mm. Geometry/skin package payloads match the prior packages byte for
byte. Reports reside in the boxing game's `build/square-hook-contact`.

A separate comparison with prior 30 Hz runtime samples shows up to 31.1 mm
joint differences during fast motion. This is retained as diagnostic evidence:
the old GLB path passed through lower-rate Blender conversion. The authored
pose comparison is the fidelity check; it does not prove whole-body collision,
contact state, controller feel or AAA animation quality.
