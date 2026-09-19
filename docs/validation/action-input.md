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

The fixture `test/action_input.elisa` covers analog dead-zone filtering,
pressed/held/released transitions, context isolation, chord activation, and
disconnect cleanup. The shared gate compiles and runs this fixture with the
stage1 compiler.

The native Wicked gate also runs `native/action_input_bridge.h`: SDL3 keyboard
events become portable action edges, analog values honor dead zones, focus loss
clears held state, and the adapter supports reconnectable device slots. The
native adapter never exposes SDL codes to gameplay.
