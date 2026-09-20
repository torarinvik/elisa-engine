# Persistent native host validation

The reusable SDL3/Wicked host in `native/native_application.h` drives the
maze through the Elisa game API. `native/live_game_probe.h` owns the persistent
loop: it polls real SDL key events, applies pause/resume and restart commands,
advances the fixed simulation clock, renders continuously, and exits only
after a close event.

The deterministic self-test injects the same event shapes used by the real
input path. It verifies two pause toggles, one world restart, one movement,
at least one fixed simulation tick, and a close request before returning.

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
`restarted`, `fixed_ticks=2`, `close requested`, and exited zero through the
ordered shutdown boundary. The host lifecycle probe also registers
reverse-order shutdown hooks and an RAII callback scope before each repeated
hidden host shutdown. Hooks run before GPU/audio/window teardown, and callback
admission is closed before that boundary. The PNG is a runtime artifact; the
self-test also requires a graphics session because it exercises the visible
host path.
