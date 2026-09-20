# Action input validation

`src/runtime/action_input.elisa` owns the portable action layer between SDL3
events and gameplay or UI code. Device codes stay at the boundary; consumers
read stable action IDs through `action_down`, `action_pressed`,
`action_released`, and `action_value`.

The module validates action and device codes, dead zones, duplicate bindings,
and bounded binding/state capacity. Bindings carry a context and an optional
chord code. `begin_frame` clears edge state, `set_context` isolates gameplay
and UI actions, and disconnecting a device releases all held actions from that
device's state without leaving stale input live.

`bind_checked` reports those validation failures as `BindResult` without an
error union. It checks both bounded tables before writing either, so a full
action table cannot consume a binding slot. `bind` remains available and maps
the same results to typed `InputError` values. The fixture checks every result,
the typed error mapping, full binding/action tables, repeated action-capacity
failures, and preservation of a held action when another binding is added.

Multiple bindings for one action are aggregated: releasing one binding keeps
the action down while another remains held, and the action releases only when
the last held binding is released or disconnected. The reported value is the
strongest remaining binding value, and frame boundaries clear edges while
preserving held values. The fixture `test/action_input.elisa` covers this in
both release orders and across device disconnects, alongside analog dead-zone
filtering, single-binding pressed/held/released transitions, context isolation,
chord activation, disconnect cleanup, and an analog value that falls below a
binding's dead zone without reaching zero. The same fixture checks public
engine key/button/axis codes, while
`test/application_gamepad_codes.cpp` checks SDL code mapping and normalized
axis/token conversion. The shared gate compiles and runs the Elisa fixtures
with the stage1 compiler.

For the checked-binding change, `elisac-stage1 -emit exe -o
build/action-input-test test/action_input.elisa && build/action-input-test`
passed. `DEVELOPER_DIR=/Library/Developer/CommandLineTools elisascript
scripts/check.elisascript` passed the full portable suite, SDL3 probe, Godot
4.7.2 probe, and both Elisa Proof suites. `scripts/check_source_length.py`,
`scripts/check_module_hygiene.py`, and `elisascript --check
scripts/check.elisascript` also passed.

The native Wicked gate also runs `native/action_input_bridge.h`: SDL3 keyboard
events become portable action edges, analog values honor dead zones, and focus
loss clears transient state without marking connected devices unplugged. Its
probe covers alternate bindings, per-device disconnect, and held values across
frames. The native adapter never exposes SDL codes to gameplay.
