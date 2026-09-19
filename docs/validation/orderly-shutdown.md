# Orderly native shutdown boundary

`NativeApplication::shutdown()` now owns the host-local shutdown order: it
waits for the Wicked graphics device, detaches the platform window, destroys
the SDL3 window, and quits SDL. The finite probe can exercise this path with
`ELISA_ORDERLY_SHUTDOWN=1`.

The pinned Wicked build still does not expose a public shutdown operation for
its process-wide worker systems. On the current Metal checkout the optional
mode does not complete reliably after the full graphics probe, so the default
gate retains its explicit `_Exit` diagnostic boundary. F05 remains unchecked;
the next step is to identify and patch the pinned worker lifetime rather than
claiming normal process teardown.
