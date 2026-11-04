# Development testing

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
implementation against up to 32 existing windows. This is ABI/query-path smoke
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
