# Visual asset shelf

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is W04/W06 progress. [`course-durable-beacons.md`](course-durable-beacons.md)
listed catalogue resolution of restored asset IDs as remaining.

## Design

- `src/assets/visual_shelf.elisa` (`VisualShelf`) holds up to 16 mesh and
  material entries keyed by stable `AssetId`, each registered in the asset
  catalogue too.
  - A mesh carries the game's own shape code. A material carries a colour.
  - Duplicate, zero-ID and over-capacity registrations are rejected.
  - `visual_known` is true only when the mesh ID resolves to a mesh and the
    material ID to a material, so swapped kinds and unknown IDs both fail.
  - `mesh_shape` and `material_colour` return `Unknown` for a wrong-kind or
    missing ID rather than guessing.
  - `recolour` replaces a material's colour and bumps its catalogue cooked
    generation, as a recook would.
- `CourseBeacons` no longer hard-codes which IDs exist. It stocks a shelf
  (two meshes, three materials) and uses it in three places:
  - a load checks every restored override against the shelf
    (`LOAD_UNKNOWN_ASSET`);
  - render creation resolves each restored ID to a primitive;
  - each restored ID resolves to a colour.

## Checks

- `test/visual_shelf.elisa` exits 0 (`scripts/check.elisascript`). It covers:
  - kind checks;
  - unknown IDs;
  - swapped kinds;
  - shape and colour resolution;
  - duplicate, zero-ID and capacity rejection;
  - catalogue generations before and after `recolour`;
  - `recolour` of a mesh failing.
- Negative control: making `has_material` accept any known kind fails
  the test with code 3.
- The character-course smokes (beacon save, corrupt, unknown-asset and
  relaunch cases, codes 131–152) pass against the shelf-backed beacons.

## Gaps

- The shelf still resolves to primitives and colours. Loading cooked mesh
  and material packages by these IDs through the native asset loader remains.
- The shelf is stocked in code, not read from an authored catalogue file.
