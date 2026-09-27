# Elisa render graph

`RenderGraph` is the Elisa-owned, bounded contract for describing render work.
Graph construction, hazard checks, and lifetime planning stay independent of
Wicked, while `RenderGraphRuntime` submits a compiled plan to the primary Wicked
render path through a checked scalar ABI.

The planner supports up to 32 passes and 64 resources. Resources can use fixed
dimensions or the primary internal resolution and have an explicit format,
sample count, and `Imported`, `Persistent`, or `Transient` lifetime. Passes
declare reads and writes, and dependencies establish execution order. `compile`
returns a deterministic topological order, resource-use intervals, and
compatible transient alias slots. Internal-resolution descriptors use zero for
their stored width and height; the runtime resolves them after Wicked updates
the render path's buffers.

Compilation rejects invalid descriptors, duplicate or unknown IDs, cycles,
conflicting accesses without an ordering path,
reads before initialization, writes to imported resources, unused resources,
and transient resources without a writer. Multiple writes are accepted only
when dependencies serialize them. Transient resources share a slot only when
their lifetimes do not overlap and their size mode, dimensions, format, and
sample count match. Before submission, the runtime recomputes and compares the
compiled plan so mutated pass order, resource intervals, or alias slots cannot
reach the native allocator. Each operation must match that pass's declared
graph reads and writes. Explicit `ReadWrite` access is valid for initialized
resources; the backend accepts it only for operations with defined in-place
semantics. The selected output is a terminal use:
if its transient slot would be overwritten by a resource used after the output's
first access, the runtime gives the output its own slot through composition.

The native executor currently accepts one imported primary scene-color image
and an optional imported linear-depth image from Wicked's shader-readable
`depthBuffer_Copy`. Imports declare their source explicitly; linear depth uses
R32_FLOAT and supports exact copies into graph-owned R32 targets plus a
`VisualizeLinearDepth` pass that samples it with Wicked's built-in image shader
and writes grayscale to a single-sample color target. `BlendColor` draws a
single-sample color source over an initialized, single-sample color destination
using Wicked's alpha blend pipeline. `AdjustSaturation` reads a color resource
and writes a separate, matching single-sample color target through Wicked's
built-in image shader; its Elisa-authored value is bounded from 0 through 2.
`ScaleColor` uses the same shader path to multiply RGB intensity from 0 through
8 into a separate, matching single-sample color target.
Custom depth-sampling
shaders and depth-tested geometry remain unsupported. Other targets use RGBA8,
RGBA16F, Wicked's R11G11B10F main format, or D32 depth, with fixed or
primary-internal extents. Color sample counts 1, 2, 4,
and 8 are accepted only when Wicked creates the exact requested count; depth
targets and imported scene color remain single-sampled. Operations are
transparent black `ClearColor`, depth-one `ClearDepth`, exact single-sample
`CopyColor`, `ResolveColor` from a multisampled color target to a compatible
single-sample target, and `BlendColor` with an initialized single-sample color
destination. A depth target can be cleared and transiently aliased, but
cannot be sampled or selected for composition. The executor allocates targets
on the render thread, maps transient resources sharing a planner slot to the
same Wicked texture, and selects the configured color output as the path's
postprocess result. Persistent initialized color targets are zeroed at
allocation; initialized persistent depth targets are rejected. Multiple
imports, arbitrary shader callbacks, and load/store variants are rejected by
the native boundary.

The SDL3/Metal native smoke builds a three-pass graph: clear transient resource
2, copy imported scene color to resource 3, then alpha-blend imported scene
color over resource 3 in place. The planner proves that the two transient
lifetimes can share one target while keeping resource 3 live through the blend;
the test rejects a forged plan and a copy operation that disagrees with its
declared reads, then renders the transient clear target as output and verifies
it stays black despite the later passes sharing the planner's original slot.
It restores the blended scene output and checks that the rendered scene
returns. A diagnostic isolated-order run passed the complete graph fixture on
Metal, including its frame checks; the following quality-history check returned
native test case 20. The regular runner reaches case 20 before the graph
fixture, so its full SDL3/Metal run currently stops before R15. The smoke also exercises
resize/restoration. Test-only failure injection forces target allocation and
second-pass failures; both preserve the base postprocess output, leave the
execution count unchanged, and recover on the next frame. Existing rendered
image comparisons still pass. Target allocation is repeated when internal
resolution changes. A test-only zero-resolution injection checks suspended
status, unchanged execution count, retained fallback, and recovery on the next
frame. A third graph clears a transient D32 target and a four-sample color
target, resolves the color target, and confirms the resolved output stays
black. A later pass deliberately shares its planner slot, so this also checks
that the selected resolve output remains live through composition. The native
test calls SDL3's real minimize and restore operations, synchronizes the
window, and processes the resulting state through the normal application pump.
It confirms that minimize returns the host's suspended status without
advancing the frame or graph, keeps the last rendered output, and restore runs
the graph again. The host also reconciles SDL's minimized flag after event
polling, covering window managers that change the flag before delivering an
event. For deferred retirement, the smoke configures two 1024×1024
RGBA16F targets, replaces them, and samples Metal device allocation before and
after Wicked's buffer-count retirement window plus a GPU wait. Replacement
raises allocation while the old targets await retirement; after the window,
allocation falls by at least 8 MiB. This measurement is specific to the tested
macOS Metal path. Device-loss recovery and broader rendered graph references
remain open R15 work. The final graph fixture imports
both primary scene color and linear depth, visualizes depth into a transient
color target, and then restores scene color as the composed output. A test-only
GPU readback confirms the depth visualization is spatially nonuniform.

`BlendColor` accepts an Elisa-authored opacity from 0 through 1. The legacy
native `set_pass` entry point retains full-opacity behavior; the additive
`set_pass_with_opacity` entry point validates the value again before staging.
The native application fixture clears an output, blends scene color at zero
opacity and verifies a black frame, then repeats at half opacity and verifies
the scene becomes spatially visible. Opacity outside the range is rejected by
the Elisa adapter before the native graph is staged, and a direct native ABI
test confirms the executor repeats that check.

`AdjustSaturation` uses its own additive scalar ABI, leaving the older pass
entry points and opacity behavior intact. The native application fixture
rejects an out-of-range value in both the Elisa adapter and native boundary,
applies zero saturation to the rendered scene, and uses GPU readback hashing
to confirm the frame changed before restoring the ordinary scene-color graph.
The full render-only SDL3/Metal smoke passes against the manifest-pinned Wicked
checkout on macOS 27 / Apple M5.

`ScaleColor` accepts a bounded intensity multiplier from 0 through 8 through
its own additive scalar ABI. Its native application fixture rejects an
out-of-range value at both validation boundaries, scales the scene to zero, and
checks that the read-back image becomes uniform black before restoring scene
color. The full render-only smoke passes on the pinned SDL3/Metal backend.

The 2026-09-27 SDL3/Metal render-only application run reached and passed the
full graph fixture after correcting the preceding quality-history expectation.
The graph checks run in the ordinary Elisa application entry, not a standalone
native probe. High/Low rendered references also pass after the documented
macOS 27 High-reference refresh.

Run the focused planner test with:

```sh
ELISA_ALLOW_STALE_STAGE1=1 ../Elisa-compiler/scripts/elisac_stage1.sh \
  -emit exe -o build/render-graph-test test/render_graph.elisa
build/render-graph-test
```

The planner test is included in `elisascript scripts/check.elisascript`. The
native integration is included in `scripts/render_scene_native_smoke.py` and
requires the pinned Wicked SDL3/Metal build on macOS.
