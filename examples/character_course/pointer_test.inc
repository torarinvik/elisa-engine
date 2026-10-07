# Pointer operation of the rebind/settings menu for the character-course
# self-test, included in the CharacterCourse private section after
# `access_test.inc`. Events are built in Elisa; the SDL pointer queue itself
# is covered by the RenderScene selection smoke.
        def pointer_event(kind: Application::PointerEventKind, button: i32, x: f32, y: f32,
            delta_y: f32, pressed: bool) -> Application::PointerEvent:
            Application::PointerEvent{kind: kind, button: button, x: x, y: y,
                delta_x: 0.0, delta_y: delta_y, buttons: 0, pressed: pressed}

        def row_middle(settings: CourseAccess::Settings&, row: i64) -> f32:
            legend_row_y(settings, row) + CourseAccess::scaled(settings, Tune::LEGEND_ROW_HEIGHT) * 0.5

        # Every row's middle and last pixel hit that row at each text size;
        # points left, right, above and below the rows miss.
        def hits_at(settings: CourseAccess::Settings&) -> bool:
            scale: CourseAccess::Settings = settings
            all: mutable bool = true
            inside_x: f32 = Tune::LEGEND_X + 4.0
            for row in 0..<CourseControls::MENU_ROWS |all, scale, inside_x|:
                bottom: f32 = legend_row_y(&scale, row) + CourseAccess::scaled(&scale, Tune::LEGEND_ROW_HEIGHT) - 1.0
                all <- false if legend_hit(&scale, inside_x, row_middle(&scale, row)) != row or legend_hit(&scale, inside_x, bottom) != row
            right: f32 = Tune::LEGEND_X + legend_key_offset(settings) + CourseAccess::scaled(settings, Tune::LEGEND_KEY_WIDTH)
            middle: f32 = row_middle(settings, 0)
            misses: bool = legend_hit(settings, Tune::LEGEND_X - 1.0, middle) == -1 and legend_hit(settings, right + 1.0, middle) == -1
            above: bool = legend_hit(settings, inside_x, legend_row_y(settings, 0) - 1.0) == -1
            below: bool = legend_hit(settings, inside_x, legend_row_y(settings, CourseControls::MENU_ROWS) + 1.0) == -1
            all and misses and above and below

        def hit_test() -> bool:
            plain: CourseAccess::Settings = CourseAccess::defaults()
            medium: CourseAccess::Settings = CourseAccess::adjust(plain, CourseAccess::ROW_TEXT_SIZE, 1)
            large: CourseAccess::Settings = handoff_access()
            # The last row at 100% is a different row once text grows.
            moved_row: bool = legend_hit(&large, Tune::LEGEND_X + 4.0, row_middle(&plain, CourseControls::MENU_ROWS - 1)) != CourseControls::MENU_ROWS - 1
            hits_at(&plain) and hits_at(&medium) and hits_at(&large) and moved_row

        def press_test() -> bool:
            menu: CourseControls::Menu = CourseControls::Menu(CourseControls::defaults())
            left: i32 = Application::PointerButtons::LEFT
            right: i32 = Application::PointerButtons::RIGHT
            slot: CourseControls::MenuStep = CourseControls::menu_pointer(menu, 3, left)
            slot_right: CourseControls::MenuStep = CourseControls::menu_pointer(menu, 3, right)
            forward: CourseControls::MenuStep = CourseControls::menu_pointer(menu, CourseControls::MENU_ROWS - 2, left)
            back: CourseControls::MenuStep = CourseControls::menu_pointer(menu, CourseControls::SLOT_COUNT, right)
            middle: CourseControls::MenuStep = CourseControls::menu_pointer(menu, CourseControls::SLOT_COUNT, Application::PointerButtons::MIDDLE)
            missed: CourseControls::MenuStep = CourseControls::menu_pointer(menu, -1, left)
            selects: bool = slot.result == CourseControls::MenuResult.Moved and slot.menu.selected == 3 and slot_right.result == CourseControls::MenuResult.Ignored
            adjusts: bool = forward.result == CourseControls::MenuResult.Adjusted and forward.delta == 1 and forward.menu.selected == CourseControls::MENU_ROWS - 2 and back.result == CourseControls::MenuResult.Adjusted and back.delta == -1 and back.menu.selected == CourseControls::SLOT_COUNT
            selects and adjusts and middle.result == CourseControls::MenuResult.Ignored and missed.result == CourseControls::MenuResult.Ignored

        # Whole events at 125%: a click on Text size steps it to 150%; a
        # release, a motion and a click outside change nothing; the wheel
        # moves the selection both ways.
        def event_test() -> bool:
            settings: CourseAccess::Settings = CourseAccess::adjust(CourseAccess::defaults(), CourseAccess::ROW_TEXT_SIZE, 1)
            menu: CourseControls::Menu = CourseControls::Menu(CourseControls::defaults())
            left: i32 = Application::PointerButtons::LEFT
            x: f32 = Tune::LEGEND_X + 4.0
            y: f32 = row_middle(&settings, CourseControls::SLOT_COUNT)
            click: CourseControls::MenuStep = menu_pointer_event(menu, &settings, pointer_event(Application::PointerEventKind.Button, left, x, y, 0.0, true))
            larger: CourseAccess::Settings = CourseAccess::adjust(settings, click.menu.selected - CourseControls::SLOT_COUNT, click.delta)
            release: CourseControls::MenuStep = menu_pointer_event(menu, &settings, pointer_event(Application::PointerEventKind.Button, left, x, y, 0.0, false))
            motion: CourseControls::MenuStep = menu_pointer_event(menu, &settings, pointer_event(Application::PointerEventKind.Motion, 0, x, y, 0.0, false))
            outside: CourseControls::MenuStep = menu_pointer_event(menu, &settings, pointer_event(Application::PointerEventKind.Button, left, 600.0, y, 0.0, true))
            up: CourseControls::MenuStep = menu_pointer_event(menu, &settings, pointer_event(Application::PointerEventKind.Wheel, 0, x, y, 1.0, false))
            down: CourseControls::MenuStep = menu_pointer_event(menu, &settings, pointer_event(Application::PointerEventKind.Wheel, 0, x, y, -1.0, false))
            applied: bool = click.result == CourseControls::MenuResult.Adjusted and larger.text_scale == 150
            quiet: bool = release.result == CourseControls::MenuResult.Ignored and motion.result == CourseControls::MenuResult.Ignored and outside.result == CourseControls::MenuResult.Ignored
            wheel: bool = up.menu.selected == CourseControls::MENU_ROWS - 1 and down.menu.selected == CourseControls::SLOT_FORWARD + 1
            applied and quiet and wheel

        def pointer_test() -> i32:
            return 162 if not hit_test()
            return 163 if not press_test()
            return 164 if not event_test()
            # Nothing is queued in the hidden run: both drains are no-ops.
            settings: CourseAccess::Settings = CourseAccess::defaults()
            menu: CourseControls::Menu = CourseControls::Menu(CourseControls::defaults())
            _ = CourseControls::discard_pointer()
            idle: CourseControls::MenuStep = drain_menu_pointer(menu, &settings, 900)
            return 165 if idle.result != CourseControls::MenuResult.Ignored or idle.menu.selected != menu.selected or CourseControls::discard_pointer() != 0
            0
