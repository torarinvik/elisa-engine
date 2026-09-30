# Control profiles and input recordings on disk

`ActionInputStore` (src/runtime/action_input_store.elisa) keeps two kinds of file as checksummed, versioned `UserData` payloads:
- a control profile (`save_profile` / `load_profile`);
- a session recording that holds the action log and the pointer-axis log together (`save_recording` / `load_recording`).

The recording's layout is "EIS1", then u32 lengths for both parts, then each codec's bytes.
Loads go through the validating decoders. The caller's logs or bindings are filled only if every part decodes; otherwise the load reports `Corrupt`, and it reports `NotFound` or `WrongVersion` from the envelope.

Native coverage: test/action_input_store_probe.elisa runs inside application-native-smoke, using the real user-data directory. The probe:
1. rebinds a key, saves the profile and reloads it;
2. records 20 ticks of key and mouse-motion input, saves them and reloads them;
3. replays the pointer log and checks that it sums to the same live look value;
4. writes a payload with a valid envelope but a damaged recording and checks that it is refused without touching the target logs;
5. removes the files and checks that `NotFound` is reported.

The probe's codes are 110–122; the native main reports them as 104 + code.
Negative control: saving an empty pointer part makes the smoke fail with probe code 116.

Stage1 note: `ActionInputReplay`'s entry enum was renamed from `Kind` to `EntryKind`. The bare `Kind` in src/world/commands.elisa resolved to it once both were in one program.

## Character course session recording

The character course now records its gameplay input. `CourseControls::drain_gameplay_recorded` routes every Application event through `ActionInputRecording::record_application_event` at the play loop's gameplay-frame tick, which also updates the live input. On quit, the loop saves the log (with an empty pointer log) under `course-input-recording` through `ActionInputStore::save_recording`. Menu events are not recorded, because they drive the rebind menu rather than gameplay actions.

The self-test `recording_test` (codes 250–255) checks the round trip:

- It drives the course's own default bindings with synthetic events: hold W, tap Space, hold A, then focus loss.
- It checks the live action digests per tick.
- It saves and reloads the recording through user data.
- It replays the recording into a freshly bound input, and every one of the 10 ticks must match.

`python3 scripts/application_native_smoke.py --only character-course-smoke` passed. A control that replays one tick late fails with status 255. A real play session's file was not inspected.
