# Scene and owner teardown for CourseSounds. Kept separate from playback so
# cleanup policy stays reviewable without expanding the asset/event module.

        # Restart or load: silence every event sound and forget the old
        # timeline so its ticks cannot suppress or double the new ones.
        def reset_scene(sounds: mutable Sounds&) -> bool:
            stopped: bool =
                for slot in 0..<AudioEvents::MAX_INSTANCES |ok: bool = true, sounds| -> ok:
                    stopped: bool = stop_slot(sounds, slot)
                    ok <- stopped and ok
            return false if not stopped
            AudioEvents::reset(&sounds.board)
            AudioTriggers::reset(&sounds.triggers.footfalls, &sounds.triggers.impacts)
            sounds.tracked <- false
            true

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
            stopped_ambience: bool = stop_owned_voice(&sounds.ambience)
            ok <- stopped_ambience and ok
            stopped_music: bool = stop_owned_stream(&sounds.music)
            ok <- stopped_music and ok
            stopped_victory: bool = stop_owned_stream(&sounds.victory)
            ok <- stopped_victory and ok
            sounds.score <- AudioMusic::Transition()
            if sounds.ambience_sound < AudioVirtual::MAX_SOUNDS:
                removed: bool = catch AudioVirtual::remove(&sounds.sounds_virtual, sounds.ambience_sound):
                    ok: true
                    AudioVirtual::VirtualSoundError.UnknownSound: true
                    error failure: false
                ok <- removed and ok
                sounds.ambience_sound <- AudioVirtual::MAX_SOUNDS if removed
            resumed: bool = catch AudioRuntime::set_bus_paused(AudioRuntime::Bus.Effects, false):
                ok: true
                error failure: false
            ok <- resumed and ok
            released: mutable bool = release_owned_clip(&sounds.jump_a)
            released <- release_owned_clip(&sounds.jump_b) and released
            released <- release_owned_clip(&sounds.land) and released
            released <- release_owned_clip(&sounds.win) and released
            released <- release_owned_clip(&sounds.fall) and released
            released <- release_owned_clip(&sounds.save) and released
            released <- release_owned_clip(&sounds.footstep) and released
            released <- release_owned_clip(&sounds.wind) and released
            ok <- released and ok
            sounds.mix <- CourseSoundMix::settled_mix()
            sounds.mood <- MOOD_PLAY
            full: AudioMixer::Snapshot = mood_snapshot(sounds, MOOD_PLAY)
            gains_restored: bool = CourseSoundMix::apply_gains(full)
            ok <- gains_restored and ok
            sounds.live <- not ok
            released and ok

        def stop_owned_voice(voice: mutable AudioRuntime::VoiceHandle&) -> bool:
            stopped: bool = stop_voice(voice)
            if stopped:
                voice <- AudioRuntime::empty_voice()
            stopped

        def stop_owned_stream(stream: mutable AudioRuntime::StreamHandle&) -> bool:
            stopped: bool = stop_stream(stream)
            if stopped:
                stream <- AudioRuntime::empty_stream()
            stopped

        def release_owned_clip(clip: mutable AudioRuntime::ClipHandle&) -> bool:
            released: bool = release(clip)
            if released:
                clip <- AudioRuntime::empty_clip()
            released
