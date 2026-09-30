# Native smoke failure artifacts

Every native application smoke (`scripts/application_native_smoke.py`) now writes
`build/native-smoke/NAME.json` and `NAME.log`. The JSON records the status, whether the smoke
passed, the source file, the elapsed time and the UTC time. For a failure, it also records
the last 40 log lines and `failed_assertions`: each `return STATUS if CONDITION` guard in the
smoke's Elisa source whose status matches, with its line number. The failure message on stderr
names the JSON file.

A control that skips destroying the lights in `render-resource-churn-smoke` produced status 47.
The JSON's single assertion pointed to line 105, `not same(counts(), baseline)`.
`scripts/native_smoke_artifacts.py --self-test` runs in the shared check. Its tests cover
several guards with the same status, passing runs, and a missing source.

A smoke's output is now printed after the smoke finishes instead of streaming live.
