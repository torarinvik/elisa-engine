# Session-recording round trip for the character-course self-test, included
# in the CharacterCourse private section after `look_test.inc`. Synthetic
# Application events drive the course's own bindings through the recording
# path, the log is saved and reloaded through user data, and a replay into a
# freshly bound input must match every tick.
        def recording_key(code: i64, down: bool) -> Application::InputEvent:
            Application::InputEvent{kind: Application::InputEventKind.Key, device: 0, code: code,
                value: 1.0 if down else 0.0, pressed: down, released: not down, chord_down: false,
                connected: false, device_slot: 0, physical_code: 0}

        def recording_digest(input: ActionInput::Input&) -> i64:
            forward: i64 = 1 if ActionInput::action_down(input, CourseControls::ACTION_FORWARD) else 0
            jump: i64 = 2 if ActionInput::action_pressed(input, CourseControls::ACTION_JUMP) else 0
            left: i64 = 4 if ActionInput::action_down(input, CourseControls::ACTION_LEFT) else 0
            forward + jump + left

        # The event for tick `t`: hold W over ticks 1-5, tap Space on 3, hold A
        # from 4 until focus is lost on 7.
        def recording_event(t: i64, keymap: CourseControls::Keymap&) -> Application::InputEvent:
            return recording_key(keymap.forward, true) if t == 1
            return recording_key(keymap.jump, true) if t == 3
            return recording_key(keymap.left, true) if t == 4
            return recording_key(keymap.forward, false) if t == 5
            if t == 7:
                lost: mutable Application::InputEvent = recording_key(0, false)
                lost.kind <- Application::InputEventKind.FocusLost
                return lost
            recording_key(0, false)

        def recording_test() -> i32:
            keymap: CourseControls::Keymap = CourseControls::defaults()
            pads: CoursePad::Pad = CoursePad::defaults()
            live: mutable ActionInput::Input = ActionInput::Input()
            return 250 if not CourseControls::bind(&live, &keymap, &pads)
            log: mutable ActionInputReplay::Log = ActionInputReplay::Log()
            digests: mutable i64[10] = zeroed
            for t in 0..<10 |live, log, digests|:
                ActionInput::begin_frame(&live)
                event: Application::InputEvent = recording_event(t.i64(), &keymap)
                if event.code != 0 or event.kind != Application::InputEventKind.Key:
                    _ = ActionInputRecording::record_application_event(&log, &live, t.i64(), event)
                digests[t] <- recording_digest(&live)
            return 251 if digests[2] != 1 or digests[3] != 3 or digests[6] != 4 or digests[7] != 0
            no_pointer: ActionPointerReplay::Log = ActionPointerReplay::Log()
            return 252 if ActionInputStore::save_recording(RECORDING_KEY, &log, &no_pointer) != ActionInputStore::Stored.Ok
            loaded: mutable ActionInputReplay::Log = ActionInputReplay::Log()
            loaded_pointer: mutable ActionPointerReplay::Log = ActionPointerReplay::Log()
            return 253 if ActionInputStore::load_recording(RECORDING_KEY, &loaded, &loaded_pointer) != ActionInputStore::Stored.Ok
            return 254 if loaded.count != log.count or not ActionInputReplay::replay_trusted(&loaded)
            replayed: mutable ActionInput::Input = ActionInput::Input()
            return 250 if not CourseControls::bind(&replayed, &keymap, &pads)
            cursor: mutable usize = 0
            mismatches: mutable i64 = 0
            for t in 0..<10 |replayed, cursor, mismatches|:
                ActionInput::begin_frame(&replayed)
                cursor <- ActionInputReplay::replay_tick(&loaded, &replayed, t.i64(), cursor)
                mismatches <- mismatches + 1 if recording_digest(&replayed) != digests[t]
            return 255 if mismatches != 0 or cursor != loaded.count
            0
