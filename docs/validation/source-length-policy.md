# Source-length gate restored

The packaging and asset-cook implementation had three Python files over the
repository's 600-line limit. The code is now split by responsibility: asset
cook declaration checks live in `scripts/asset_cook_config.py`, launcher
identity and script generation live in `scripts/package_macos_launcher.py`,
and launcher tests use their own module with shared fixture support. The asset
cook cache fingerprint includes the extracted configuration module so edits to
that behavior invalidate cached outputs. Source and test changes are committed
in `9e40976a`.

## Qualification

- `python3 scripts/check_source_length.py` passes. The former over-limit files
  are now `scripts/asset_cooks.py` (517 lines),
  `scripts/package_macos_app.py` (489 lines), and
  `scripts/test_package_macos_app.py` (443 lines).
- Packaging suites: 41 tests pass. Log:
  `build/validation/source-length-package-tests.log`.
- Build/run and asset-cook suites: 48 tests pass. Log:
  `build/validation/source-length-asset-cook-tests.log`.
- Dense animation cook controls: 2 tests pass. Log:
  `build/validation/source-length-dense-cook-tests.log`.
- Python bytecode compilation for the changed modules and `git diff --check`
  pass.
- Source-length output: `build/validation/source-length-policy.log`.

The packaging tests use temporary projects and fake executables; they qualify
staging, launcher behavior and failure handling, not signing, notarization or a
relocated end-user application on another machine.
