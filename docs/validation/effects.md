# Particle and decal validation

`native/effect_bridge.h` keeps Wicked particle emitters and decals behind a
bounded generation-checked pool. Emitter descriptors validate capacity, count,
lifetime, and size before configuring `EmittedParticleSystem`; decal descriptors
validate color, opacity, range, and slope blending before configuring the native
component. An emitter tick is explicit and destruction removes both components,
which keeps owner/lifetime policy outside the vendor API.

Evidence: the SDL3/Wicked gate creates, updates, ticks, and destroys one emitter
and one decal, rejects a foreign handle, and verifies component-count baselines.
Elisa event spawning and rendered combat effects remain open R09 work.
