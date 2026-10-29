# Per-display CADisplayLink cadence diagnostic

`display-cadence-diagnostic` is an opt-in developer tool for collecting aggregate,
strict-redacted evidence from public AppKit `NSScreen` and QuartzCore
`CADisplayLink` APIs. It creates one link for every `NSScreen` in the current
snapshot, schedules each link on a private probe process run loop, samples for
a bounded duration, invalidates every link, and waits for a bounded interval to
count callbacks after invalidation.

It does **not** change yabai settings, preferences, Reduce Motion, the scripting
addition, SIP, TCC state, daemon configuration, or production animation
wiring. `production_backend_unchanged` is emitted as `true` deliberately: this
is evidence only, not a CADisplayLink rollout or an animation-backend proposal.

## Build and run

```sh
make display-cadence-diagnostic
bin/debug/display-cadence-diagnostic \
  --source-revision f699bc5ffd65f1e97cfe22352e4cec85041d70f8 \
  --fixture-revision display-cadence-fixture-v1 \
  --duration-seconds 1 \
  --max-samples 1000 \
  >"${TMPDIR:-/tmp}/yabai-display-cadence.json"
```

Keep generated evidence outside the repository and delete it after analysis.
The duration is bounded to 60 seconds and samples are bounded to 10,000 per
screen. The invalidation wait is fixed at 250 ms and is reported in the
lifecycle object. The process does not open a window or request protected
permissions.

## Schema and redaction

The output is one JSON object with schema `yabai.display-cadence/v1`. It records
source and fixture revisions, OS/build/architecture provenance, requested
bounds, aggregate per-screen ordinals, nominal refresh (`maximumFramesPerSecond`),
current public Core Graphics mode refresh, backing scale, callback count,
target-timestamp monotonicity, and p50/p95/min/max target and wall-clock deltas.
The lifecycle section records links created for the current screen snapshot,
callbacks before invalidation, callbacks after invalidation, and whether the
bounded wait completed.

Screen ordinals are positional only. The output never serializes display IDs,
UUIDs, screen names, application names, absolute origins, window identifiers,
or paths. The current mode is queried through public Core Graphics APIs using
the screen's public AppKit device description internally; that identifier is
never written to evidence.

The pure core has deterministic tests for nearest-rank p50/p95 statistics,
timestamp monotonicity, finite-value rejection, stable JSON output, and
redaction. The percentile convention is nearest-rank: p50 and p95 select the
smallest rank whose cumulative count reaches the requested percentile.

## Interpretation and environment limits

`callbacks_after_invalidate: 0` is the expected lifecycle result. A nonzero
value is evidence that callback delivery continued during the bounded wait; it
is not a reason to alter production scheduling. `target_timestamp_monotonic`
and cadence statistics describe observations only and do not establish that a
single production animation clock can safely span displays.

Mixed-refresh conclusions require simultaneous displays with different current
refresh modes. The live run used for this slice exposed two current displays,
with aggregate nominal/current modes of 60 Hz and 120 Hz respectively, so this
host had no external-display availability blocker for the mixed-refresh slice.
No display was attached and no preferences were changed.
