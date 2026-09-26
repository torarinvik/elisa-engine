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
through the generic application runner in a hidden 320x200 test project,
rejects empty titles and messages, then successfully reports a missing-resource
message before renderer initialization. The fixture is part of
`scripts/application_native_smoke.py`. Source-length, module hygiene, runner
unit tests, and whitespace checks pass. The full SDL3/Metal application smoke
matrix passed all 13 projects with this fixture included; the message appeared
on stderr and no modal was opened for the hidden project. Interactive native-
dialog appearance and accessibility remain a manual release check. Reproduce
from the repository root with:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN="../Elisa-compiler/bin/elisac-stage1" \
ELISA_RUNTIME_OBJ="../Elisa-compiler/build/runtime/elisacore_runtime.o" \
/opt/homebrew/bin/python3 scripts/application_native_smoke.py
```
