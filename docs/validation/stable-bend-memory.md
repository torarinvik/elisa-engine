# Stable bend memory

`Ik::bend_memory()` creates caller-owned state for one limb. Call
`Ik::stable_bend` with the root-to-target direction and pole direction before
passing its result to `Pose::pose_correct_limb`. All vectors must share a
coordinate space. Reset memory after teleporting, changing spaces, or requesting
an intentional bend-side change.

The previous bend is parallel-transported by the shortest direction rotation.
Near-parallel hints retain that plane, rather than normalizing noise. Valid hints
are kept in the previous bend hemisphere. Invalid or zero target inputs do not
mutate memory. No allocation is required.

`test/ik_bend_main.elisa` passes 100 alternating noisy singular hints, a reversed
hint, 100 changing target directions, perpendicularity and zero-target retention.
Existing IK/FK blending tests also pass. This is solver evidence, not a native
fighter integration or a visual claim. Opposite target directions remain
ambiguous, and deliberate pole changes near the hemisphere boundary can still
need temporal rate limits. Joint limits, contact policy and gameplay integration
remain separate work.

## Pose and native integration

`Pose::pose_correct_limb_stable` wraps the rotational solve. It stages bend
memory and commits it only after a successful correction. Shape/index errors and
unreachable targets leave memory unchanged. Pure pose tests cover 100 repeated
near-full-extension frames, length preservation and rejected-target atomicity.

`test/render_scene_ik_fk_main.elisa` uses the stateful path on both cooked boxing
rigs. Each passes five IK/FK weights and 20 near-full-extension frames with noisy
singular hints. Actual Wicked scene joints match overridden poses within 0.2 mm.
Default audio was used. These are leg integration tests; arm motion, dynamic
contact policy and visually reviewed gameplay constraints remain outstanding.
