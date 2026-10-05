# Scene and owner teardown for CourseSounds. Kept separate from playback so
# cleanup policy stays reviewable without expanding the asset/event module.

        # Restart or load: silence every event sound and forget the old
        # timeline so its ticks cannot suppress or double the new ones.
        def reset_scene(sounds: mutable Sounds&) -> bool:
            stopped: bool =
                for slot in 0..<AudioEvents::MAX_INSTANCES |ok: bool = true, sounds| -> ok:
                    stopped: bool = stop_slot(sounds, slot)
                    ok <- stopped and ok
            AudioEvents::reset(&sounds.board)
            AudioTriggers::reset(&sounds.triggers.footfalls, &sounds.triggers.impacts)
            sounds.tracked <- false
            stopped

        # Attempt every cleanup so a failure cannot strand later resources.
        # Returns false if persistence or any native release failed.
        def stop(sounds: mutable Sounds&) -> bool:
            return true if not sounds.live
            ok: mutable bool = true
            if sounds.decisions.count > 0 and sounds.decisions.mode == RuntimeDecisionLog::Mode.Recording:
                saved: bool = RuntimeDecisionSessionStore::save_chunk(sounds.sealed_chunks, &sounds.decisions) == RuntimeDecisionLogStore::Stored.Ok
                ok <- saved and ok
            stopped_events: bool = reset_scene(sounds)
            ok <- stopped_events and ok
            stopped_ambience: bool = stop_voice(sounds.ambience)
            ok <- stopped_ambience and ok
            stopped_music: bool = stop_stream(sounds.music)
            ok <- stopped_music and ok
            stopped_victory: bool = stop_stream(sounds.victory)
            ok <- stopped_victory and ok
            sounds.victory <- AudioRuntime::empty_stream()
            sounds.score <- AudioMusic::Transition()
            sounds.ambience <- AudioRuntime::empty_voice()
            if sounds.ambience_sound < AudioVirtual::MAX_SOUNDS:
                removed: bool = catch AudioVirtual::remove(&sounds.sounds_virtual, sounds.ambience_sound):
                    ok: true
                    error failure: false
                ok <- removed and ok
            sounds.ambience_sound <- AudioVirtual::MAX_SOUNDS
            sounds.music <- AudioRuntime::empty_stream()
            sounds.live <- false
            resumed: bool = catch AudioRuntime::set_bus_paused(AudioRuntime::Bus.Effects, false):
                ok: true
                error failure: false
            ok <- resumed and ok
            released: mutable bool = release(sounds.jump_a)
            released <- release(sounds.jump_b) and released
            released <- release(sounds.land) and released
            released <- release(sounds.win) and released
            released <- release(sounds.fall) and released
            released <- release(sounds.save) and released
            released <- release(sounds.footstep) and released
            released <- release(sounds.wind) and released
            sounds.jump_a <- AudioRuntime::empty_clip()
            sounds.jump_b <- AudioRuntime::empty_clip()
            sounds.land <- AudioRuntime::empty_clip()
            sounds.win <- AudioRuntime::empty_clip()
            sounds.fall <- AudioRuntime::empty_clip()
            sounds.save <- AudioRuntime::empty_clip()
            sounds.footstep <- AudioRuntime::empty_clip()
            sounds.wind <- AudioRuntime::empty_clip()
            sounds.board <- AudioEvents::Board()
            sounds.triggers <- SoundEventAssets::Triggers()
            sounds.mix <- CourseSoundMix::settled_mix()
            sounds.mood <- MOOD_PLAY
            full: AudioMixer::Snapshot = mood_snapshot(sounds, MOOD_PLAY)
            gains_restored: bool = CourseSoundMix::apply_gains(full)
            ok <- gains_restored and ok
            released and ok
