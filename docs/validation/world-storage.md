# General world storage validation

`src/world/storage.elisa` adds a bounded typed catalogue beside the checked
maze world. It owns one monotonic identity stream while keeping actor and
projectile columns separate, so unrelated game sets can coexist without
exposing physical storage to callers. Spawn, typed lookup, destruction, and
column compaction preserve IDs and counts.

`test/world_storage.elisa` passed on 2026-09-19 through the stage-1 compiler.
The shared `scripts/check.elisascript` gate now compiles and runs this fixture
alongside the existing world lifecycle tests. Capacity and allocator-pressure
soaks remain future W10 work.
