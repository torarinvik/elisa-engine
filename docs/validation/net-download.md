# Integrity-checked resumable downloads

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is T07 progress.

## Design

`src/net/download.elisa` (`NetDownload`) is the policy a libcurl transport
will feed. It has no I/O and stores no credential.

- `begin` refuses an empty or oversized package, a checksum outside range, or
  a start while one is running.
- `receive` takes one byte at exactly the next offset. A gap, a repeat or an
  out-of-range byte changes nothing; an overrun fails the transfer.
- `resume_offset` gives where to continue after an interruption, so a
  dropped connection resumes instead of restarting.
- `finish` checks size and a rolling checksum (multiplier 257, modulus
  1000000007, so nothing overflows). A mismatch or truncation discards the
  staging area.
- `install` replaces the installed package only from a verified download and
  only with a higher version; the staging area is cleared. Failure,
  cancellation or a refused install always leaves the installed version and
  checksum untouched.

## Checks

- `test/net_download.elisa` exits 0 (codes 1-18): bad requests, resume,
  gaps and repeats, install, corruption, truncation, cancellation, overrun
  and downgrade.
- Negative control: replacing the rolling checksum with a plain sum fails
  the test (exit 11).
- Wired into `scripts/check.elisascript`.

## Gaps

- The checksum is a rolling hash for integrity against corruption, not a
  cryptographic digest; a signed manifest or SHA-256 is still needed against
  tampering.
- No libcurl binding, timeout, progress callback or on-disk atomic rename
  exists; no packaged update was interrupted for real. T07 stays open.
- Credential redaction is by construction (nothing here takes one); the
  transport and logs still need to be checked.
- No proof written for this module.
