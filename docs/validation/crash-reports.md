# Local crash reports

`native/crash_report.h` installs handlers for SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT and
SIGTRAP. They run on an alternate stack and are installed when `elisa_application_v1_initialize`
runs, and only if `ELISA_CRASH_DIR` is set. On a fatal signal the handler writes
`crash-PID.txt` to that directory, containing:

- the signal and the fault address;
- the build identity from `ELISA_BUILD_IDENTITY`;
- the main image path and its load address;
- the launch arguments;
- a backtrace.

The handler then re-raises the signal, so the process still dies by the original signal. The
build identity, arguments and image details are formatted when the handler is installed, so the
handler itself only calls open, write and backtrace. Nothing is sent over the network.

Packaged launchers default `ELISA_CRASH_DIR` to `~/Library/Logs/<app>`, and a directory the user
exports takes precedence. If the directory can't be created, crash reports are turned off. The
launcher exports the 16-digit build identity from the executable's provenance sidecar as
`ELISA_BUILD_IDENTITY`; it keeps the longer package and source identity in the launcher log.

## Evidence

- `scripts/test_crash_report.py` runs in the native unit tests. It forks children that fault
  with SIGSEGV and SIGABRT, checks the exit signal and each report field, and checks that an
  unusable directory installs nothing. Two controls fail it: one exits instead of re-raising
  (status 12), and one drops the backtrace (status 17).
- `test_launcher_configures_local_crash_reports` checks the launcher's default directory, an
  explicit directory, an unusable directory and the identity. This test caught literal shell
  quotes in the identity, which is now fixed.
- `scripts/validate_crash_report.py --app build/CourseCrash.app` checked the optimized packaged
  character course. It sent SIGSEGV while the game was rendering. The process died by SIGSEGV, and
  the report carried the package identity (`source=795f2772…`) and the probe argument. `atos`,
  using the recorded load address, named frames from `elisa_application_v1_pump` through
  `wi::Application::Run` to `GraphicsDevice_Metal::RenderPassBegin`.

Sentry Native and a crash in an Elisa-level trap (rather than a signal) are not covered.
