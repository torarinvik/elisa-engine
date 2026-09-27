# Godot camera-ray picking

`backends/godot/scene_picking.gd` maps a Godot `Camera3D` viewport point to a
physics ray and returns the Elisa gameplay epoch and entity ID bound to the
first colliding body. The caller supplies the collision mask, and the screen
point is in that camera's viewport pixel coordinates. Rays have a fixed maximum
distance; invalid cameras, points, and masks return `INVALID_ARGUMENT`.

`bind_identity` stores `elisa_gameplay_epoch` and `elisa_gameplay_id` metadata on
the collision body after checking both values are positive. A ray returns one
of four statuses: `HIT`, `MISS`, `NOT_SELECTABLE`, or `INVALID_ARGUMENT`. An
unbound collider stops the query with `NOT_SELECTABLE`, so a non-gameplay object
cannot be silently skipped to select something behind it. Elisa can validate
the returned epoch against its current world before using the entity ID.

`backends/godot/editor_picking_probe.gd` runs inside the regular Godot scene
probe, using the scene manifest's actual Elisa epoch and entity ID. The headless
test creates live Godot physics bodies, waits for the physics server, and checks
the center-camera hit and distance, excluded collision masks, an off-target
miss, out-of-viewport and null-camera rejection, and an unbound blocker. Run the
Godot contract gate with:

```sh
godot --headless --path backends/godot --script res://probe.gd -- "$PWD/backends/scene_manifest.txt"
```

The existing Godot backend probe passed on Godot 4.7.2 with the picking checks
enabled. This covers camera rays and checked identity on Godot; visible
selection outlines remain implemented and visually verified only on Wicked.
