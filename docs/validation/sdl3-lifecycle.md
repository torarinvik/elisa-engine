# SDL3 suspension and frame pacing validation

`NativeApplication::WindowState` suspends simulation while the window is
unfocused, minimized, or has a zero pixel extent. `run_frame()` skips Wicked
presentation during suspension. `advance_fixed()` clears its accumulator and
does not run ticks, so time spent suspended cannot produce a catch-up burst
after restore.

Discrete gameplay events use the bounded `InputTickQueue` instead of mutating
the game from the SDL event callback. Fixed ticks drain actions in FIFO order;
overflow is reported, and the frame-pacing probe checks full-capacity
rejection and ring wraparound. The live-game probe confirms an SDL movement
event stays queued until a fixed tick advances Elisa.

The native lifecycle probe sends focus-lost, minimized, focus-gained, restored,
and pixel-size events. It checks that focus gain alone does not resume a
minimized window. The probe also injects a zero pixel width at the host-state
boundary, verifies that both presentation and four accumulated fixed steps are
skipped, then verifies a clean single-step resume with no backlog. Existing
checks still cover fullscreen transitions, high-DPI dimensions, and resize
serial behavior.

Validation on the pinned SDL3/Wicked Metal build:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 ~/.local/bin/elisascript scripts/wicked_probe.elisascript
```

Result: exit status 0; both native frame runs passed lifecycle, topology,
determinism, and frame-time checks. The persistent-host self-test also passed:
restart and movement were consumed by fixed ticks, then close shut down the
host cleanly. The zero extent is injected at the host boundary because the
hidden Metal test window cannot reliably request a true zero-sized drawable.
Wicked swapchain recreation after a real display-mode change remains open.
