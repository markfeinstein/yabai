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

## Confirmed macOS 27.0.1 ABI

Read-only arm64 disassembly on build 26A434 confirms that both exported functions use the same eight-argument C ABI:

```c
(connection, filter_window_id, flags, reserved,
 screen_point, window_point_out, window_id_out, window_connection_out)
```

Both truncate the input point to signed integer coordinates, call the same internal `_CGSFindWindow` routine, and decode the same output layout. The distinguishing behavior is an internal mode argument: legacy passes `0`, while `SLSFindWindowByGeometry` passes `1`. The geometry result is therefore a different semantic query, not a drop-in implementation of the legacy query.

Schema v2 reports that internal mode distinction plus a conservative relationship classification:

- `same-window`;
- `parent-child`;
- `associated`;
- `same-owner`;
- `unrelated`;
- `unavailable`.

It also checks each result independently against WindowServer bounds and the returned local point. Raw IDs remain omitted unless `--verbose-ids` is explicitly supplied.

The basic `(filter=0, flags=1, reserved=0)` and filtered `(filter=legacy_window_id, flags=-1, reserved=0)` forms are retained as diagnostic inputs only. They are inherited from the legacy call pattern; their equivalence is not assumed.

Controlled macOS 27.0.1 fixtures produced stable disagreements for ordinary, overlapping, panel, transparent, and sheet points. Disagreement is evidence of different semantics and does not imply either API is incorrect.

Before any activation PR, explain those relationships across display boundaries, multiple/mixed-scale displays, yabai proxies, JankyBorders, macOS 26.6, and macOS 27.2. Production mouse, drag/drop, focus-follows-mouse, and `--window mouse` behavior must continue using the legacy result.
