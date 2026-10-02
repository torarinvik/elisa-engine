# UiAudioPage and AudioSettings

`src/runtime/audio_settings.elisa` saves the player's mixer snapshot (music, effects, UI) as whole percentages in a version-1 Save blob under the user-data key `audio-mix`. Decoding rejects a wrong version, a bad length or any value outside 0–100 instead of clamping it, so a corrupt save never reaches the mixer. An invalid mix is never written.

`src/ui/audio_page.elisa` shows each bus as a 0–100 % slider (step 5, fields 301–303). It loads the page from a snapshot and turns the page back into one.

`test/ui_audio_page.elisa` (in the gate) covers loading, a nudge that edits only its own bus, a blob round trip, and rejecting corrupt, wrong-version and invalid mixes. Negative control: removing the range check fails with code 6.
