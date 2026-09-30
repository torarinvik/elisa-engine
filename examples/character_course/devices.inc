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

        def devices_quit(saved: DeviceSettings::Saved&) -> bool:
            DeviceSettings::save_window(DEVICES_KEY, &saved.audio)

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
