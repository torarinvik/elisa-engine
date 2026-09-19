# Persistent native host validation

The persistent mode uses the reusable SDL3/Wicked host and keeps gameplay
authority in the Elisa C ABI. It starts a visible window when
`ELISA_PERSISTENT_HOST=1`, handles W/A/S/D movement, P pause/resume, R restart,
and exits on the SDL close request. No frame count or manifest movement rule is
used.

The reproducible event-loop check is:

```sh
ELISA_PERSISTENT_HOST=1 \
ELISA_PERSISTENT_SELF_TEST=1 \
build/wicked-native-probe "$PWD/../WickedEngine/WickedEngine" \
  "$PWD/backends/scene_manifest.txt" "$PWD/build/persistent-selftest.png" alwaysactive
```

It exited `0` after logging pause, resume, restart, and close. The interactive
visible-window path remains hardware-dependent in this headless session and is
therefore not marked as the complete F02 milestone. The known Wicked global
worker shutdown limitation remains tracked under F05.
