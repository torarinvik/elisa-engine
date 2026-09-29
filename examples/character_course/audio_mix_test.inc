# Mixer moods and weighted jump variants for the character-course self-test,
# included in the CharacterCourse private section after `pointer_test.inc`.
        def gains_near(left: AudioMixer::Snapshot, right: AudioMixer::Snapshot) -> bool:
            ElisaMath::fabsf(left.music - right.music) < 0.001 and ElisaMath::fabsf(left.effects - right.effects) < 0.001 and ElisaMath::fabsf(left.ui - right.ui) < 0.001

        def heard_is(sounds: CourseSounds::Sounds&, now_ms: u64, expected: AudioMixer::Snapshot) -> bool:
            heard: AudioMixer::Snapshot = CourseSounds::heard(sounds, now_ms)
            gains_near(heard, expected)

        # Before the asset loads every mood is full gain. The authored moods
        # duck the music for pause and further for the menu; Ui stays full.
        def moods_test() -> bool:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            unloaded: AudioMixer::Snapshot = CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_MENU)
            return false if not gains_near(unloaded, AudioMixer::snapshot(1.0, 1.0, 1.0)) or not CourseSounds::define_assets(&sounds)
            play: AudioMixer::Snapshot = CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_PLAY)
            paused: AudioMixer::Snapshot = CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_PAUSED)
            menu: AudioMixer::Snapshot = CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_MENU)
            mapped: bool = CourseSounds::mood_for(true, true) == CourseSounds::MOOD_MENU and CourseSounds::mood_for(false, true) == CourseSounds::MOOD_PAUSED and CourseSounds::mood_for(false, false) == CourseSounds::MOOD_PLAY
            ducked: bool = menu.music < paused.music and paused.music < play.music and paused.effects < play.effects
            mapped and ducked and play.ui == 1.0 and paused.ui == 1.0 and menu.ui == 1.0

        # Without audio the ramp still runs: pause at 1000 ms reaches the
        # paused gains at 1250; repeating the mood keeps that ramp; opening
        # the menu mid-ramp starts from the gains heard then.
        def ramp_test() -> bool:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return false if not CourseSounds::define_assets(&sounds)
            play: AudioMixer::Snapshot = CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_PLAY)
            paused: AudioMixer::Snapshot = CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_PAUSED)
            menu: AudioMixer::Snapshot = CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_MENU)
            steady: bool = CourseSounds::mix(&sounds, CourseSounds::MOOD_PLAY, 500) and heard_is(&sounds, 500, play)
            _ = CourseSounds::mix(&sounds, CourseSounds::MOOD_PAUSED, 1000)
            halfway: AudioMixer::Snapshot = CourseSounds::heard(&sounds, 1125)
            _ = CourseSounds::mix(&sounds, CourseSounds::MOOD_PAUSED, 1200)
            fades: bool = heard_is(&sounds, 1000, play) and halfway.music < play.music and halfway.music > paused.music and heard_is(&sounds, 1250, paused)
            _ = CourseSounds::mix(&sounds, CourseSounds::MOOD_MENU, 1125)
            steady and fades and heard_is(&sounds, 1125, halfway) and heard_is(&sounds, 1375, menu)

        # Live audio: every mood reaches the buses, jump variants split 3:1,
        # a reset replays the same variant for the same tick, and stop
        # restores full gains.
        def weighted_jump_test(session: mutable RuntimeServices::Session&) -> i32:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return 168 if CourseSounds::start(&sounds, session) != CourseSounds::STARTED
            applied: bool = CourseSounds::mix(&sounds, CourseSounds::MOOD_MENU, 10) and CourseSounds::mix(&sounds, CourseSounds::MOOD_PAUSED, 20) and CourseSounds::mix(&sounds, CourseSounds::MOOD_PLAY, 30)
            return 168 if not applied
            common: usize =
                for tick in 1..<301 |count: usize = 0, sounds| -> count:
                    fired: i32 = CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, tick.u64())
                    count <- count + 1 if fired != CourseSounds::FIRE_FAILED and CourseSounds::last_variant(&sounds) == 0
            return 169 if common < 195 or common > 255
            # Each of the 300 jumps started a voice and logged one decision;
            # the first 256 were sealed and saved as the session's chunk 0.
            return 169 if CourseSounds::decisions(&sounds) != 300
            return 169 if RuntimeDecisionSessionStore::manifest().chunks != 1
            _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 233)
            first: u32 = CourseSounds::last_variant(&sounds)
            CourseSounds::reset_scene(&sounds)
            _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 233)
            return 170 if CourseSounds::last_variant(&sounds) != first
            return 171 if not CourseSounds::stop(&sounds) or not audio_idle(session)
            # Stopping saved the partial second chunk, so the whole session is on disk.
            last_chunk: RuntimeDecisionLogStore::Read = RuntimeDecisionSessionStore::load_chunk(1)
            return 169 if RuntimeDecisionSessionStore::manifest().chunks != 2 or last_chunk.stored != RuntimeDecisionLogStore::Stored.Ok or last_chunk.loaded.log.count < 44
            return 169 if not RuntimeDecisionSessionStore::clear()
            0

        # Fires 300 jumps from tick `first`; returns a hash of the variants.
        def jump_run(sounds: mutable CourseSounds::Sounds&, first: u64) -> u64:
            for tick in 0..<300 |hash: u64 = 17, sounds| -> hash:
                _ = CourseSounds::fire(sounds, CourseSounds::EVENT_JUMP, first + tick.u64())
                hash <- hash * 31u64 + CourseSounds::last_variant(sounds).u64()

        # A recorded 300-jump session, saved on stop, replays from disk with
        # the same variants and no divergence; a shifted replay diverges.
        def replay_test(session: mutable RuntimeServices::Session&) -> i32:
            recorded: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return 230 if CourseSounds::start(&recorded, session) != CourseSounds::STARTED
            original: u64 = jump_run(&recorded, 1)
            return 230 if not CourseSounds::stop(&recorded)
            replayed: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return 231 if CourseSounds::start(&replayed, session) != CourseSounds::STARTED or not CourseSounds::replay_saved(&replayed)
            again: u64 = jump_run(&replayed, 1)
            return 231 if again != original or CourseSounds::replay_diverged_at(&replayed) != 0 - 1 or CourseSounds::decisions(&replayed) != 300
            return 231 if not CourseSounds::stop(&replayed)
            shifted: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return 232 if CourseSounds::start(&shifted, session) != CourseSounds::STARTED or not CourseSounds::replay_saved(&shifted)
            _ = jump_run(&shifted, 2)
            return 232 if CourseSounds::replay_diverged_at(&shifted) != 2
            return 232 if not CourseSounds::stop(&shifted) or not RuntimeDecisionSessionStore::clear() or not audio_idle(session)
            0

        def cue_gains(sounds: CourseSounds::Sounds&, course: u64, victory: u64) -> bool:
            CourseSounds::music_gain_permille(sounds, CourseSounds::MUSIC_COURSE) == course and CourseSounds::music_gain_permille(sounds, CourseSounds::MUSIC_VICTORY) == victory

        # Without audio the authored crossfade still runs: a win at 1000 ms
        # fades the course loop out over 600 ms while victory swells in over
        # 1500 ms, repeating the cue keeps the fade, and restart cuts back.
        def crossfade_test() -> bool:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return false if not CourseSounds::define_assets(&sounds) or not CourseSounds::restart_music(&sounds)
            _ = CourseSounds::play_music(&sounds, CourseSounds::music_for(false), 1000)
            steady: bool = cue_gains(&sounds, 1000, 0)
            _ = CourseSounds::play_music(&sounds, CourseSounds::music_for(true), 1000)
            _ = CourseSounds::play_music(&sounds, CourseSounds::music_for(true), 1300)
            halfway: bool = cue_gains(&sounds, 500, 200)
            _ = CourseSounds::play_music(&sounds, CourseSounds::music_for(true), 2500)
            won: bool = cue_gains(&sounds, 0, 1000)
            restarted: bool = CourseSounds::restart_music(&sounds) and cue_gains(&sounds, 1000, 0)
            steady and halfway and won and restarted

        # Live audio: both loops stream at once, the crossfade reaches their
        # gains, and stop releases both streams.
        def music_stream_test(session: mutable RuntimeServices::Session&) -> bool:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return false if CourseSounds::start(&sounds, session) != CourseSounds::STARTED
            faded: bool = CourseSounds::play_music(&sounds, CourseSounds::MUSIC_VICTORY, 10) and CourseSounds::play_music(&sounds, CourseSounds::MUSIC_VICTORY, 700)
            restarted: bool = CourseSounds::restart_music(&sounds) and CourseSounds::music_position(&sounds) < 22050
            # Two loops stream together without starving either ring.
            calm: bool = CourseSounds::music_status(&sounds).underrun_frames == 0 and CourseSounds::victory_status(&sounds).underrun_frames == 0
            CourseSounds::stop(&sounds) and faded and restarted and calm and audio_idle(session)

        # Hot reload without audio: a changed asset swaps its moods in, the
        # same bytes reload nothing, and a rejected or missing file leaves the
        # loaded asset alone.
        def reload_test() -> bool:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return false if not CourseSounds::define_assets(&sounds)
            original: AudioMixer::Snapshot = CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_PAUSED)
            same: bool = CourseSounds::reload_events(&sounds, "sounds/events.sfx") == CourseSounds::UNCHANGED
            swapped: bool = CourseSounds::reload_events(&sounds, "sounds/events_alt.sfx") == CourseSounds::RELOADED
            ducked: AudioMixer::Snapshot = CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_PAUSED)
            bad: bool = CourseSounds::reload_events(&sounds, "sounds/events_bad.sfx") == CourseSounds::REJECTED and CourseSounds::reload_events(&sounds, "sounds/missing.sfx") == CourseSounds::REJECTED
            kept: bool = gains_near(CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_PAUSED), ducked)
            back: bool = CourseSounds::reload_events(&sounds, "sounds/events.sfx") == CourseSounds::RELOADED and gains_near(CourseSounds::mood_snapshot(&sounds, CourseSounds::MOOD_PAUSED), original)
            same and swapped and ducked.music < original.music and bad and kept and back

        # Live audio: a reload keeps the firing history and live voices of
        # surviving events, so a re-run tick still does not double-fire, and
        # stops the voice of an event the new asset removes. Polling is
        # throttled and reloads nothing while the file is unchanged.
        def reload_live_test(session: mutable RuntimeServices::Session&) -> bool:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return false if CourseSounds::start(&sounds, session) != CourseSounds::STARTED
            fired: bool = CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 5) == CourseSounds::PLAYED and CourseSounds::fire(&sounds, CourseSounds::EVENT_STEP, 5) == CourseSounds::PLAYED
            reloaded: bool = CourseSounds::reload_events(&sounds, "sounds/events_alt.sfx") == CourseSounds::RELOADED
            kept: bool = CourseSounds::event_voices(&sounds) == 1 and CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 5) == CourseSounds::DUPLICATE
            removed: bool = CourseSounds::fire(&sounds, CourseSounds::EVENT_STEP, 6) == CourseSounds::FIRE_FAILED
            restored: bool = CourseSounds::reload_events(&sounds, "sounds/events.sfx") == CourseSounds::RELOADED and CourseSounds::fire(&sounds, CourseSounds::EVENT_STEP, 6) == CourseSounds::PLAYED
            throttled: bool = CourseSounds::poll_reload(&sounds, 100) == CourseSounds::UNCHANGED and CourseSounds::poll_reload(&sounds, 700) == CourseSounds::UNCHANGED
            CourseSounds::stop(&sounds) and fired and reloaded and kept and removed and restored and throttled and audio_idle(session)

        # Live audio: the guide's authored foot plants sound when its clip
        # passes them, once per tick, and a reload that drops the bindings
        # silences them.
        def anim_marker_test(session: mutable RuntimeServices::Session&) -> bool:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return false if CourseSounds::start(&sounds, session) != CourseSounds::STARTED
            quiet: bool = CourseSounds::animation_markers(&sounds, 0, 400, 1) == 0
            right: bool = CourseSounds::animation_markers(&sounds, 400, 600, 2) == 1
            again: bool = CourseSounds::animation_markers(&sounds, 400, 600, 2) == 0
            left: bool = CourseSounds::animation_markers(&sounds, 600, 1000, 3) == 1
            both: bool = CourseSounds::animation_markers(&sounds, 1000, 2100, 4) == 1
            dropped: bool = CourseSounds::reload_events(&sounds, "sounds/events_alt.sfx") == CourseSounds::RELOADED and CourseSounds::animation_markers(&sounds, 2100, 2600, 5) == 0
            CourseSounds::stop(&sounds) and quiet and right and again and left and both and dropped and audio_idle(session)

        def audio_mix_test(session: mutable RuntimeServices::Session&) -> i32:
            return 166 if not moods_test()
            return 167 if not ramp_test()
            return 196 if not crossfade_test()
            return 197 if not music_stream_test(session)
            return 198 if not reload_test()
            return 199 if not reload_live_test(session)
            return 200 if not anim_marker_test(session)
            weighted: i32 = weighted_jump_test(session)
            return weighted if weighted != 0
            replay_test(session)
