# Animation event queue bounds

The public AnimState exposes event_count; a corrupt or oversized count must not
permit reading past its fixed eight-element event storage. anim_event_at now
refuses index >= MAX_QUEUED_EVENTS as well as index >= event_count. Valid counts
and the existing -1 refusal sentinel retain their behavior. Two insertion paths
bind their checked index inside named regions. The array extent and private
write guard reference the public capacity directly, avoiding a nested constant
alias the prover cannot yet import. The physical extent remains eight.

Runtime tests cover all eight successful insertions, the last valid read, full
queue insertion refusal, capacity-index refusal and an oversized public count.
An old-source control traps with exit -5 at the oversized read; the repaired
implementation passes. The 215-test gate passes with zero cached compiles in
62 seconds. Installed snapshot is documentation revision 665f40d7, product built
at 8006b660 (unchanged product/runtime hashes in compiler-8006b660-install.md).
Source-length and module-hygiene policies pass.

The implementation-linked audio_anim_events proof changes from 45/58 with
13 findings to **53/57**, **zero replay gaps**, and four findings. Remaining:
Playback tick float conversion, anim_play resource-summary encoding, anim_tick
callee summary, and consume_state dispatch indexing. The inventory changes as
previously unsupported array accesses become modeled; no contracts or proof rows
were removed. Full uncached sweep remains **69/73** with the same four failing
modules. The full plan remains open.

Evidence in build/validation:
- audio-anim-current.json
- audio-anim-bounded-slots.json
- audio-anim-bounded-runtime.log
- audio-anim-bounded-sweep.log
- anim-old-count-control.elisa and anim-old-count-control.log
