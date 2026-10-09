# Gamepad button rebinding for the character-course self-test, included in
# the CharacterCourse private section after `pointer_test.inc`. Events are
# synthetic Elisa events; no physical controller was used.
        def pad_button(name: Application::GamepadButtonCode) -> i64:
            Application::gamepad_button_code(name)

        def pad_refused(values: i64[Save::MAX_FIELDS], count: usize) -> bool error[Save::SaveError]:
            blob: Save::Blob = try Save::save_write(values, count)
            result: CoursePad::LoadResult = CoursePad::decode(&blob)
            defaults: CoursePad::Pad = CoursePad::defaults()
            result.status == CoursePad::LoadStatus.Rejected and result.pad.jump == defaults.jump

        def pad_test() -> i32 error[Save::SaveError, UserData::UserDataError] can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                defaults: CoursePad::Pad = CoursePad::defaults()
                return 202 if not CoursePad::valid(&defaults)
                north: i64 = pad_button(Application::GamepadButtonCode.North)
                start: i64 = pad_button(Application::GamepadButtonCode.Start)
                moved: CoursePad::AssignOutcome = CoursePad::assign(defaults, CoursePad::SLOT_JUMP, north)
                return 203 if moved.result != CoursePad::AssignResult.Assigned or moved.pad.jump != north
                swapped: CoursePad::AssignOutcome = CoursePad::assign(moved.pad, CoursePad::SLOT_CROUCH, north)
                return 204 if swapped.result != CoursePad::AssignResult.Swapped or swapped.pad.jump != defaults.crouch or swapped.pad.crouch != north or not CoursePad::valid(&swapped.pad)
                reserved: CoursePad::AssignOutcome = CoursePad::assign(defaults, CoursePad::SLOT_JUMP, start)
                return 205 if reserved.result != CoursePad::AssignResult.Reserved
                invalid: CoursePad::AssignOutcome = CoursePad::assign(defaults, 3, north)
                return 206 if invalid.result != CoursePad::AssignResult.InvalidSlot
                unchanged: CoursePad::AssignOutcome = CoursePad::assign(defaults, CoursePad::SLOT_JUMP, defaults.jump)
                return 207 if unchanged.result != CoursePad::AssignResult.Unchanged
                # The rebound button drives the action and the old one does not.
                input: mutable ActionInput::Input = ActionInput::Input()
                keymap: CourseControls::Keymap = CourseControls::defaults()
                return 208 if not CourseControls::bind(&input, &keymap, &moved.pad)
                ActionInput::apply(&input, device_event(ActionInput::Device.Gamepad, defaults.jump, 1.0))
                old_button: bool = ActionInput::action_down(&input, CourseControls::ACTION_JUMP)
                ActionInput::apply(&input, device_event(ActionInput::Device.Gamepad, north, 1.0))
                new_button: bool = ActionInput::action_down(&input, CourseControls::ACTION_JUMP)
                return 209 if old_button or not new_button
                # A stored record round-trips, and bad records restore defaults.
                try CoursePad::save(&moved.pad)
                loaded: CoursePad::LoadResult = CoursePad::load()
                return 210 if loaded.status != CoursePad::LoadStatus.Loaded or loaded.pad.jump != north or loaded.pad.crouch != defaults.crouch
                try UserData::remove_blob(CoursePad::storage_key())
                missing: CoursePad::LoadResult = CoursePad::load()
                return 211 if missing.status != CoursePad::LoadStatus.Missing
                values: mutable i64[Save::MAX_FIELDS] = zeroed
                values[0] <- 2
                values[1] <- defaults.jump
                values[2] <- defaults.crouch
                values[3] <- defaults.restart
                refused_212: bool = try pad_refused(values, 4)
                return 212 if not refused_212
                values[0] <- 1
                values[3] <- defaults.jump
                refused_213: bool = try pad_refused(values, 4)
                return 213 if not refused_213
                values[3] <- start
                refused_214: bool = try pad_refused(values, 4)
                return 214 if not refused_214
                values[3] <- defaults.restart
                refused_215: bool = try pad_refused(values, 3)
                return 215 if not refused_215
                # Menu rows cycle a pad slot through the pool, wrapping and swapping.
                forward: CoursePad::Pad = CoursePad::cycle(defaults, CoursePad::SLOT_JUMP, 1)
                back: CoursePad::Pad = CoursePad::cycle(defaults, CoursePad::SLOT_JUMP, -1)
                east: i64 = pad_button(Application::GamepadButtonCode.East)
                right_bumper: i64 = pad_button(Application::GamepadButtonCode.RightShoulder)
                return 216 if forward.jump != east or forward.crouch != defaults.jump or back.jump != right_bumper or not CoursePad::valid(&forward) or not CoursePad::valid(&back)
                menu: CourseControls::Menu = CourseControls::Menu(keymap)
                on_pad: CourseControls::Menu = CourseControls::Menu{keymap: keymap, selected: CourseControls::PAD_FIRST}
                right: CourseControls::MenuStep = CourseControls::menu_key(on_pad, key_code(Application::KeyboardKeyCode.ArrowRight))
                typed: CourseControls::MenuStep = CourseControls::menu_key(on_pad, key_code(Application::KeyboardKeyCode.J))
                return 217 if right.result != CourseControls::MenuResult.Adjusted or right.delta != 1 or right.menu.selected != CourseControls::PAD_FIRST or typed.result != CourseControls::MenuResult.Ignored or menu.selected != CourseControls::SLOT_FORWARD
                return 0
