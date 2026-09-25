# Spatial audio validation

`src/audio/spatial.elisa` keeps source attachment and spatial policy in Elisa
state. Sources are bounded, generation-independent engine records keyed by a
stable entity ID; detaching an entity removes its source before a native mixer
can observe a stale attachment.

The policy validates ranges, cone thresholds, occlusion, duplicate IDs, and
capacity. It computes distance attenuation, forward cone gain, occlusion
attenuation, and a bounded Doppler ratio from listener/source positions and
velocities. Native backends can consume these values without importing world
or audio-library types.

`test/audio_spatial.elisa` covers attachment, duplicate rejection, cone and
distance gain, Doppler response, full occlusion, and safe detach. The shared
gate compiles and runs this fixture.

The native miniaudio gate now accepts listener/source velocities, applies a
validated occlusion factor in the callback mix, exposes a bounded Doppler ratio,
and rejects invalid occlusion values while retaining stale-handle checks.

## World attachment and callback spatial mix

`src/runtime/world_audio.elisa` adds `WorldAudio` to the public runtime bundle.
An affine `WorldAudio::Emitters` set binds up to 32 checked `World` entities to
voices owned by a `RuntimeServices::Session`. Elisa computes all spatial policy.
Native code receives only a per-voice gain and Doppler pitch ratio:

- `attach` validates the entity, voice, and emitter settings before it changes
  any state. It reads the entity's current transform, stores a `SpatialAudio`
  source, and pushes the first mix. If the native push fails, it releases the
  voice slot and detaches the source, so a rejected attach leaves no binding and
  never adopts or stops the offered voice.
- `update` rejects zero, negative, and non-finite time steps. For each emitter
  it reads the entity's transform, derives velocity from the last committed
  position, rotates the emitter's local forward by the entity rotation, and
  pushes gain and pitch. A step longer than 0.25 s is treated as a hitch and
  produces zero velocity, not a large Doppler shift.
- The first pass only stages and pushes. The second pass commits the staged
  sources and removes detached emitters from the back of the table.
- Despawned entities have their voices stopped and bindings removed
  (`detached_despawned`). Voices that ended or were stopped outside `WorldAudio`
  are unbound as `detached_finished`.
- The listener follows a tracked entity. If that entity despawns, the listener
  keeps its last position with zero velocity, stops tracking, and the report
  sets `listener_lost`.
- `set_occlusion` feeds a caller-computed occlusion value in [0, 1]. `detach`
  stops the voice before unlinking it, so a failed stop can be retried.
- `update_with_physics` refreshes world transforms and derives occlusion with
  one filtered physics ray from the listener to each emitter. The caller chooses
  a nonzero layer mask and occlusion strength; a hit between the endpoints
  applies that strength, while a miss or a hit at the source endpoint leaves
  the source unobstructed. This keeps acoustic filtering in Elisa and lets a
  game select which physics categories can block sound without a native shim.

The native mixer gets `elisa_audio_v1_set_voice_spatial(slot, generation, gain,
pitch_ratio)`. It accepts only finite values, gain in [0, 1], pitch in
[0.5, 2], and a live generation. The callback applies the gain and advances a
Q16 fractional cursor, interpolating linearly between frames. At unit pitch the
output is bit-identical to the unspatialized path. `play` now resets the whole
voice slot. Before this fix, a reused slot kept the previous voice's position,
occlusion, and spatial mix.

`SpatialAudio::source_attach` and `source_update` now reject non-finite
position, velocity, and forward vectors (`SpatialError.InvalidValue`). They
also reject NaN range, cone, and occlusion values, which previously passed
ordered comparisons. A source is always stored as live, whatever the caller
passed.

Evidence:

- `native/miniaudio_spatial_probe.h` runs in the sanitized boundary harness and
  the native miniaudio probe. It checks:
  - unit mix identity;
  - silent-but-live gain 0 and a halved gain;
  - exact octave-up resampling, with a one-shot voice ending at half length;
  - octave-down interpolation;
  - a looped, pitched voice that wraps;
  - rejection of NaN, infinite, and out-of-range values and of a stopped
    handle;
  - neutral state on a reused slot.
- `test/audio_spatial.elisa` adds adversarial cases:
  - NaN or infinite position, velocity, range, cone, and occlusion are rejected;
  - a caller-supplied `live: false` is stored as live;
  - a rejected update leaves the stored source unchanged.
- `test/world_audio_probe.elisa` runs inside the SDL3/Metal application smoke
  with the silent miniaudio route. It checks:
  - rejected attachments (empty voice, zero forward, NaN or inverted range,
    despawned entity, stopped voice) leave no binding;
  - a duplicate attach is refused;
  - a close emitter moving toward the listener gets Doppler above 1 and audible
    gain, while one beyond its range is silent;
  - a hitch step resets velocity;
  - valid occlusion is applied, and NaN or unknown-entity occlusion is rejected;
  - a real Jolt body on the selected layer attenuates a source ray, then removal
    of that body restores the unobstructed mix;
  - a despawned entity's voice is stopped and unbound;
  - an externally stopped voice is unbound as finished;
  - listener loss, invalid time steps, and repeated detach are handled.

Limits: physics occlusion performs one ray per emitter per update and uses a
single caller-selected strength; transmission, diffraction and room effects
remain part of S04. The mix is gain and pitch only, with no stereo panning or
HRTF. `WorldAudio` does not submit native source positions, so native distance
attenuation stays neutral and is not applied twice.

This slice also exposed a stage1 compiler miscompile. Passing `&name`, where
`name` is already a reference parameter, silently binds the parameter's own
storage when the call is inside `catch`, inside a `raise ... if` condition, is
module-qualified, or targets an immutable reference. In the probe this showed
up as a non-deterministic trap in `World.world_spawn`. The same pattern was
fixed in `RuntimeServices::pump`, `Rollback::restore`, `EditorSession`, and the
maze native client, and `scripts/check_module_hygiene.py` now rejects it.

Validation on 2026-09-21 (macOS 27.0 / Apple M5, SDL3/Metal):

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh python3 scripts/application_native_smoke.py`
  passed both the normal and startup-failure cleanup projects, including
  `WorldAudioProbe`. Three repeated runs of one build also passed.
- `python3 scripts/run_boundary_sanitized.py` passed with no AddressSanitizer
  or UBSan finding, including the spatial mix checks.
- `PYTHONPATH=scripts python3 scripts/render_scene_native_smoke.py` passed,
  including the maze native smoke.
- `test/audio_spatial.elisa`, `test/world.elisa`, and `test/editor_session.elisa`
  passed on their own.
- `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript scripts/check.elisascript`
  passed, including both proof suites (17/17 and 6/6 certificates replayed).
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`,
  and `git diff --check` passed.
- On 2026-09-25, the complete eight-fixture SDL3/Metal
  `scripts/application_native_smoke.py` suite passed with the selected Wicked
  SDL3 backend. Its world-audio fixture places a Jolt box between listener and
  source, verifies attenuation, removes the box, and verifies the clear-ray
  mix is restored. It also externally stops a live voice, verifies it leaves
  the native active-voice count immediately, and confirms the following Elisa
  update reports exactly one finished detach with no remaining binding.
  Source-length and module-hygiene checks passed.
