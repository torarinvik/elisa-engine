# Native resource handles and retirement validation

`NativeResourceRegistry` and `NativeVoiceRegistry` issue monotonically unique
owner identities per registry, so two registries wrapping the same Wicked scene
or audio service reject each other's handles. Both registries are non-copyable
and non-movable, keeping identity attached to the object that minted each
handle.

Resource retirement uses a fixed 64-entry queue; voice retirement uses a fixed
queue bounded by the service's voice capacity. A full resource queue rejects a
deferred destruction before changing the resource's live state. The native
probe first fills a 64-slot Wicked mesh pool and checks the 65th creation is
rejected, then verifies all 64 slots return to the scene baseline. It also fills
the retirement queue with live cube entities, checks the 65th request is
rejected atomically, verifies nothing is collected before its submission
serial, then collects and reuses the capacity while returning the scene object
count to baseline. Existing checks cover logical invalidation, fence-delayed
texture and voice release, slot reuse with a new generation, failed creation,
and resource pressure.

The voice probe also forces generation exhaustion after playback starts. The
adapter stops that untracked voice before returning an invalid handle, so the
service does not retain an orphaned playback slot.

Validation on the pinned SDL3/Wicked Metal build:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 ~/.local/bin/elisascript scripts/wicked_probe.elisascript
```

Result: exit status 0; the resource and voice probes passed in both native
frame runs, determinism and scene topology matched, and frame time stayed
within budget. Production imported-mesh upload and material binding are tracked
separately under A05, R02, and R04.
