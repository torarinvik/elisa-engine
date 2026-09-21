# Action input validation

`src/runtime/action_input.elisa` owns the portable action layer, and
`src/runtime/action_input_runtime.elisa` connects it to events from the
SDL3-backed `Application` queue. Device codes stay at the boundary; consumers
read stable action IDs through `action_down`, `action_pressed`,
`action_released`, and `action_value`.

The module validates action and device codes, dead zones, duplicate bindings,
and bounded binding/state capacity. Bindings carry a context and an optional
chord code. `begin_frame` clears edge state, `set_context` isolates gameplay
and UI actions and clears both held and edge state, and disconnecting a device
releases all held actions from that device's state without leaving stale input
live.

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
filtering, single-binding pressed/held/released transitions, and context
isolation. Chords track their source codes independently, work whether the
primary or chord key arrives first, and release only when no matching binding
remains active. The fixture checks focus-loss and overflow clearing, gamepad
connect/disconnect, keyboard, mouse-button, and analog-trigger translation, and
an axis value falling below its dead zone without reaching zero. It also checks
public engine key/button/axis codes, while
`test/application_gamepad_codes.cpp` checks SDL code mapping and normalized
axis/token conversion. The shared gate compiles and runs the Elisa fixtures
with the stage1 compiler.

The focused fixture also models render frames with no simulation step and a
three-tick catch-up frame: a press edge remains pending until the first tick,
then clears after that tick while its held value survives later ticks. A
context-switch case verifies that an unconsumed gameplay edge cannot fire in
the UI context.

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

On every gamepad disconnect, the runtime clears held state before applying
whether another controller remains connected. This prevents a button held on
the removed controller from sticking in the shared logical gamepad state. The
portable regression test disconnects one of two logical controllers, checks
the release edge, then verifies input from the still-connected controller
works on the next frame.

The binding table is bounded at 48 entries so games can retain keyboard
aliases, controller movement, and distinct UI-context bindings. The capacity
fixture fills all 48 entries, verifies the next bind is rejected, and checks
that failed action-capacity insertion leaves the table unchanged.

The action-input fixture imports `src/runtime/public.elisa`, verifying that
ordinary games receive the event router from the default module set.

After adding `ActionInputRuntime`, the focused stage1 action-input executable
and `test/application_gamepad_codes.cpp` both passed. The SDL3/Metal
`scripts/application_native_smoke.py` also passed with an Elisa client draining
the Application queue through the runtime adapter, and
`DEVELOPER_DIR=/Library/Developer/CommandLineTools elisascript
scripts/check.elisascript` passed the full suite, including Godot and both proof
suites. A physical controller was not attached, so device delivery remains
verified through SDL mapping tests rather than hardware input.
