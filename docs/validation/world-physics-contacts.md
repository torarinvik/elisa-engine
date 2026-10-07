# Checked physics contact identity — 2026-10-07

Jolt contact records contain native participant keys. Those numbers cannot be
cast to World entity IDs. `PhysicsRuntime::body_contact_key` and its service
variant query a body through its checked world/slot/generation handle;
`RuntimeServices::physics_body_contact_key` adds the session checks. The C ABI
checks owner/world state and body generation before writing the key.

`WorldPhysics::body_contact_key` queries an existing binding without exposing
its native handle. `WorldPhysics::contact_reference` joins a participant key
against the binding owner and verifies the resulting reference is live in the
supplied World. The World epoch and entity generation remain part of that
check. Unknown/unbound participants return `{world_epoch: 0, id: 0}`. Stale
physics handles and invalid matching World references remain explicit errors.
Keys are runtime identities for joining contact records, not serialized IDs.

## Native evidence

`world-physics-pose-smoke` passed on SDL3/Jolt/Metal with silent audio fallback,
status 0, 52.021 seconds including compile/link. It verifies:

- A current body's nonzero key resolves to its exact World reference.
- Zero and an unbound old key produce the invalid reference.
- A different World with the same numeric entity ID is rejected by epoch.
- A despawned World entity is rejected while its body is still bound.
- Unbinding and creating a replacement body yields a different key; the old
  key stays unresolved even when the binding table is nonempty again.
- Restarting the physics session rejects the previous binding as `StaleWorld`.

Existing pose, hierarchy, kinematic and cleanup assertions also passed, and
its hierarchy capture changed across 22860 pixels at 640×400. Results are in
`build/native-smoke/world-physics-pose-smoke.json` and its log. These are native
outcome assertions; they do not constitute a whole-program formal proof.

## Scheduled contact delivery

`WorldContactEvents::enqueue` consumes copied records during Physics while
holding the scheduler's World read token. It requires an Audio subscription,
filters everything except Added non-trigger contacts with distinct nonzero
keys, and resolves each participant through the checked binding owner. It
emits AudioCue events with the current World position. The report separately
counts emitted cues, unbound participants, rejected references, full-queue
drops, filtered contacts and native queue overflow. The existing native queue
remains the worker handoff mechanism.

The focused native gate passed with the scheduled adapter (60.727 seconds).
It checks wrong phase, missing subscription, a conflicting write token,
oversized input, trigger/persisted/removed filtering and deterministic
saturation: 63 preexisting events plus 64 copied records produce one new cue,
63 queue drops and 64 unbound participants. Native overflow 7 remains visible.
The Audio dispatch delivers exactly 64 events, with the new cue retaining its
checked reference, payload and current World position.

`proof/world_contact_policy.elisa` proves 8/8 obligations for the implementation's
eligibility predicate, including trigger, non-Added and duplicate-key rejection.
Policy SHA-256: `644a9c0180ad38bde9b25dbee1e495f5be66e01c998c892b78e0fce67ab05c10`.
This proof covers classification; it does not prove native callback ownership.

The same gate also rejects a matching body key against a different World epoch,
reports it as rejected and emits no cue.

`CourseContactAudio::step` runs after every completed fixed step in the live
course. It synchronizes crate poses under a World write token, releases that
token, polls the native queue, enqueues checked cues under read access and
advances the same frame to Audio for dispatch. It passes the cue payload to
CourseSounds' existing landing sound, including silent playback fallback.
Its automated verdict requires emitted/delivered/audio-attempt counts to be
positive and equal, with zero stale rejection and scheduled/native overflow.

The first cell-pilot run passed the contact assertions, then returned 115
from its existing jump-height check. Waiting for grounded state was insufficient.
A diagnostic established that Space reached the jump action and the native
controller received speed 5 while reporting OnGround. The velocity trace found
`vy=-6.231466` before movement and `vy=-1.231466` afterward: the requested jump
was consumed by residual fall velocity at first ground contact. The character's
peak moved only from -0.035638 to -0.020000. The pre-fix record is retained in
`build/validation/character-course-jump-before.{json,log}`.

The native character adapter now ensures a grounded jump reaches at least the
requested vertical speed relative to its support surface. It preserves the
backend's horizontal velocity and stronger upward momentum, and checks finite
velocity/target values before applying the correction. The implementation's
`character_jump_policy.h` passes 144 finite cases plus the observed regression;
disabling its clamp makes the negative control fail with status 1. The check is
part of `scripts/native_unit_tests.py`. This is native policy testing, not a
whole-program formal proof. The original 0.15 m cell-pilot rise threshold remains.

The main live-input course passed before this jump correction (status 0,
245.756 seconds), including the real-contact verdict, focus recovery, small-menu
paging, traversal, win, restart and fall. The native physics-runtime-query gate passed with the correction (53.903s).
The updated WorldPhysics gate also passed (60.727s), including its character
movement/slope/step/corner tests and the scheduled contact assertions.
The corrected cell pilot passed (196.867s), recording a start height of
-0.035638 and peak 1.153066, with the Space action observed and its unchanged
0.15 m rise requirement satisfied. Its streaming, memory, contact-audio and
teardown assertions also passed. The updated main live-input route also passed (203.575s), including focus,
menu paging, traversal, win, restart, fall and presentation capture checks. Physical listening remains
separate acceptance.
