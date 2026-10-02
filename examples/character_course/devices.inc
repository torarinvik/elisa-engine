# Saved display and audio-output choices for the character course, included
# in the CharacterCourse private section. Play reapplies them at startup and
# remembers the window's display on quit; the self-test hands a record to
# the relaunch smoke, which reapplies it in a second process.
        const DEVICES_KEY: cstr = "course-devices"

        def devices_start() -> DeviceSettings::Saved:
            saved: DeviceSettings::Saved = DeviceSettings::load(DEVICES_KEY)
            if saved.stored == DeviceSettings::Stored.Loaded:
                _ = DeviceSettings::apply_display(&saved)
                if saved.audio.name_count != 0:
                    _ = DeviceSettings::reopen_audio(&saved, Tune::AUDIO_SAMPLE_RATE_HZ, Tune::AUDIO_CHANNELS)
            saved

        # Keeps the output in the record, which the settings menu may have
        # changed since startup.
        def devices_quit(saved: DeviceSettings::Saved&) -> bool:
            current: DeviceSettings::Saved = DeviceSettings::load(DEVICES_KEY)
            audio: AudioDevices::Device = current.audio if current.stored == DeviceSettings::Stored.Loaded else saved.audio
            DeviceSettings::save_window(DEVICES_KEY, &audio)

        # Menu picks for the window's display and the saved output.
        def devices_picks(displays: ApplicationDisplays::Displays&) -> CourseDeviceMenu::Picks:
            saved: DeviceSettings::Saved = DeviceSettings::load(DEVICES_KEY)
            audio: AudioDevices::Device = saved.audio if saved.stored == DeviceSettings::Stored.Loaded else DeviceSettings::empty_audio()
            CourseDeviceMenu::current(displays, ApplicationDisplays::window_display(), &audio)

        # Saves the window's display and the picked output when the menu
        # closes; the output opens on the next launch.
        def devices_menu_save(picks: CourseDeviceMenu::Picks&) -> bool:
            output: AudioDevices::Device = CourseDeviceMenu::chosen_output(picks)
            DeviceSettings::save_window(DEVICES_KEY, &output)

        def devices_default_output() -> AudioDevices::Device:
            output: mutable AudioDevices::Device = DeviceSettings::empty_audio()
            total: i32 = AudioDevices::count()
            for i in 0..<total.usize() |output|:
                candidate: AudioDevices::Result = AudioDevices::device(i)
                output <- candidate.device if candidate.device.is_default
            output

        # Self-test: saves the primary display and the default output.
        def devices_handoff() -> i32:
            listing: ApplicationDisplays::Listing = ApplicationDisplays::list()
            return 260 if not listing.ok or listing.displays.count == 0
            primary: mutable ApplicationDisplays::Display = listing.displays.items[0]
            for i in 0..<listing.displays.count |primary, listing|:
                primary <- listing.displays.items[i] if listing.displays.items[i].primary
            output: AudioDevices::Device = devices_default_output()
            return 261 if not DeviceSettings::save(DEVICES_KEY, &primary, true, &output)
            0

        # Relaunch: the record written by the previous process reapplies the
        # display and reopens the mixer on the saved output, then is removed.
        def devices_relaunch_test() -> i32:
            saved: DeviceSettings::Saved = DeviceSettings::load(DEVICES_KEY)
            return 262 if saved.stored != DeviceSettings::Stored.Loaded or not saved.has_display
            applied: DeviceSettings::Applied = DeviceSettings::apply_display(&saved)
            return 263 if applied.display != DeviceChoice::Source.Saved or ApplicationDisplays::window_display() != applied.display_id
            if AudioDevices::count() > 0:
                opened: AudioDevices::Opened = DeviceSettings::reopen_audio(&saved, Tune::AUDIO_SAMPLE_RATE_HZ, Tune::AUDIO_CHANNELS)
                return 264 if opened != AudioDevices::Opened.Chosen
            removed: i32 = catch UserData::remove_payload(DEVICES_KEY):
                value: 0
                error failure: 1
            return 265 if removed != 0 or DeviceSettings::load(DEVICES_KEY).stored != DeviceSettings::Stored.NotFound
            0

        # Self-test: the device rows wrap, the display pick follows the
        # window, a missing saved output reads as the default, and long
        # names are cut on a code-point boundary.
        def devices_menu_test() -> i32:
            return 266 if CourseDeviceMenu::cycle(2, 1, 0, 3) != 0 or CourseDeviceMenu::cycle(0, -1, 0, 3) != 2 or CourseDeviceMenu::cycle(-1, -1, -1, 2) != 1 or CourseDeviceMenu::cycle(5, 1, 0, 0) != 0
            listing: ApplicationDisplays::Listing = ApplicationDisplays::list()
            gone: mutable AudioDevices::Device = DeviceSettings::empty_audio()
            gone.name[0] <- 90u8
            gone.name_count <- 1
            picks: CourseDeviceMenu::Picks = CourseDeviceMenu::current(&listing.displays, ApplicationDisplays::window_display(), &gone)
            window_ok: bool = listing.displays.count == 0 or listing.displays.items[picks.display.usize()].id == ApplicationDisplays::window_display()
            return 267 if not window_ok or picks.output != CourseDeviceMenu::DEFAULT_OUTPUT or CourseDeviceMenu::chosen_output(&picks).name_count != 0
            stepped: CourseDeviceMenu::Picks = CourseDeviceMenu::adjust(picks, CourseDeviceMenu::ROW_OUTPUT, 1)
            if picks.output_count > 0:
                return 268 if stepped.output != 0 or CourseDeviceMenu::chosen_output(&stepped).name_count != AudioDevices::device(0).device.name_count
                back: CourseDeviceMenu::Picks = CourseDeviceMenu::adjust(stepped, CourseDeviceMenu::ROW_OUTPUT, -1)
                return 268 if back.output != CourseDeviceMenu::DEFAULT_OUTPUT
            same_display: CourseDeviceMenu::Picks = CourseDeviceMenu::adjust(picks, CourseDeviceMenu::ROW_DISPLAY, listing.displays.count.i64())
            return 269 if same_display.display != picks.display or same_display.output != picks.output
            0
