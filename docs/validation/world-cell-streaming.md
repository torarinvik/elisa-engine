# World-cell streaming validation

`src/world/cell_streaming.elisa` keeps scene-cell requests bounded and separate
from native resource handles. A request is keyed by cell coordinates and a
dependency generation. Repeating a request coalesces it and updates the
dependency token; activation rejects stale tokens. Resident cells consume an
explicit budget, while queued or resident cells outside `radius + hysteresis`
are cancelled and become stale through their generation-checked handle.

`test/cell_streaming.elisa` covers request coalescing, stale dependencies,
resident-budget rejection, hysteretic cancellation, and reactivation after the
old resident cell is unloaded. The service remains intentionally bounded; an
asset loader or package mount supplies the dependency generation and performs
the actual decode/upload work.

`src/world/cell_world.elisa` now connects the same policy to the primary checked
`World`: a resident cell owns a generation-checked `EntityRef`, activation rolls
back both the stream request and the spawned entity on budget or world failure,
and unload releases the world entity before returning the stream slot. The
focused `test/cell_world.elisa` covers coalescing, budget rejection, world
identity, hysteretic trimming, generation reuse, and complete destruction.

Run the focused primary-world bridge with:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
"$HOME/.elisac/elisac-stage1" -emit exe -o build/cell-world-test \
test/cell_world.elisa && build/cell-world-test
```
