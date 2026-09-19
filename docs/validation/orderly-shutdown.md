# Orderly native shutdown boundary

`NativeApplication::shutdown()` owns the host-local shutdown order: it waits
for the Wicked graphics device, destroys the Wicked application while SDL is
still alive, releases FAudio through the pinned `wi::audio::Shutdown()` hook,
detaches and destroys the SDL3 window, and quits SDL. The finite and persistent
probe branches both return through this path.

Validation command:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 ELISA_ORDERLY_SHUTDOWN=1 \
elisascript scripts/wicked_probe.elisascript
```

Result: both native render passes completed, printed `orderly native shutdown
passed`, and exited zero. The pinned worker systems still have no general
callback-drain API, and repeated restart leak accounting remains open under F05.
