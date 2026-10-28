# Window constraints probe

This opt-in developer tool creates six temporary AppKit windows owned by the probe, observes their constraints through both known private read-only getters, and closes them. It never resizes, focuses, or otherwise changes an operator window and is not connected to the yabai daemon.

```sh
make window-constraints-probe
bin/debug/window-constraints-probe > /private/tmp/window-constraints.jsonl
```

The fixture matrix includes frame limits, content limits, titlebar/toolbar content limits, a borderless window, an exact fixed-size window, and an unbounded/default window. It also changes one fixture's limits and samples again after 250 ms and 1000 ms.

Each `yabai.window-constraints-probe/v1` observation records:

- AppKit frame/content and declared frame/content limits;
- existing-trust-only AX size readback;
- `SLSWindowIteratorGetConstraints` and `SLSPackagesGetWindowConstraints` results and latency;
- normalization through the production `window_constraints_update()` model;
- whether minimum, maximum, and current values match AppKit frame or content semantics;
- iterator/direct agreement.

The output contains fixture labels but no raw PID, WindowServer ID, title, application name, bundle ID, path, absolute position, Space ID, or display ID. The probe never calls an Accessibility prompting API or a private setter.

## macOS 27.0.1 observation

On build 26A434, eight controlled observations produced iterator and direct-package values in all eight cases, with exact agreement. Decorated frame-limit fixtures reported frame-relative minimum, maximum, and current sizes. Content limits were converted to decorated frame sizes. Borderless and full-size-content controls had identical frame/content dimensions. The unbounded/default fixture returned zero triplets, which production normalization correctly kept unknown. A dynamic frame-limit change was visible by the 250 ms sample and remained stable at 1000 ms.

This supports further evaluation of nullable advisory query fields, but not production layout behavior. The direct-package API added no information or recovery in this fixture, and a single AppKit process on one OS cannot establish behavior for delegate clamps, resize increments, Electron, native tabs, fullscreen, panels, or cross-version freshness.

## Decision policy

Keep constraints diagnostic-only unless controlled observations prove a consistent, documentable frame/content basis, finite and ordered limits, bounded refresh after dynamic changes, and predictive value for accepted resizes. A direct getter that merely duplicates iterator values does not justify production fallback. Current creation-time caching must not drive layout behavior without separately proven invalidation semantics.

Do not add public query fields while basis varies by style or values are invalid, unbounded, stale, or ambiguous. If a later cross-OS corpus supports exposure, fields should be nullable advisory data with an explicit `constraints-known` indicator; AX write/readback remains authoritative.
