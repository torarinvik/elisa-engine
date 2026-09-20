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

`bind_checked` exposes the same validation as a non-throwing `BindResult`, so
projects can assemble bindings in ordinary Elisa loops without carrying
fallible-call state through helper functions. A rejected binding leaves both
the binding count and action-state slots unchanged. `bind` remains available
when callers prefer typed `InputError` propagation.

Multiple bindings for one action are aggregated: releasing one binding keeps
the action down while another remains held, and the action releases only when
the last held binding is released or disconnected. The reported value is the
strongest remaining binding value, and frame boundaries clear edges while
preserving held values. The fixture `test/action_input.elisa` covers this in
both release orders and across device disconnects, alongside analog dead-zone
filtering, single-binding pressed/held/released transitions, context isolation,
chord activation, and disconnect cleanup. The shared gate compiles and runs
this fixture with the stage1 compiler.

The native Wicked gate also runs `native/action_input_bridge.h`: SDL3 keyboard
events become portable action edges, analog values honor dead zones, and focus
loss clears transient state without marking connected devices unplugged. Its
probe covers alternate bindings, per-device disconnect, and held values across
frames. The native adapter never exposes SDL codes to gameplay.
