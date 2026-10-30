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

### Bounded lifecycle stress mode

The opt-in stress mode rapidly creates, runs, invalidates, and releases a fresh
link/target for each current screen on every cycle. It is separate from the
ordinary sampling mode and does not change production code paths:

```sh
bin/debug/display-cadence-diagnostic \
  --source-revision REVISION \
  --fixture-revision cadence-stress-v1 \
  --stress --stress-cycles 20 \
  --reinventory-interval 5 \
  --main-thread-load-ms 2 \
  >"${TMPDIR:-/tmp}/yabai-display-cadence-stress.json"
```

Stress mode is bounded to 100 cycles and at most 64 screens. Each cycle has a
fixed 80 ms active interval followed by a 25 ms post-invalidation run-loop
observation. The optional main-thread load is a bounded busy slice (0–10 ms)
after each run-loop iteration, not a system-wide load generator. Screen inventory
is refreshed at the requested interval; if display identities/count change,
lanes and links are rebuilt, while evidence for retained display identities is
carried forward. Screen facts are refreshed even when the topology is stable.
Only redacted ordinals are emitted. `cycles_with_all_lanes_callback`, per-lane
`cycles_seen`/`cycles_with_callback`, the maximum last-callback wall-time skew,
link creation failures, and callbacks observed during the bounded invalidation
wait are diagnostics, not rollout criteria.

`topology_changes_observed: 0` means that this run did not exercise the rebuild
path. Proving physical attach/detach, mixed-scale, or mixed-refresh behavior
still requires an appropriate multi-display fixture; the tool does not simulate
or change display topology.

## Schema and redaction

Ordinary sampling emits one JSON object with schema `yabai.display-cadence/v1`;
stress mode emits `yabai.display-cadence-stress/v1`. The ordinary schema records
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
timestamp monotonicity, finite-value rejection, stable JSON output, bounded
stress metadata, and redaction. The percentile convention is nearest-rank: p50
and p95 select the smallest rank whose cumulative count reaches the requested
percentile.

## Interpretation and environment limits

`callbacks_after_invalidate: 0` is the expected lifecycle result. A nonzero
value is evidence that callback delivery continued during the bounded wait; it
is not a reason to alter production scheduling. `target_timestamp_monotonic`
and cadence statistics describe observations only and do not establish that a
single production animation clock can safely span displays.

Mixed-refresh conclusions require simultaneous displays with different current
refresh modes. The reviewed two-second live run exposed two 2× displays at 60 Hz
and 120 Hz. Target-timestamp p50 was 16.668 ms and 8.333 ms; p95 was 20.586 ms
and 9.301 ms. Wall-clock callback p95 was 32.474 ms and 14.253 ms, exceeding one
nominal display interval on both screens under this run-loop sampler. All target
timestamps remained strictly monotonic, 301 callbacks were observed before
invalidation, and zero callbacks arrived during the bounded post-invalidate
wait. This is useful blocking evidence, not a production pacing pass. No display
was attached and no preferences were changed.

A live stress-mode run on the same arm64 macOS 27.0.1 host used 20 cycles,
5-cycle reinventory, and a 2 ms main-thread load. It observed one 2× 120 Hz
screen, completed 20/20 cycles, created 20 links without failures, and recorded
200 active callbacks with zero callbacks during invalidation waits. All 20
cycles received a callback on the only lane; maximum lane skew was therefore
0.0 seconds and does not establish multi-lane synchronization. Three reinventory
passes found no topology changes, so the physical topology-rebuild path was not
exercised. The maximum observed invalidation wait was 25.637 ms. Evidence was
written outside the repository and contained no display identifiers or names.
