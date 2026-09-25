# Native lighting service

`src/backend/lighting.elisa` owns backend-neutral directional, point, spot, and
rectangular area-light descriptors plus sun, ambient, sky exposure, and fog
settings. It rejects
non-finite positions, colors, directions, energy, or fog parameters, as well as zero directional
vectors, and invalid range/cone combinations before native submission.

`native/lighting_bridge.h` keeps Wicked light and environment-probe entities
behind generation-checked handles and maps validated environment values into
Wicked's weather state. Probe resolution and view distance are bounded before
allocating a Wicked probe.

`Lighting::EnvironmentProbe` validates a finite position within ±1,000,000 world
units, positive box half-extents up to 1,000,000 units per axis, power-of-two
resolution from 16 through 2,048, positive view distance up to 1,000,000, and
update intervals from 0 through 3,600 seconds. The active
`RenderScene` owns at most eight probes through opaque generation-checked handles.
The `RenderSceneEnvironmentProbes` module is included in `src/runtime/public.elisa`,
so application projects receive this API through the engine-owned public bundle.
Updates move the Wicked transform and apply resolution, view distance, realtime
mode, interval, extent, and MSAA. Extents set Wicked's probe influence box and
update its selection bounds without needlessly recapturing the cube. Changes to
capture inputs mark the cube dirty, and
`refresh_environment_probe` explicitly requests a new capture after nearby scene
content changes. Invalid values, stale handles, and capacity overflow preserve
the existing live set. Probes are removed through the owning scene service.

Light updates move native transforms and apply normalized directions through the
Wicked component. All resources are removed through the bridge, so a scene
unload returns the light count to its baseline.

`test/material.elisa` covers valid and invalid light and environment descriptors
alongside the PBR contract.
The native gate creates and moves point and spot lights, verifies Wicked transform/direction state, rejects a foreign
handle and an invalid probe resolution, applies sky exposure and height fog,
rejects a zero sun direction, and destroys every probe/light. The SDL3/Metal
render-scene gate additionally tests public probe creation, descriptor updates,
position-only invalidation, explicit refresh, invalid update atomicity, stale
handles after slot reuse, all eight slots, overflow, and complete destruction.
On 2026-09-25, the isolated engine's SDL3/Metal render-only smoke passed with a
fresh stage1 compiler product and matching runtime wrapper. The probe fixture
checks influence-extent updates without recapture, captures a red emissive box
into a static probe, and requires the resulting metallic sphere to change a 5×5
rendered patch by at least 0.01 mean luminance. It then hides the source, changes
it to blue, explicitly refreshes the probe, and requires the sphere image to
change again by the same threshold. This verifies rendered reflections and
refresh on Metal; non-Metal runtime checks remain open.

The SDL3/Metal render-scene smoke also moves a point light between two positions
over the same painted panel. It samples the Wicked 3D render result before and
after `RenderScene::update_light` and requires one of the red/blue patches to
change mean luminance by at least 0.01. The related clearcoat comparison and
test limits are documented in [`clearcoat-rendering.md`](clearcoat-rendering.md).

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
Rectangular area lights carry positive width and height in world units, orient
their local −Z emission axis, and map those dimensions into Wicked's rectangle
light component. The SDL3/Metal smoke creates and updates one, checking its live
dimensions and the transformed emission axis.
Each light can also enable Wicked's volumetric scattering pass and set a
per-light contribution boost from 0 through 8. The portable descriptor test
rejects values outside that range; the native smoke verifies the enable flag
and boost after both create and update.

`RenderScene::set_sky_map` loads a project-relative color asset into Wicked's
static sky path, accepting equirectangular images with a 2:1 aspect ratio or
square cubemaps up to 8,192 pixels per edge. KTX2 assets use Elisa's checked
color transcoder; other formats use Wicked's resource manager. Rotation is
wrapped to one turn, and invalid paths, unsupported assets, and incompatible
dimensions fail without replacing the current sky. `clear_sky_map` releases
the authored map. The SDL3/Metal gate loads an 8x4 fixture, checks asset and
rotation state, pumps a frame, rejects a traversal path and a square non-cube
image, and verifies clearing. After the reference screenshots have been saved,
the gate hides the game geometry, points the camera at the sky, and compares
GPU readbacks: loading the fixture, setting sky exposure to zero, and clearing
the map must each change a 5x5 rendered patch by at least 0.01 mean luminance.
Running this last keeps the temporary sky state out of the lighting and
post-process references.

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
reduce acne and can increase light leaks; zero preserves Wicked's
format-specific caster rasterizer bias. The native gate checks range rejection
and the packed value used by the shader.

