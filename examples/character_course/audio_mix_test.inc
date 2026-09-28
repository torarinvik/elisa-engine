# Mixer moods and weighted jump variants for the character-course self-test,
# included in the CharacterCourse private section after `pointer_test.inc`.
        def gains_near(left: AudioMixer::Snapshot, right: AudioMixer::Snapshot) -> bool:
            ElisaMath::fabsf(left.music - right.music) < 0.001 and ElisaMath::fabsf(left.effects - right.effects) < 0.001 and ElisaMath::fabsf(left.ui - right.ui) < 0.001

        def heard_is(sounds: CourseSounds::Sounds&, now_ms: u64, expected: AudioMixer::Snapshot) -> bool:
            heard: AudioMixer::Snapshot = CourseSounds::heard(sounds, now_ms)
            gains_near(heard, expected)

        # Pause ducks the music and the menu ducks it further; Ui stays full.
        def moods_test() -> bool:
            play: AudioMixer::Snapshot = CourseSounds::mood_snapshot(CourseSounds::MOOD_PLAY)
            paused: AudioMixer::Snapshot = CourseSounds::mood_snapshot(CourseSounds::MOOD_PAUSED)
            menu: AudioMixer::Snapshot = CourseSounds::mood_snapshot(CourseSounds::MOOD_MENU)
            mapped: bool = CourseSounds::mood_for(true, true) == CourseSounds::MOOD_MENU and CourseSounds::mood_for(false, true) == CourseSounds::MOOD_PAUSED and CourseSounds::mood_for(false, false) == CourseSounds::MOOD_PLAY
            ducked: bool = menu.music < paused.music and paused.music < play.music and paused.effects < play.effects
            mapped and ducked and play.ui == 1.0 and paused.ui == 1.0 and menu.ui == 1.0

        # Without audio the ramp still runs: pause at 1000 ms reaches the
        # paused gains at 1250; repeating the mood keeps that ramp; opening
        # the menu mid-ramp starts from the gains heard then.
        def ramp_test() -> bool:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            play: AudioMixer::Snapshot = CourseSounds::mood_snapshot(CourseSounds::MOOD_PLAY)
            paused: AudioMixer::Snapshot = CourseSounds::mood_snapshot(CourseSounds::MOOD_PAUSED)
            menu: AudioMixer::Snapshot = CourseSounds::mood_snapshot(CourseSounds::MOOD_MENU)
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
                for tick in 1..<201 |count: usize = 0, sounds| -> count:
                    fired: i32 = CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, tick.u64())
                    count <- count + 1 if fired != CourseSounds::FIRE_FAILED and CourseSounds::last_variant(&sounds) == 0
            return 169 if common < 130 or common > 170
            _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 233)
            first: u32 = CourseSounds::last_variant(&sounds)
            CourseSounds::reset_scene(&sounds)
            _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 233)
            return 170 if CourseSounds::last_variant(&sounds) != first
            return 171 if not CourseSounds::stop(&sounds) or not audio_idle(session)
            0

        def audio_mix_test(session: mutable RuntimeServices::Session&) -> i32:
            return 166 if not moods_test()
            return 167 if not ramp_test()
            weighted_jump_test(session)
