# Validation — 25 September 2026

Environment: Ubuntu 24.04, GCC 13.2.0, Qt 6.4.2, QCoro 0.12.0, CMake/Ninja.

## Full application build and executable smoke test

Full CMake/Ninja Debug desktop build: **passed**, including final executable link.
Command used: `cmake --build brickstore-build -j 4` (Qt 6.4.2; local QCoro 0.12 source).
Three invalid generated object files and an incomplete archive were regenerated during
build recovery; no source workaround was needed for those artifacts.

`QT_QPA_PLATFORM=offscreen brickstore-build/bin/brickstore --version`: exit 0,
output `BrickStore 2026.9.1 (build: custom)`. This checks executable startup and dynamic
linking, not interactive GUI workflows.

## Focused production-code tests

`ctest --test-dir brickowl-tests-build --output-on-failure`: passed (1/1 test executable).
Direct QtTest execution: 10 passed, 0 failed, including initialization/cleanup and
these eight substantive test methods:

- Inventory strings/numbers, price, availability.
- Reject invalid quantities.
- Reject error envelopes instead of treating them as empty inventory.
- Separate color namespaces and reject ambiguous cross-references.
- Require a unique catalogue item cross-reference.
- Preserve BrickOwl condition metadata.
- Copy/equality preserve separate BrickLink and BrickOwl lot identifiers.
- OAuth HMAC-SHA1 matches a published fixed signature vector.

The focused target compiles production parsers, OAuth code and Lot with GCC
`-Wall -Wextra -Werror`. It does not use real API credentials or make network requests.

## Review

`git diff --check`: passed.
Reviewed preview/apply handling: duplicate remote mappings disable every involved
row; stale quantities/document state stop the write; off-sale BO offers are rejected;
uncertain creates retain a retry guard; failures stop subsequent rows.
These workflow properties were reviewed in source, not exercised against live stores.

## Not verified

- Windows/macOS compilation, installer packaging and visual UI behavior.
- Live BrickOwl/BrickLink authentication and mutation responses.
- BSX/binary round trips and migration with older BrickStore versions.
- Full end-to-end synchronization, network cancellation/fault injection and rate limits.
- Large inventories, concurrent sales and automatic order processing.

This is a manually operated integration prototype, not a production-certified sync service.

## Windows packaging follow-up

The existing Windows job now has an isolated prototype mode, using windows-latest,
MSVC 2022, upstream Qt 6.11.2, Release, upstream deploy/installer targets and Inno Setup.
No Windows binary was produced in this Linux session and GitHub Actions was not run.
No installer, uninstall, Windows TLS or MSVC runtime result is claimed here.

Locally completed checks for the new configuration:

- Full Linux Release build with `BRICKOWL_PROTOTYPE=ON`: passed (Qt 6.4.2).
- Prototype executable offline UI smoke: **15 checks passed**, platform `offscreen`.
  Includes actual main/settings/import windows, masked editable key, BrickLink actions,
  parser/OAuth checks, TLS and QtQuick/Quick3D imports. The smoke command skips
  `afterInit()` first-run catalogue/update prompts; it does not exercise network stores.
- Existing eight fixture tests plus init/cleanup: passed.
- PowerShell 7.4.6 parser: both new packaging/test scripts parsed without errors.
- YAML parse and actionlint 1.7.7: new Windows path passed. Lint excludes two
  pre-existing diagnostics in unrelated jobs (runner.workspace and macos-26, which
  that linter version does not recognize). The shared Qt action now uses runner.temp.
- `git diff --check`: passed.

CI additionally requires native qwindows and Schannel, tests the extracted portable ZIP,
silently installs the generated EXE, tests the installed payload, uninstalls, and publishes
artifacts only after success. See WINDOWS-PROTOTYPE-PL.md for the precise user workflow.
