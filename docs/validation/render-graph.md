# Elisa render-graph planning contract

`RenderGraph` is the Elisa-owned, bounded contract for describing render work. It
keeps graph construction and validation independent of Wicked so the same
resource and pass rules can be tested without a GPU or window.

The current planner supports up to 32 passes and 64 resources. A resource has a
stable caller-provided ID, fixed dimensions, a format, a sample count, and an
`Imported`, `Persistent`, or `Transient` lifetime. Passes declare resource reads
and writes, and explicit dependencies establish execution order. `compile`
returns a deterministic topological order, resource use intervals, and
transient alias slots.

Compilation rejects invalid descriptors, duplicate or unknown IDs, cycles,
same-pass read/write hazards, conflicting accesses without an ordering path,
reads before initialization, writes to imported resources, unused resources,
and transient resources without a writer. Multiple writes are accepted only
when dependencies serialize them. Transient resources share a slot only when
their lifetimes do not overlap and their dimensions, format, and sample count
match.

The planner is not yet the native render graph executor. The current descriptor
set has no size-relative extents, load/store operations, per-pass callbacks,
Wicked resource handles, or native allocation and retirement. SDL3/Metal
execution, resize and suspension handling, failure cleanup, and rendered
reference captures remain R15 work.

Run the focused test with:

```sh
ELISA_ALLOW_STALE_STAGE1=1 ../Elisa-compiler/scripts/elisac_stage1.sh \
  -emit exe -o build/render-graph-test test/render_graph.elisa
build/render-graph-test
```

The same test is included in `elisascript scripts/check.elisascript`.
