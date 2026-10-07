# Compiler 8006b660 installation

Installed the clean main product using `bash ../Elisa-compiler/scripts/install_stage1.sh`.
Snapshot revision: `8006b66051ff68f8212bcbed052d066ab5c2bd8f`.
Product SHA256: `0a28b0f4ed4d022a49e956804822c20a38fabcdb0d95a1873a6721e491f4f359`.
Runtime SHA256: `ca40ba1db8a74110936ad5cdaf808707020c5c74ebb6e491bda2198696d13b8a`.
The installed product matches its provenance. The runtime is unchanged from the
previous snapshot. Compiler main subsequently advanced to documentation-only
`665f40d7`, correcting the guide's accumulator lint status to working.

Command:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 /opt/homebrew/bin/python3.14 scripts/run_tests.py "$HOME/.elisac/elisac-stage1" --no-cache -j4
```

Result: **215/215**, zero cached compiles, 65 seconds. This validates the runtime
test manifest, not physical audio or the complete shared/native gate.
Log: `build/validation/compiler-8006-runtime.log`; installation log:
`build/validation/compiler-8006-install.log`. The strict O2 pair built successfully as generation
`65e022cea38248fc9506db4979206eef`; build log:
`build/validation/proof-8006-build.log`. The uncached sweep remains **69/73**,
with the same ActionInput context/deadzone, audio animation events and sound event
assets failures (`build/validation/compiler-8006-proof-sweep.log`).
Fixed-array admission, scalar float ownership and IEEE literal forgery controls
all pass (`build/validation/compiler-8006-focused.log`). Full compatibility matrix
and shared/native qualification remain open.
