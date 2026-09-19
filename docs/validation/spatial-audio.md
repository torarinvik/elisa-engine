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
