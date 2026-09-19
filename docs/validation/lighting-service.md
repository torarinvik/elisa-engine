# Native lighting service

`native/lighting_bridge.h` keeps Wicked light and environment-probe entities
behind generation-checked handles. Elisa descriptors select directional, point,
or spot lights, color, intensity, range, cone angles, and shadow casting.

Environment probes validate power-of-two resolution, view distance, and realtime
mode before creating a Wicked probe. All resources are removed through the
bridge, so a scene unload returns the light count to its baseline.

The native gate creates and updates point and spot lights, rejects a foreign
handle and an invalid probe resolution, enables shadow casting and realtime
probe state, and destroys every resource. Camera and renderer scheduling still
own when the resulting light/probe data is consumed.
