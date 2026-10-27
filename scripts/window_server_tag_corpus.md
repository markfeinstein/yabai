# WindowServer tag corpus and classifier comparison

This developer-only tool records strict-redacted WindowServer metadata. It never changes daemon behavior and the candidate classifier is not used by production inventory.

## Build

```sh
make window-server-tag-corpus
```

## Capture

Arrange a scenario manually, allow it to become quiescent, then run:

```sh
bin/debug/window-server-tag-corpus \
  --scenario appkit-standard \
  --output /private/tmp/appkit-standard.jsonl \
  --compare-classifier \
  --labels /private/tmp/appkit-standard.labels.json
```

Capture each labeled state at least twice. Do not commit generated output or label files without an explicit privacy review.

The tool:

- uses one public CG window list and one immutable WindowServer snapshot;
- does not contact the yabai daemon;
- never requests Accessibility or Screen Recording permission;
- uses Accessibility only when it is already authorized;
- does not move, focus, resize, order, minimize, fullscreen, create, or destroy windows;
- creates output with `O_EXCL|O_NOFOLLOW` and mode `0600`;
- emits no raw window IDs, PIDs, titles, application names, bundle IDs, paths, Space/display identities, or geometry.

## Labels

Label files are local, mode `0600`, non-symlink JSON files:

```json
{
  "schema": "yabai.window-server-tag-labels/v1",
  "windows": [
    {
      "window_id": 123,
      "fixture_role": "primary-document",
      "expected_normal": "manageable",
      "expected_include_minimized": "manageable"
    }
  ]
}
```

Allowed fixture roles are:

- `primary-document`
- `root-dialog`
- `child-sheet`
- `floating-panel`
- `menu-negative`
- `overlay-negative`
- `electron-main`
- `electron-helper`
- `fullscreen`
- `split-view`
- `minimized`
- `native-tab-active`
- `native-tab-inactive`
- `native-tab-detached`
- `bit58-ordinary`

Expected classes are `manageable`, `user-facing-nonmanageable`, `system-overlay`, or `uncertain`.

## Evidence matrix

Before any activation PR, repeat and review labeled captures for:

1. AppKit standard documents.
2. Root dialogs, child sheets, and floating panels.
3. Menu, menu-extra, Dock, and system-overlay negatives.
4. Electron main and helper-owned surfaces.
5. Native fullscreen and Split View.
6. Minimized and restored states.
7. Stage Manager off/on and shelf surfaces.
8. Native tabs: active, inactive, detached, and tab-bar states.
9. An ordinary window carrying bit 58.
10. AX-authorized and AX-unavailable sessions.

Required OS coverage is macOS 26.6, 27.0, and 27.2. The 27.2 layout remains `unverified` until independently reviewed.

## Activation gate

The candidate remains diagnostic-only until all labeled candidate-manageable false positives are zero, every disagreement is adjudicated, required positives are explained, bit 58 is proven classification-invariant, AX-unavailable behavior remains conservative, and multi-display/Separate Spaces coverage is complete. Activation requires a separate version-gated PR with explicit fallback and rollback behavior.
