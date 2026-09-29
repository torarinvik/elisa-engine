# Session screen and roster policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is T08 progress.

## Design

`src/net/session_ui.elisa` (`NetSessionUi`) decides what a session screen
shows; it draws nothing and opens no connection.

- Screens: Menu, Hosting, Joining, Connected, Failed. A stable `Reason`
  (BadAddress, Unreachable, Refused, Timeout, Full) explains a failure.
- `host` needs the menu and a valid port. `join` refuses an empty or
  oversized address or a bad port before any attempt and shows BadAddress.
  Retrying from Failed keeps the retry count; starting from the menu resets it.
- `failed` returns whether another try may be offered: only Unreachable and
  Timeout, and only while fewer than three failures are counted. Refused and
  Full are final. A failure clears the roster.
- The roster holds up to eight unique positive player ids with a ready flag.
  Removal swaps the last player into the gap; `all_ready` needs two or more
  players, all ready. Players can only join while connected or hosting.

## Checks

- `test/net_session_ui.elisa` exits 0 (codes 1-17): address validation,
  bounded retry, host/connect, roster limits, uniqueness, swap-remove,
  readiness, and clearing on failure or leave.
- Negative control: removing the retry bound makes the test exit 6.
- Wired into `scripts/check.elisascript`.

## Gaps

- No UI widgets, discovery configuration, diagnostics panel or GameNetworking
  Sockets connection drives this yet; no two-player packaged sample was run.
  T08 stays open.
- Retry limit (three) and roster size (eight) are fixed constants.
- No proof written for this module.
