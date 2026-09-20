# Runtime service rollback validation

The native application smoke checks partial startup rollback for the typed
Physics and Audio fallback adapters. Fault injection is compiled only when the
smoke runner passes `--native-test-probes`; ordinary Elisa applications do not
export or link these test hooks.

The Physics path initializes the Jolt scene, injects a failure before the
adapter reports success, then verifies the scene was released and the live
backend profile remains queryable. Elisa shuts down the host, negotiates the
same Jolt fallback again, and initializes the world successfully on retry.

The Audio path opens the miniaudio null device, injects a failure immediately
after device startup, and verifies the adapter shuts the device down. Elisa
observes `ApplicationUnavailable` from the audio service, closes the host,
checks that the profile is closed, and negotiates and initializes the same
silent-audio fallback successfully on retry.

Run both lifecycle scenarios with:

```sh
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
  python3 scripts/application_native_smoke.py
```

Validation on 2026-09-20: both `application-native-smoke` and
`application-failure-cleanup-smoke` passed on SDL3/Metal. The runner's
test-probe flag is supplied only by this smoke script. Source-length and
module-hygiene checks also passed.
