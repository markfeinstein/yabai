# Development testing

## Discover and select core tests

```sh
make -C tests core-build
./tests/bin/ordinary/tests --list
./tests/bin/ordinary/tests --filter window_constraints_
./tests/bin/ordinary/tests --list --filter window_close_
./tests/bin/ordinary/tests --help
```

`--filter` is one nonempty, case-sensitive **literal substring**, not a glob or
regular expression. It composes with `--list` in either order. Listing prints
only the selected test names and exits before suite initialization or test
execution. A run reports the selected count and preserves source registration
order. No match exits 1 (including list mode); unknown, repeated, or malformed
arguments exit 2 without running tests. `--help` exits 0. Existing individual
`--benchmark-<name>` modes are unchanged and cannot be combined with filtering.
With no arguments the executable still runs the full core suite. The runner
initializes the shared temporary arena and read-only WindowServer client library
before execution so filtered tests do not rely on incidental setup by earlier
tests. As with the full suite, these legacy core tests require macOS and its
private frameworks; they are not a platform-independent unit-test executable.

These options select only tests in the core executable, not the separate runtime,
UI-model, installer, or shell-fixture executables. `make test` continues to run
all ordinary offline gates; its CLI regression check exercises the real entry
point. The standalone `tests/runner.c` module is compiled and linked separately
for ordinary, sanitizer, and thin-architecture configurations, and its parser
unit tests run in every core suite. For another configuration, build with
`make -C tests CONFIG=asan core-build` (or `CONFIG=tsan`) and use the corresponding
`tests/bin/<configuration>/tests` executable; retain the sanitizer environment
settings from `tests/makefile` when running it directly.

## Offline evidence-gate self-tests

`make classifier-corpus-gate native-tab-evidence-gate` runs the existing Python
self-tests with synthetic positive and negative evidence. Both also run through
`make test` / `make test-all` and an explicit CI step, without a daemon, live
captures, or network access. These validate the gates, not real-host coverage.

## Bounded parser fuzzing

`make fuzz-build` builds deterministic AddressSanitizer and
UndefinedBehaviorSanitizer drivers for the regex-cost/compiler boundary, daemon
message framing, and Mach-O parser. `make fuzz-smoke` runs 1,000 generated inputs
per harness with bounded maximum input sizes and is suitable for CI. Each harness
source exports the standard `LLVMFuzzerTestOneInput` entry point, so a developer
with a libFuzzer runtime can link the same source with `-fsanitize=fuzzer` for a
coverage-guided campaign.

The repository does not ship crash corpora or mutate production files. Preserve
any useful reproducer as a focused regression fixture before committing it.

## Wake and visibility recovery

The offline `window_focus_reconciliation_` test uses mocked live AX and
WindowServer state to check stale minimized/ordered-out recovery on tiled and
floating-layout Spaces. Floating Spaces must refresh visibility flags and the
active-window highlight without running tiling validation; genuinely minimized,
ordered-out, other-Space, and non-visible cases remain untouched. This is not
live wake/sleep confirmation.

## Native matcher compilation limits

JSONC rule matchers and base/mode scaffold matchers use the same regex
complexity guard as CLI rules, both during validation and runtime preparation.
The offline regressions reject compact nested repetitions before system regex
compilation, retain field-specific redacted diagnostics, and check cleanup of
partially prepared rules and scaffold regex arrays. Valid matcher and exclusion
behavior remains covered by the existing rule and scaffold suites.

## Independent compilation boundaries

`src/pattern.c`, `src/window_constraints.c`, `src/window_server_classifier.c`,
and `src/window_server_hit_test.c` compile as ordinary C objects alongside
`menu_bar_model.c`, rather than being included by `src/manifest.m`. The daemon
and core tests link these modules separately in every
build/sanitizer/architecture configuration. Source/header dependencies and
generated `.d` files preserve incremental rebuilds; repository fixtures verify
that a module change recompiles its object and relinks without rebuilding
unrelated modules or OSAX artifacts. These are incremental boundary
extractions, not a wholesale rewrite of the manifest. Existing matcher,
constraint, classifier, and hit-test behavior and public interfaces are
unchanged; their tests exercise the separately linked implementations.

