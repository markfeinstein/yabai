# WindowServer geometry hit-test probe

This opt-in developer tool compares the existing `SLSFindWindowAndOwner` result with the dynamically resolved `SLSFindWindowByGeometry` result. It never contacts the yabai daemon and does not change window or desktop state.

```sh
make window-server-geometry-probe
bin/debug/window-server-geometry-probe --iterations 500
```

Options:

- `--point X,Y` compares a manually prepared screen coordinate instead of the current cursor.
- `--iterations N` runs an alternating-order latency comparison after warmup.
- `--verbose-ids` temporarily includes raw result IDs on stdout. Default output omits IDs.

The probe validates both the basic `(filter=0, flags=1, reserved=0)` form and the existing filtered `(filter=legacy_window_id, flags=-1, reserved=0)` form. Disagreement is evidence only and does not imply either API is correct.

Before any activation PR, manually compare overlapping windows, child/sheet windows, transparent or shaped regions, panels, multiple displays and display boundaries, yabai proxies, and JankyBorders. Repeat on macOS 27.2. Production mouse, drag/drop, focus-follows-mouse, and `--window mouse` behavior must continue using the legacy result until those disagreements are explained.
