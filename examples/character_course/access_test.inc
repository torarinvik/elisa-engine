# Accessibility settings checks for the character-course self-test and the
# relaunch smoke, included in the CharacterCourse private section after
# `beacons_test.inc`.
        const module AccessTune:
            BASE_FONT: i32 = 20
            LARGE_FONT: i32 = 30
            MEASURE_FRAMES: usize = 3
            CAPTION_NANOS: u64 = 2500000000

        def handoff_access() -> CourseAccess::Settings:
            CourseAccess::Settings{text_scale: 150, high_contrast: true, captions: true, locale: UiLocale::RU}

        def same_settings(left: CourseAccess::Settings&, right: CourseAccess::Settings&) -> bool:
            left.text_scale == right.text_scale and left.high_contrast == right.high_contrast and left.captions == right.captions and left.locale == right.locale

        def write_access_raw(version: i64, scale: i64, contrast: i64, captions: i64, locale: i64, count: usize) -> bool:
            values: mutable i64[Save::MAX_FIELDS] = zeroed
            values[0] <- version
            values[1] <- scale
            values[2] <- contrast
            values[3] <- captions
            values[4] <- locale
            blob: Save::Blob = catch Save::save_write(values, count):
                value: value
                error failure: return false
            catch UserData::write_blob(CourseAccess::storage_key(), &blob):
                ok: true
                error failure: false

        def access_refused(version: i64, scale: i64, contrast: i64, captions: i64, count: usize) -> bool:
            locale_refused(version, scale, contrast, captions, UiLocale::EN, count)

        def locale_refused(version: i64, scale: i64, contrast: i64, captions: i64, locale: i64, count: usize) -> bool:
            return false if not write_access_raw(version, scale, contrast, captions, locale, count)
            loaded: CourseAccess::LoadResult = CourseAccess::load()
            defaults: CourseAccess::Settings = CourseAccess::defaults()
            loaded.status == CourseAccess::LoadStatus.Rejected and same_settings(&loaded.settings, &defaults)

        def adjust_test() -> bool:
            start: CourseAccess::Settings = CourseAccess::defaults()
            bigger: CourseAccess::Settings = CourseAccess::adjust(start, CourseAccess::ROW_TEXT_SIZE, 1)
            biggest: CourseAccess::Settings = CourseAccess::adjust(bigger, CourseAccess::ROW_TEXT_SIZE, 1)
            capped: CourseAccess::Settings = CourseAccess::adjust(biggest, CourseAccess::ROW_TEXT_SIZE, 1)
            floor: CourseAccess::Settings = CourseAccess::adjust(start, CourseAccess::ROW_TEXT_SIZE, -1)
            contrast: CourseAccess::Settings = CourseAccess::adjust(start, CourseAccess::ROW_CONTRAST, 1)
            captions: CourseAccess::Settings = CourseAccess::adjust(start, CourseAccess::ROW_CAPTIONS, -1)
            other: CourseAccess::Settings = CourseAccess::adjust(start, CourseAccess::ROW_COUNT, 1)
            sizes: bool = bigger.text_scale == 125 and biggest.text_scale == 150 and capped.text_scale == 150 and floor.text_scale == 100
            flags: bool = contrast.high_contrast and not contrast.captions and captions.captions and not captions.high_contrast
            sizes and flags and same_settings(&other, &start) and not start.high_contrast and not start.captions

        # Up from the first slot wraps to the last settings row; Left/Right
        # adjust a settings row and a letter key there changes nothing.
        def access_menu_test() -> bool:
            defaults: CourseControls::Keymap = CourseControls::defaults()
            menu: CourseControls::Menu = CourseControls::Menu(defaults)
            up: CourseControls::MenuStep = CourseControls::menu_key(menu, key_code(Application::KeyboardKeyCode.ArrowUp))
            last_row: bool = up.result == CourseControls::MenuResult.Moved and up.menu.selected == CourseControls::MENU_ROWS - 1
            right: CourseControls::MenuStep = CourseControls::menu_key(up.menu, key_code(Application::KeyboardKeyCode.ArrowRight))
            left: CourseControls::MenuStep = CourseControls::menu_button(up.menu, Application::gamepad_button_code(Application::GamepadButtonCode.DpadLeft))
            letter: CourseControls::MenuStep = CourseControls::menu_key(up.menu, key_code(Application::KeyboardKeyCode.J))
            slot_side: CourseControls::MenuStep = CourseControls::menu_key(menu, key_code(Application::KeyboardKeyCode.ArrowRight))
            wrapped: CourseControls::MenuStep = CourseControls::menu_key(up.menu, key_code(Application::KeyboardKeyCode.ArrowDown))
            adjusted: bool = right.result == CourseControls::MenuResult.Adjusted and right.delta == 1 and left.result == CourseControls::MenuResult.Adjusted and left.delta == -1
            unchanged: bool = letter.result == CourseControls::MenuResult.Ignored and letter.menu.keymap.crouch == defaults.crouch and slot_side.result == CourseControls::MenuResult.Ignored
            last_row and adjusted and unchanged and wrapped.menu.selected == CourseControls::SLOT_FORWARD

        # A version 1 save (no language) still loads, as English.
        def version_one_loads() -> bool:
            return false if not write_access_raw(1, 125, 1, 0, UiLocale::JA, 4)
            loaded: CourseAccess::LoadResult = CourseAccess::load()
            loaded.status == CourseAccess::LoadStatus.Loaded and loaded.settings.text_scale == 125 and loaded.settings.high_contrast and loaded.settings.locale == UiLocale::EN

        # Captions come from the packaged text/course.loc: English matches the
        # built-in text, Russian is real UTF-8, and a missing file refuses.
        def localized_captions_test() -> bool:
            words: CourseText::Captions = CourseText::load(CourseText::PATH)
            jump: UiLocaleData::Utf8 = CourseText::line(&words, UiLocale::EN, CourseSounds::EVENT_JUMP)
            plain: bool = words.loaded and jump.length == 13 and jump.bytes[1] == 83u8 and jump.bytes[12] == 93u8
            ru_jump: UiLocaleData::Utf8 = CourseText::line(&words, UiLocale::RU, CourseSounds::EVENT_JUMP)
            cyrillic: bool = ru_jump.length == 24 and ru_jump.bytes[1] == 208u8
            ja_step: UiLocaleData::Utf8 = CourseText::line(&words, UiLocale::JA, CourseSounds::EVENT_STEP)
            japanese: bool = ja_step.length == 19 and ja_step.bytes[1] == 229u8
            bad_locale: UiLocaleData::Utf8 = CourseText::line(&words, 9, CourseSounds::EVENT_JUMP)
            missing: CourseText::Captions = CourseText::load("text/missing.loc")
            keyed: bool = CourseText::key_of(CourseSounds::EVENT_SAVE) == CourseText::MSG_CAPTION_CHIME
            # The Language row cycles all five and wraps both ways.
            start: CourseAccess::Settings = CourseAccess::defaults()
            back: CourseAccess::Settings = CourseAccess::adjust(start, CourseAccess::ROW_LANGUAGE, -1)
            on: CourseAccess::Settings = CourseAccess::adjust(back, CourseAccess::ROW_LANGUAGE, 1)
            cycles: bool = back.locale == UiLocale::JA and on.locale == UiLocale::EN and CourseAccess::row_value(&back, CourseAccess::ROW_LANGUAGE) == "< 日本語 >"
            plain and cyrillic and japanese and bad_locale.length == 13 and not missing.loaded and keyed and cycles

        def captions_test() -> bool:
            quiet: CourseAccess::Settings = CourseAccess::defaults()
            loud: CourseAccess::Settings = handoff_access()
            none: CourseAccess::Captions = CourseAccess::Captions()
            muted: CourseAccess::Captions = CourseAccess::caption(none, &quiet, CourseSounds::EVENT_JUMP, 10)
            jump: CourseAccess::Captions = CourseAccess::caption(none, &loud, CourseSounds::EVENT_JUMP, 10)
            chime: CourseAccess::Captions = CourseAccess::caption(jump, &loud, CourseSounds::EVENT_SAVE, 20)
            off: bool = muted.event == 0 and not CourseAccess::caption_visible(&muted, &quiet, 10)
            shown: bool = CourseAccess::caption_visible(&jump, &loud, 10) and not CourseAccess::caption_visible(&jump, &loud, 10 + AccessTune::CAPTION_NANOS)
            newest: bool = chime.event == CourseSounds::EVENT_SAVE and CourseAccess::caption_visible(&chime, &loud, 10 + AccessTune::CAPTION_NANOS)
            off and shown and newest and not CourseAccess::caption_visible(&jump, &quiet, 10)

        # The first pass only requests every label; widths count once each
        # measurement reports ready after later frames.
        def widest_label(font_size: i32) -> f32 error[RenderScene::RenderSceneError]:
            widest: mutable f32 = 0.0
            pending: mutable bool = false
            for row in 0..<CourseControls::MENU_ROWS |widest, pending, font_size|:
                metrics: RenderSceneText::OverlayTextMetrics = try RenderScene::measure_overlay_text(row_label(row), font_size)
                pending <- true if not metrics.ready
                widest <- metrics.width if metrics.width > widest
            -1.0 if pending else widest

        # At every text size the widest measured label ends before the scaled
        # key column, so nothing overlaps at 150%.
        def layout_fits(session: mutable RuntimeServices::Session&) -> bool error[RenderScene::RenderSceneError, RuntimeServices::RuntimeServicesError]:
            settings: mutable CourseAccess::Settings = CourseAccess::defaults()
            fits: mutable bool = true
            for step in 0..<3 |session, settings, fits|:
                size: i32 = CourseAccess::font_size(&settings, Tune::LEGEND_FONT_SIZE)
                _ = try widest_label(size)
                for _ in 0..<AccessTune::MEASURE_FRAMES |session|:
                    _ = try RuntimeServices::pump(session)
                widest: f32 = try widest_label(size)
                fits <- false if widest <= 0.0 or widest >= legend_key_offset(&settings)
                settings <- CourseAccess::adjust(settings, CourseAccess::ROW_TEXT_SIZE, 1)
            fits

        # The sound event whose caption uses `key`.
        def caption_event(key: i64) -> u32:
            return CourseSounds::EVENT_JUMP if key == CourseText::MSG_CAPTION_JUMP
            return CourseSounds::EVENT_LAND if key == CourseText::MSG_CAPTION_LAND
            return CourseSounds::EVENT_WIN if key == CourseText::MSG_CAPTION_WIN
            return CourseSounds::EVENT_FALL if key == CourseText::MSG_CAPTION_FALL
            return CourseSounds::EVENT_STEP if key == CourseText::MSG_CAPTION_STEP
            CourseSounds::EVENT_SAVE

        # The widest packaged caption over every language, or -1 while a
        # measurement is still pending.
        def widest_caption(captions: CourseText::Captions&, settings: CourseAccess::Settings&) -> f32 error[RenderScene::RenderSceneError]:
            words: CourseText::Captions = captions
            font_size: i32 = CourseAccess::font_size(settings, Tune::LEGEND_FONT_SIZE)
            widest: mutable f32 = 0.0
            pending: mutable bool = false
            for index in 0..<30 |words, settings, widest, pending, font_size|:
                shown: CourseAccess::Settings = CourseAccess::Settings{text_scale: settings.text_scale, high_contrast: settings.high_contrast, captions: settings.captions, locale: index / CourseText::CAPTIONS}
                line: UiLocaleData::Utf8 = wrapped_caption(&words, &shown, caption_event(index % CourseText::CAPTIONS))
                metrics: RenderSceneText::OverlayTextMetrics = try RenderScene::measure_overlay_text_utf8(&line.bytes[0], line.length, font_size)
                pending <- true if not metrics.ready
                widest <- metrics.width if metrics.width > widest
            -1.0 if pending else widest

        # At 150% every caption in every language fits the caption line, and
        # Arabic hangs from the right edge while English starts at the left.
        def captions_fit(session: mutable RuntimeServices::Session&) -> i32 error[RenderScene::RenderSceneError, RuntimeServices::RuntimeServicesError]:
            captions: CourseText::Captions = CourseText::load(CourseText::PATH)
            large: CourseAccess::Settings = handoff_access()
            _ = try widest_caption(&captions, &large)
            for _ in 0..<AccessTune::MEASURE_FRAMES |session|:
                _ = try RuntimeServices::pump(session)
            widest: f32 = try widest_caption(&captions, &large)
            arabic: CourseAccess::Settings = CourseAccess::Settings{text_scale: large.text_scale, high_contrast: large.high_contrast, captions: large.captions, locale: UiLocale::AR}
            english: CourseAccess::Settings = CourseAccess::defaults()
            anchored: bool = caption_anchor_x(&arabic) == Tune::LEGEND_X + caption_width(&arabic) and caption_anchor_x(&english) == Tune::LEGEND_X
            return 184 if not captions.loaded or not anchored
            return 183 if widest <= 0.0 or widest >= caption_width(&large)
            # 172: Arabic captions reach the renderer shaped and in display
            # order, so their bytes change; English ones pass through untouched.
            jump: u32 = CourseSounds::EVENT_JUMP
            em: i64 = CourseAccess::font_size(&arabic, Tune::LEGEND_FONT_SIZE).i64()
            logical: UiLocaleData::Utf8 = UiLocaleData::wrap_utf8(CourseText::line(&captions, UiLocale::AR, jump), em, caption_width(&arabic).i64())
            shown: UiLocaleData::Utf8 = wrapped_caption(&captions, &arabic, jump)
            plain_em: i64 = CourseAccess::font_size(&english, Tune::LEGEND_FONT_SIZE).i64()
            plain: UiLocaleData::Utf8 = UiLocaleData::wrap_utf8(CourseText::line(&captions, UiLocale::EN, jump), plain_em, caption_width(&english).i64())
            english_shown: UiLocaleData::Utf8 = wrapped_caption(&captions, &english, jump)
            mutable moved: i64 = 0
            mutable kept: bool = plain.length == english_shown.length
            for i in 0..<256 |moved, kept|:
                moved <- moved + 1 if i.u32() < logical.length and logical.bytes[i] != shown.bytes[i] else moved
                kept <- kept and (i.u32() >= plain.length or plain.bytes[i] == english_shown.bytes[i])
            return 172 if logical.length == 0u32 or shown.length == 0u32 or moved == 0 or not kept
            # 173: the bundled Arabic font supplies presentation-form glyphs
            # the default font lacks. Without it a lam-alef run measures as
            # the same missing-glyph boxes as unassigned private-use points.
            ligature: cstr = "ﻻﻻﻻ"
            missing: cstr = ""
            _ = try RenderScene::measure_overlay_text(ligature, AccessTune::BASE_FONT)
            _ = try RenderScene::measure_overlay_text(missing, AccessTune::BASE_FONT)
            for _ in 0..<AccessTune::MEASURE_FRAMES |session|:
                _ = try RuntimeServices::pump(session)
            glyphs: RenderSceneText::OverlayTextMetrics = try RenderScene::measure_overlay_text(ligature, AccessTune::BASE_FONT)
            boxes: RenderSceneText::OverlayTextMetrics = try RenderScene::measure_overlay_text(missing, AccessTune::BASE_FONT)
            gap: f32 = glyphs.width - boxes.width
            return 173 if not glyphs.ready or not boxes.ready or (gap < 0.5 and gap > -0.5)
            0

        def semantics_describes(keymap: CourseControls::Keymap&, settings: CourseAccess::Settings&) -> bool error[UiSemantics::SemanticsError]:
            pad_defaults: CoursePad::Pad = CoursePad::defaults()
            _ = try row_semantics(keymap, &pad_defaults, settings, CourseControls::MENU_ROWS, 0)
            true

        def semantics_refused(keymap: CourseControls::Keymap&, settings: CourseAccess::Settings&) -> bool:
            catch semantics_describes(keymap, settings):
                value: false
                error failure: true

        def semantics_test(settings: CourseAccess::Settings&) -> bool:
            keymap: CourseControls::Keymap = CourseControls::defaults()
            pad_defaults: CoursePad::Pad = CoursePad::defaults()
            first: UiSemantics::Control = catch row_semantics(&keymap, &pad_defaults, settings, 0, 7):
                value: value
                error failure: return false
            size: UiSemantics::Control = catch row_semantics(&keymap, &pad_defaults, settings, CourseControls::SLOT_COUNT, 6):
                value: value
                error failure: return false
            toggle: UiSemantics::Control = catch row_semantics(&keymap, &pad_defaults, settings, CourseControls::PAD_FIRST - 2, CourseControls::PAD_FIRST - 2):
                value: value
                error failure: return false
            language: UiSemantics::Control = catch row_semantics(&keymap, &pad_defaults, settings, CourseControls::PAD_FIRST - 1, 0):
                value: value
                error failure: return false
            cycling: bool = language.role == UiSemantics::Role.Slider and language.label == "Language" and language.value == "< English >" and UiSemantics::adjustable(&language)
            pad_row: UiSemantics::Control = catch row_semantics(&keymap, &pad_defaults, settings, CourseControls::PAD_FIRST, CourseControls::PAD_FIRST + 1):
                value: value
                error failure: return false
            outside: bool = semantics_refused(&keymap, settings)
            first.role == UiSemantics::Role.KeyBinding and not first.selected and not UiSemantics::adjustable(&first) and size.role == UiSemantics::Role.Slider and size.selected and toggle.role == UiSemantics::Role.Toggle and toggle.selected and UiSemantics::position(&toggle) == CourseControls::PAD_FIRST - 1 and cycling and pad_row.label == "Pad jump" and pad_row.value == "A / Cross" and UiSemantics::adjustable(&pad_row) and outside and size.value == "< 100% >"

        def access_test(session: mutable RuntimeServices::Session&) -> i32 error[RenderScene::RenderSceneError, RuntimeServices::RuntimeServicesError, Save::SaveError, UserData::UserDataError]:
            return 153 if not adjust_test()
            large: CourseAccess::Settings = handoff_access()
            plain_settings: CourseAccess::Settings = CourseAccess::defaults()
            return 154 if CourseAccess::font_size(&large, AccessTune::BASE_FONT) != AccessTune::LARGE_FONT or legend_key_offset(&large) <= legend_key_offset(&plain_settings)
            return 155 if not access_menu_test()
            try CourseAccess::save(&large)
            loaded: CourseAccess::LoadResult = CourseAccess::load()
            return 156 if loaded.status != CourseAccess::LoadStatus.Loaded or not same_settings(&loaded.settings, &large)
            return 157 if not access_refused(2, 150, 1, 1, 4) or not access_refused(1, 110, 1, 1, 4) or not access_refused(1, 200, 0, 0, 4) or not access_refused(1, 125, 2, 0, 4) or not access_refused(1, 125, 1, 1, 3)
            return 181 if not locale_refused(2, 125, 1, 1, 5, 5) or not locale_refused(2, 125, 1, 1, -1, 5) or not locale_refused(3, 125, 1, 1, 0, 5)
            return 182 if not version_one_loads()
            try UserData::remove_blob(CourseAccess::storage_key())
            return 157 if CourseAccess::load().status != CourseAccess::LoadStatus.Missing
            return 158 if not captions_test()
            return 180 if not localized_captions_test()
            fits: bool = try layout_fits(session)
            return 159 if not fits
            caption_status: i32 = try captions_fit(session)
            return caption_status if caption_status != 0
            return 201 if not semantics_test(&plain_settings)
            # The live HUD takes every setting and releases every handle.
            keymap: CourseControls::Keymap = CourseControls::defaults()
            pad_defaults: CoursePad::Pad = CoursePad::defaults()
            legend: Legend = try create_legend(&keymap, &pad_defaults, &large)
            instruction: RenderScene::OverlayTextHandle = try RenderScene::create_overlay_text("Access", 18.0, 18.0, AccessTune::BASE_FONT, box_color(1.0, 1.0, 1.0))
            try layout_hud(&legend, instruction, &large)
            try refresh_legend(&legend, &keymap, &pad_defaults, &large, CourseControls::MENU_ROWS - 1)
            try layout_hud(&legend, instruction, &plain_settings)
            try release_legend(&legend)
            try RenderScene::destroy_overlay_text(instruction)
            try CourseAccess::save(&large)
            0

        # Relaunch: the saved settings come back and apply, then are removed.
        def access_relaunch_test() -> i32 error[RenderScene::RenderSceneError, UserData::UserDataError]:
            expected: CourseAccess::Settings = handoff_access()
            loaded: CourseAccess::LoadResult = CourseAccess::load()
            return 160 if loaded.status != CourseAccess::LoadStatus.Loaded or not same_settings(&loaded.settings, &expected)
            keymap: CourseControls::Keymap = CourseControls::defaults()
            pad_defaults: CoursePad::Pad = CoursePad::defaults()
            legend: Legend = try create_legend(&keymap, &pad_defaults, &loaded.settings)
            try release_legend(&legend)
            try UserData::remove_blob(CourseAccess::storage_key())
            return 161 if CourseAccess::load().status != CourseAccess::LoadStatus.Missing
            0
