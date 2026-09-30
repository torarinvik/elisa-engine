# Action input profiles (saved rebinding)

`ActionInput` now exposes `binding_count`, `binding_at` and `rebind_checked(input, index, code, chord)`.
A rebind keeps the binding's action, device, dead zone and context, and it runs the same code, chord and duplicate checks as `bind_checked`.
It also clears the old source's held state, so an action held through a rebind is not left stuck.

`ActionInputProfile` (src/runtime/action_input_profile.elisa) stores a whole map as bytes.
The format is "EIB1", a u32 count, then 20 bytes per binding. Dead zones are stored in 1/2^20 steps.

`profile_load` loads only into an empty map. It first checks the magic, the length and the reserved and enum bytes.
It then replays every record through `bind_checked` into a scratch map, and the target receives bindings only if every record binds.
A profile with a duplicate or zero-code record is therefore rejected whole.

Coverage: test/action_input_profile.elisa (codes 1–22; registered in the gate).
Negative control: keeping `binding_down` across a rebind makes the test fail with code 5.
Not yet covered:
- writing profile bytes to a save file on disk;
- a rebinding UI in the course;
- per-controller profiles.