## Snapshot lookup performance

`make test-benchmark-ws-snapshot test-benchmark-snapshot-reuse` measures snapshot
capture and its startup/menu/visibility consumers with synthetic backends, not
live WindowServer performance. Snapshots with at least 128 records build a sorted,
immutable ID index in the same allocation as the records. This adds eight bytes
per record and O(n log n) capture work, but changes repeated lookups from O(n)
to O(log n), without reordering records or adding allocation calls. Small and
borrowed snapshots retain linear lookup. Tests cover the threshold, shuffled and
duplicate IDs, zero/maximum IDs, unavailable metadata, and allocation failures.

## Read-only native private-API smoke

`make private-api-smoke` builds and runs a **live**, read-only probe with a
15-second execution deadline. It is never a dependency of `make test`, sanitizer
or architecture suites, `test-all`, or pull-request CI. Build without running it
with `make private-api-smoke-build`.

Run on a quiet, unlocked, logged-in Mac with at least one existing visible app
window, Command Line Tools, and Python 3. The probe does not need a running yabai
daemon or scripting addition. It never creates, closes, moves, or focuses
windows, starts services, installs anything, changes permissions, or requests
Accessibility/Screen Recording access. Do not grant permissions or disable SIP
just to make this smoke test pass.

For attributable evidence and a checked host expectation:

```sh
python3 -B tests/integration/private_api_smoke.py \
  --probe bin/debug/private-api-smoke \
  --expected-os 27 --expected-arch arm64 \
  --source-revision <tested-commit>
```

The JSON report contains the source revision supplied by the caller, actual OS
version/build, loaded architecture, translation state, check results, and counts
only. Use the exact commit containing the probe, or explicitly identify dirty
working-tree evidence; the caller's revision is not independently attested.
Reports omit display/Space/window IDs, geometry, owners, titles, commands, and
native stderr. Missing APIs, invalid results, wrong host versions/architectures,
Rosetta execution, missing console sessions/windows, and timeouts fail rather
than skip. If windows disappear mid-sample, rerun manually on a quiet host; the
probe does not retry indefinitely.

The checks call real `SLSCopyManagedDisplays`, `SLSCopyManagedDisplaySpaces`,
`SLSManagedDisplayGetCurrentSpace`, `SLSSpaceGetType`,
`SLSCopyWindowsWithOptionsAndTags`, `SLSGetWindowBounds`, and
`SLSWindowIsOrderedIn`, and exercise the production `window_server_snapshot`
implementation against up to 32 existing windows. When the snapshot captures
the optional alpha and Space-count fields, `snapshot_optional_fields` checks
them against `SLSGetWindowAlpha` and `SLSCopySpacesForWindows`, which query
serialization would otherwise call per window. This is ABI/query-path smoke
coverage, **not** confirmation of tiling, focus, wake recovery, native tabs,
resize, animations, scripting-addition behavior, or every window's optional
constraint/corner metadata.

### Prepared-host workflow

`.github/workflows/private-api-smoke.yml` is manual-only. An operator must first
prepare an interactive self-hosted Mac with the standard `self-hosted`, `macOS`
labels and a version/architecture label such as
`yabai-smoke-macos-27-arm64` or `yabai-smoke-macos-14-x86_64`.
Select the OS major (14, 15, 26, 27) and native architecture in the dispatch UI.
The driver validates the selected expectation against the probe's runtime
metadata; Rosetta is never accepted as native Intel evidence. Register only
hosts the operator owns and explicitly approves; run trusted revisions only,
never untrusted pull requests on these machines.

The job has a 10-minute deadline and retains the redacted JSON in its run summary
and logs. Record the workflow-run URL, exact source revision, OS build, and
architecture in the [environment ledger](../scripts/macos_compatibility_matrix.md)
after a real run. A queued job, cross-build, syntax check, or synthetic driver
test is not a passing host row. No runner provisioning, workflow dispatch,
service changes, or deployment is performed by adding this workflow.

The older `make integration-hide-on-close` target remains a separate, explicitly
state-changing UI test. Do not substitute it for this read-only smoke test.
