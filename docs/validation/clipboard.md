# Clipboard service

`ApplicationClipboard` (src/runtime/application_clipboard.elisa) exposes `set_text` and `get_text` over the native host (native/application_clipboard.inc). Text is limited to 4096 bytes and must be valid UTF-8. The host rejects overlong encodings, surrogates and code points above U+10FFFF with `InvalidText`. A read of larger text returns `TooLarge` and does not truncate it. Calls made before the host starts return `NotRunning`.

Validation: test/application_clipboard_probe.elisa runs inside application-native-smoke (codes 195–201). It saves the user's clipboard, round-trips ASCII and multi-byte text, checks that invalid UTF-8 is rejected, and always restores the original. A negative control that disables the UTF-8 check fails with 199. The smoke passed with the clipboard empty and with non-ASCII text, and the text was unchanged after the run.
