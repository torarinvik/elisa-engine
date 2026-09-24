# Native lighting service

`src/backend/lighting.elisa` owns backend-neutral directional, point, and spot
light values plus sun, ambient, sky exposure, and fog settings. It rejects
non-finite positions, colors, directions, energy, or fog parameters, as well as zero directional
vectors, and invalid range/cone combinations before native submission.

`native/lighting_bridge.h` keeps Wicked light and environment-probe entities
behind generation-checked handles and maps validated environment values into
Wicked's weather state. Probe resolution and view distance are bounded before
allocating a Wicked probe.

Environment probes validate power-of-two resolution, view distance, and realtime
mode before creating a Wicked probe. Light updates move native transforms and
apply normalized directions through the Wicked component. All resources are removed through the
bridge, so a scene unload returns the light count to its baseline.

`test/material.elisa` covers valid and invalid light and environment descriptors
alongside the PBR contract.
The native gate creates and moves point and spot lights, verifies Wicked transform/direction state, rejects a foreign
handle and an invalid probe resolution, applies sky exposure and height fog,
rejects a zero sun direction, and destroys every probe/light. Authored sky-map
loading, shadow-bias policy, and camera/renderer scheduling remain open.

`RenderScene::set_sun_cascade_distances` configures Wicked's three directional
shadow cascade end distances. Values must strictly increase and the last split
must fit inside the active camera's far clip. The native environment test
checks decreasing splits, splits past the camera range, and a valid 10/100/400
distance configuration against the active primary camera's 1,000-unit far
plane.
