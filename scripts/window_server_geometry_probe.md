# WindowServer geometry hit-test probe

This opt-in developer tool compares the production-authoritative `SLSFindWindowAndOwner` result with the diagnostic `SLSFindWindowByGeometry` result. It never changes yabai production selection, mouse targeting, drag/drop, focus-follows-mouse, or `--window mouse` behavior.

## Build and controlled corpus

```sh
make window-server-geometry-probe
revision=$(git rev-parse --short=12 HEAD)
bin/debug/window-server-geometry-probe \
  --controlled \
  --source-revision "$revision" \
  --fixture-revision controlled-v3 \
  --iterations 200
```

The controlled harness creates and closes only probe-owned AppKit fixtures and samples:

- ordinary client center and rounded-corner cutout;
- the deployed active-highlight inner edge when discoverable;
- same-process overlap;
- utility panel;
- transparent click-through panel;
- attached sheet;
- just inside, exactly on, and just outside the main display’s left edge.

A manual point remains available:

```sh
bin/debug/window-server-geometry-probe \
  --point X,Y \
  --source-revision "$revision" \
  --fixture-revision manual-v1
```

Output omits raw window IDs, connection IDs, PIDs, application identity, titles, paths, display IDs/UUIDs, and absolute coordinates. It records controlled fixture/point roles, redacted result kinds, fractional-to-signed-integer truncation delta, display count/scale, statuses, relationship evidence, bounds/local-point checks, filtered-call results, timing, and provenance.

## Relationship taxonomy

Schema `yabai.window-server-geometry-probe/v3` distinguishes:

- `same-window`;
- `legacy-parent-geometry-child`;
- `legacy-child-geometry-parent`;
- directional or bidirectional association;
- `same-owner-unrelated` and `different-owner-overlap`;
- `legacy-only-hit`, `geometry-only-hit`, and `both-no-hit`;
- per-side error outcomes;
- `relationship-metadata-unknown` when parent/association inspection is unavailable.

Both basic and filtered variants receive a full relationship label and status pair. False and unavailable metadata are not conflated.

## Exact ABI evidence

On macOS 27.0.1 build 26A434, read-only arm64 disassembly established the shared eight-argument ABI. Legacy uses internal mode 0 and geometry uses mode 1. The probe marks that ABI as verified only on that exact build; other versions remain unverified until independently inspected.

## Current macOS 27.0.1 controlled result

With only the internal 2× display active, ten controlled points selected the same WindowServer surface in both APIs. After connecting an external 2× fixed-60 Hz main display beside the built-in 120 Hz display, the same corpus produced six same-window points and four stable different-owner overlaps; those four disagreed in all 100 repetitions. This confirms that mode-0/mode-1 semantics depend on exact stacking and multi-display surface composition rather than timing noise.

The deployed highlight remained click-through in the separate highlight acceptance probe. A real animation proxy or insertion-feedback fixture was not available. Removed external border software is not a supported fixture.

## Remaining gates

Production remains legacy-authoritative until controlled evidence explains relationships across:

- different-process overlap;
- menus and popovers;
- real animation proxies and insertion feedback;
- multiple displays, negative origins, shared boundaries, and mixed scales;
- macOS 26.6 and 27.2.

Do not promote geometry to a fallback or selector from timing or same-window counts alone.