`RenderScene::set_sun_shadow_rasterizer_bias` changes the caster's constant and
slope depth bias on Wicked's Metal and Vulkan backends. Metal applies these
values as dynamic encoder state; Vulkan uses dynamic pipeline state. Wicked's
Direct3D 12 backend also supports the setting when `D3D12_OPTIONS16` reports
dynamic depth bias: it opts only the two shadow rasterizer pipelines into
dynamic bias and updates command-list state. Older D3D12 runtimes or adapters
continue to return `RenderSceneError.UnsupportedFeature`. The PS5 backend still
reports that error. D3D12 and PS5 runtime visual validation remains pending on
their target systems. The SDL3/Metal
reference renders the same isolated caster and receiver with the default
`(-1, -4)` and variant `(256, -2)` values, requires at least a `0.01` full-frame
patch-luminance change, confirms the single- and double-sided shadow states
share the live bias, and restores the defaults. It also rejects out-of-range
slope bias without changing the live state. The Vulkan implementation compiles
in the pinned Wicked build; runtime visual validation on a Vulkan device remains.

The SDL3/Metal render smoke also checks that `set_sun_shadows` changes visible
lighting. It temporarily hides the smoke scene's existing geometry, electric
arc, and HUD, then creates a lit plane and a lit box above it. With the sun
above the receiver, it renders three frames with shadows disabled and three
with them enabled. A test-only probe compares the full 3D render target using
overlapping 5×5 mean-luminance patches and requires at least a `0.01` change.
This keeps animated or unrelated scene content from satisfying the check and
proves the setting affects a cast shadow, as well as Wicked's state. The smoke
can save the two presentation captures as
`build/render-scene-shadows-disabled.png` and
`build/render-scene-shadows-enabled.png`.

The same isolated scene checks the live receiver-bias effect. It captures the
shadowed frame at bias `0`, changes the bias to `0.005`, renders three more
frames, and requires the same `0.01` patch change while also checking Wicked's
packed live value. It restores the smoke's prior `0.001` bias before returning
to the rest of the render tests.

The same reference scene compares outdoor and indoor lighting. The outdoor
capture uses the authored sun; the indoor capture removes sun contribution and
uses a warm rectangular area light above the receiver. Both use the same lit
plane, caster, and camera, and the smoke requires a `0.01` full-frame patch
change. Captures are written to `build/render-scene-lighting-outdoor.png` and
`build/render-scene-lighting-indoor.png`.

The lighting reference also places a blue translucent box over the outdoor
receiver. It saves `build/render-scene-lighting-transparent.png`, checks the
blend changes the rendered 3D image by at least `0.01`, then switches that same
lit object to opaque and requires another `0.01` image change. This verifies
visible blending under authored environment light, rather than only checking
the alpha-mode fields. The capture is generated by the SDL3/Metal render smoke.

The render smoke also saves twelve downsampled 160×100 references under
`docs/validation/references/lighting/`: the two point-light positions, shadow
toggle, outdoor/indoor, translucent/opaque, and receiver/rasterizer-bias
baseline and variant frames. Every run compares fresh captures against these
tracked PNGs, using a maximum RGB channel difference of `0.35` and mean
per-pixel difference of `0.025` on the 0–1 scale. Across three SDL3/Metal runs,
two generated identical captures; a fresh run after the render-graph merge also
passed, with the largest observed per-image peak and mean differences of
`0.2275` and `0.0015`. These tolerances are scoped to this Metal fixture; they
do not establish cross-backend image equivalence. Regenerate references only
after reviewing the full-size captures and confirming an intentional visual
change:

```sh
ELISA_UPDATE_LIGHTING_REFERENCES=1 \
ELISA_RENDER_SCENE_RENDER_ONLY=1 \
WICKED_BUILD="../WickedEngine/build-elisa-sdl3" \
/opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py
```

The comparator can also be run directly with
`/opt/homebrew/bin/python3.14 scripts/compare_lighting_references.py` after a
smoke capture.
