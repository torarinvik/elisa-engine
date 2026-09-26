# Application error messages

`Application::show_error(title, message)` reports a startup or fatal error
through the engine boundary. It returns whether delivery succeeded. Titles
follow the application's existing bounded title validation; the nonempty
message is limited to 4096 bytes. Invalid arguments return false.

Call from the application owner thread, including before initialization when
startup fails. An established application rejects calls from other threads.
The API writes the message to stderr first. A hidden project stops there,
without opening a modal UI; a visible project requests an SDL native error
dialog. Dialog failure returns false but retains the stderr diagnostic. No
platform library or native shim is needed in the Elisa game project.

Validation on 2026-09-26: `test/application_error_message_native.elisa`, built
through the generic application runner in a hidden 320x320 test project,
rejects empty titles and messages, then successfully reports a missing-resource
message before renderer initialization. Source-length, module hygiene and
whitespace checks pass. Interactive native-dialog appearance and accessibility
remain a manual release check.
