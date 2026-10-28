# Active-window highlight acceptance probe

This opt-in developer tool observes the deployed highlight through real WindowServer and AppKit entry points. It does not enable the feature, change yabai configuration, load the scripting addition, capture screen contents, or request protected permissions.

The JSONL output omits raw window IDs, process IDs, titles, application names, bundle identifiers, paths, Space IDs, display IDs, and absolute geometry. It reports only platform metadata, lifecycle labels, booleans, counts, relative order, and geometry deltas.

## Build

```sh
make active-window-highlight-probe
```

The target creates an ad-hoc-signed application bundle at:

```text
bin/debug/active-window-highlight-probe.app
```

## Controlled AppKit fixture

Use the PID of the already-running deployed daemon and the configured highlight width. Keep output outside the repository and delete it after analysis.

```sh
daemon_pid=$(pgrep -x yabai)
out=$(mktemp "${TMPDIR:-/tmp}/yabai-highlight.XXXXXX.jsonl")
open -W -n bin/debug/active-window-highlight-probe.app \
  --args --fixture "$daemon_pid" "$out" 2
```

The fixture covers:

- ordinary titled windows;
- move and resize;
- a floating-level window;
- an AppKit utility panel;
- rapid focus changes;
- minimize and restore;
- ordered-out and ordered-in lifecycle;
- fullscreen entry and exit;
- native-tab selection;
- close and delayed teardown.

It closes every fixture window before exiting. A supervising script must snapshot the pre-run yabai inventory and focused window, restore focus explicitly, compare post-run frames and Space membership, and compare daemon Mach-port/window counts. Application hide/unhide and termination remain external fixture actions because a self-hiding probe cannot independently observe its own frontmost lifecycle.

## Existing window

The bundle executable can inspect an already-focused AppKit or Electron window without changing focus:

```sh
bin/debug/active-window-highlight-probe.app/Contents/MacOS/active-window-highlight-probe \
  --check-window "$daemon_pid" "$window_id" 2
```

The input identifiers are not emitted. Select and restore the target outside the tool.

## Interpretation

`overlay_matches` must be exactly one for a visible eligible target. Excluded lifecycle states may retain one reusable overlay allocation, but `overlay_on_screen` must be false. A visible matched overlay should:

- be owned by the daemon;
- carry the floating and ignore-for-events tags;
- leave legacy center and inner-edge hit testing on the target;
- have zero `nearest_delta` from the configured-width geometry;
- have a stable relative ordering that is explained for the fixture class.

`overlay_above` and `overlay_adjacent` describe the front-to-back `CGWindowListCopyWindowInfo` order. They are evidence, not an activation selector. Different window classes or OS releases must not be normalized away without a complete z-order explanation.

This probe is not physical accessibility acceptance. Menu navigation, VoiceOver, Reduce Transparency/Increase Contrast, and visible flicker still require direct observation. Cross-version conclusions still require the existing macOS 26.6 and 27.2 environments; do not create or download a VM for this tool.
