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
same-pass read/write hazards, conflicting accesses without an ordering path,
reads before initialization, writes to imported resources, unused resources,
and transient resources without a writer. Multiple writes are accepted only
when dependencies serialize them. Transient resources share a slot only when
their lifetimes do not overlap and their size mode, dimensions, format, and
sample count match. Before submission, the runtime recomputes and compares the
compiled plan so mutated pass order, resource intervals, or alias slots cannot
reach the native allocator. Each clear, depth-clear, or copy operation must match that
pass's declared graph reads and writes. The selected output is a terminal use:
if its transient slot would be overwritten by a resource used after the output's
first access, the runtime gives the output its own slot through composition.

The native executor currently accepts one imported primary scene-color image,
color targets in RGBA8, RGBA16F, or Wicked's R11G11B10F main format, D32 depth
targets, and fixed or primary-internal extents. Color sample counts 1, 2, 4,
and 8 are accepted only when Wicked creates the exact requested count; depth
targets and imported scene color remain single-sampled. Operations are
transparent black `ClearColor`, depth-one `ClearDepth`, exact single-sample
`CopyColor`, and `ResolveColor` from a multisampled color target to a compatible
single-sample target. A depth target can be cleared and transiently aliased, but
cannot be sampled or selected for composition. The executor allocates targets
on the render thread, maps transient resources sharing a planner slot to the
same Wicked texture, and selects the configured color output as the path's
postprocess result. Persistent initialized color targets are zeroed at
allocation; initialized persistent depth targets are rejected. Multiple
imports, arbitrary shader callbacks, and load/store variants are rejected by
the native boundary.

The SDL3/Metal native smoke builds a two-pass graph: clear transient resource 2,
then copy imported scene color to resource 3. The planner proves that the two
transient lifetimes can share one target; the test rejects a forged plan and a
copy operation that disagrees with its declared reads, then renders the
transient clear target as output and verifies it stays black despite a
later pass sharing the planner's original slot. It restores the scene-copy
output and checks that the rendered scene returns. The smoke also exercises
resize/restoration. Test-only failure injection forces target allocation and
second-pass failures; both preserve the base postprocess output, leave the
execution count unchanged, and recover on the next frame. Existing rendered
image comparisons still pass. Target allocation is repeated when internal
resolution changes. A test-only zero-resolution injection checks suspended
status, unchanged execution count, retained fallback, and recovery on the next
frame. A third graph clears a transient D32 target and a four-sample color
target, resolves the color target, and confirms the resolved output stays
black. A later pass deliberately shares its planner slot, so this also checks
that the selected resolve output remains live through composition. The native test also
injects SDL3 minimized/restored events through the
application queue: minimize returns the host's suspended status without
advancing the frame or graph, keeps the last rendered output, and restore runs
the graph again. For deferred retirement, the smoke configures two 1024×1024
RGBA16F targets, replaces them, and samples Metal device allocation before and
after Wicked's buffer-count retirement window plus a GPU wait. Replacement
raises allocation while the old targets await retirement; after the window,
allocation falls by at least 8 MiB. This measurement is specific to the tested
macOS Metal path. A user-driven OS minimize or device-loss cycle and broader
rendered graph references remain open R15 work.

Run the focused planner test with:

```sh
ELISA_ALLOW_STALE_STAGE1=1 ../Elisa-compiler/scripts/elisac_stage1.sh \
  -emit exe -o build/render-graph-test test/render_graph.elisa
build/render-graph-test
```

The planner test is included in `elisascript scripts/check.elisascript`. The
native integration is included in `scripts/render_scene_native_smoke.py` and
requires the pinned Wicked SDL3/Metal build on macOS.
