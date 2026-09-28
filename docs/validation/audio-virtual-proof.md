# Audio voice virtualization proof

`proof/audio_virtual.elisa` proves the slot bounds of `AudioVirtual`
(`src/audio/virtual.elisa`), the policy behind voice stealing and
virtualization from 616bff3. It uses the `../elisa-engine-proof` prover.

## Contracts

- `live` ensures `not result or slot < MAX_SOUNDS`. Only an in-range slot can
  report a live sound, so callers that guard with `live` may index the pool.
- `position` ensures `result == 0 or (slot < MAX_SOUNDS and result <
  pool.frames[slot])`. A realized voice starts inside its clip.
- `next_virtual` ensures `result <= MAX_SOUNDS`: a slot or the sentinel. Its
  loop states `invariant best <= MAX_SOUNDS`.
- `outranks` requires `slot < MAX_SOUNDS`.

## Bug found by the proof

`position` read `length - 1` and `elapsed % length` with no zero check. For a
zero-length clip, a looped sound trapped on the modulo, and a one-shot sound
wrapped to `u64` max. It now returns 0 when `length == 0`.

## Prover work

Proving these needed several prover fixes in `../elisa-engine-proof`, each
with accepted and rejected examples and an `AUDIT.md` entry:

- Disequality tightening: `length != 0` gives `length >= 1`.
- Conditional-equality splitting.
- Place aliases.
- Global-constant retention, including constants named only in tail-loop
  invariants.
- Branch joins that keep facts both arms prove.

The join work also found and fixed a soundness bug that was present at
6930911. A self-referential rebind (`b <- b + 1`) inside an arm let facts about
the old `b` reach the join, which proved a false `result <= 8`.

## Compiler catch-up

The current compiler (068a37b3) now rejects three patterns the engine used:

- `PhysicsRuntime::poll_contacts`, `service_poll_contacts` and
  `RuntimeServices::physics_poll_contacts` wrote through a readonly
  `ContactBuffer&`. They now take `mutable ContactBuffer&`, and every caller
  already passed a mutable local.
- `PrefabWorld::InstancePool()` moved its linear array into the struct without
  `move`. It is now written `InstancePool{instances: move instances}`.
- `WorldEffects` called its private `remove_at`. The checker resolved that
  call to `WorldAudio::remove_at`, which returns an error union, and rejected
  it. The helper is now `remove_effect_at`. This is a compiler name-resolution
  bug, left for the compiler session.

## Evidence

- `elisa-proof --json proof/audio_virtual.elisa`: 109 obligations, 109 proven,
  0 failed, 109 certificates replayed, 0 gaps.
- Audio unit tests passed: `audio_virtual`, `audio_mixer`, `audio_policy`,
  `audio_events`.
- A compile sweep of every `test/*.elisa` found no remaining rule errors.
- Full `scripts/check.elisascript` passed in a detached worktree, including every `proof/*.elisa` report.
- The compiler is `../Elisa-compiler` at 068a37b3, run with
  `ELISA_ALLOW_STALE_STAGE1=1`.

## Remaining

The pool's field arrays are not tied to one another by any invariant. For
example, nothing states that a real voice is live. Those properties are
tested, not proved.
