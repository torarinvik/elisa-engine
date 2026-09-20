# Persistent native host validation

The reusable SDL3/Wicked host in `native/native_application.h` drives the
maze through the Elisa game API. `native/live_game_probe.h` owns the persistent
loop: it polls SDL key events, applies pause/resume controls, queues restart
and movement actions, advances those game actions on fixed simulation ticks,
renders continuously, and exits only after a close event.

The deterministic self-test injects the same event shapes used by the real
input path. It verifies two pause toggles, one restart consumed on a fixed
tick, one movement, fixed simulation ticks, and a close request before
returning. The regular native probe separately verifies that an SDL movement
event waits in the FIFO until a fixed tick consumes it.

Validation command:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_PERSISTENT_HOST=1 ELISA_PERSISTENT_SELF_TEST=1 \
build/wicked-native-probe \
  "$PWD/../WickedEngine/WickedEngine" \
  "$PWD/backends/scene_manifest.txt" \
  "$PWD/build/persistent-test.png" alwaysactive
```

Result on the pinned SDL3/Metal build: the host printed `paused`, `resumed`,
`restarted on fixed tick`, `fixed_ticks=4`, `close requested`, and exited zero
through the ordered shutdown boundary. The host lifecycle probe also registers
reverse-order shutdown hooks and an RAII callback scope before each repeated
hidden host shutdown. Hooks run before GPU/audio/window teardown, and callback
admission is closed before that boundary. The PNG is a runtime artifact; the
self-test also requires a graphics session because it exercises the visible
host path.
