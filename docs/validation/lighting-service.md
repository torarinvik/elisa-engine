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
rejects a zero sun direction, and destroys every probe/light.

Point-light direction is ignored by the shared descriptor and can be zero.
The Wicked adapter substitutes a stable downward direction rather than
normalizing zero. The SDL3/Metal light test creates a zero-direction point
light, moves it, changes its color, intensity, range and shadow flag, and checks
those values plus spot cone settings on the live Wicked components.
Cone angles are required only for spots; point and directional lights use zero
angles at the Wicked boundary. Each light can request a fixed shadow-map size
with `shadow_resolution`; zero leaves resolution selection to Wicked, while
explicit values are powers of two from 16 through 2048. The SDL3/Metal test
checks the live component after creation and update, including the automatic
sentinel and fixed 512/256 pixel requests.
Directional and spot descriptors also set the Wicked transform rotation. The
native assertion checks the transformed local +Y axis as well as the component
direction, matching the direction Wicked uses when it updates and renders the
scene.

`RenderScene::set_sky_map` loads a project-relative color asset into Wicked's
static sky path, accepting equirectangular images with a 2:1 aspect ratio or
square cubemaps up to 8,192 pixels per edge. KTX2 assets use Elisa's checked
color transcoder; other formats use Wicked's resource manager. Rotation is
wrapped to one turn, and invalid paths, unsupported assets, and incompatible
dimensions fail without replacing the current sky. `clear_sky_map` releases
the authored map. The SDL3/Metal gate loads an 8x4 fixture, checks asset and
rotation state, pumps a frame, rejects a traversal path and a square non-cube
image, and verifies clearing.

`RenderScene::set_sun_cascade_distances` configures Wicked's three directional
shadow cascade end distances. Values must strictly increase and the last split
must fit inside the active camera's far clip. The native environment test
checks decreasing splits, splits past the camera range, and a valid 10/100/400
distance configuration against the active primary camera's 1,000-unit far
plane.

`RenderScene::set_sun_shadow_bias` configures a per-sun receiver comparison
offset in normalized reverse-Z depth, bounded to `[-0.01, 0.01]`. Wicked packs
it into directional-light shader data and applies it to the depth comparison,
so it takes effect at runtime without rebuilding pipelines. The setting is
retained if applied before the environment creates its owned sun, and the
shared quality profile carries the same value. Positive values
reduce acne and can increase light leaks; zero preserves the existing
hard-coded rasterizer bias. Rasterizer constant/slope bias remains at Wicked's
format-specific defaults because those values are baked into cached pipelines.
The native gate checks range rejection and the packed value used by the shader.
