# Native keyboard control and workflow engine

Yabai schema version 3 adds a typed global-hotkey engine under the top-level
`keyboard` object. The complete skhd-parity configuration is in
[`examples/native-hotkeys.jsonc`](../examples/native-hotkeys.jsonc).

## JSONC schema

```jsonc
{
  "version": 3,
  "keyboard": {
    "enabled": true,
    "bindings": [ /* default-mode bindings */ ],
    "modes": [ /* transient input modes */ ]
  }
}
```

This `keyboard.modes` array is independent of the existing top-level `modes`
array used for declarative Space-policy overlays. Keyboard mode is process-local
and transient; Space-policy mode remains managed by the `yabai -m mode` domain.

### Bindings

```jsonc
{
  // Use either a named key or a physical hardware keycode.
  "key": "h",
  // "keycode": "0x04", // integers and 0x-prefixed strings are accepted
  "modifiers": ["cmd", "ctrl", "alt", "shift"],
  // Defaults to true. A false value still consumes repeated events when the
  // selected disposition consumes; it only suppresses repeated actions.
  "repeat": true,
  "cases": [
    {
      "application": {
        "bundle_id": "com.example.Application",
        "name": "Compatibility Display Name"
      },
      "disposition": "pass_through"
    },
    {
      // An omitted application is the wildcard/default case. It must be last.
      "disposition": "consume",
      "transition": "resize",
      "action": { "type": "focus_window", "direction": "west" }
    }
  ]
}
```

Named keys include `a` through `z`, `0` through `9`, `-`, `=`, arrows,
`space`, `return`, `escape`, and `backspace`. Raw keycodes are matched without
keyboard-layout translation.

Application matching is exact. When both `bundle_id` and `name` are present,
both must match. Bundle identifiers are preferred because display names are
mutable; name-only matching exists for applications such as the current
`Rex Beta` build whose compatibility configuration already uses its display
name.

Bindings with the same keycode and modifier set conflict within one input mode.
Application alternatives therefore belong in one binding's ordered `cases`
array. A default case is optional, unique, and last.

### Event dispositions

- `consume`: suppress the original event and run an action and/or transition.
- `consume_only`: suppress the original event without side effects.
- `pass_through`: deliver the event to the focused application unchanged.
- `remap`: suppress the event and run exactly one `synthesize_key` action.

The event tap decides the disposition before returning from its callback. This
is what prevents native actions such as macOS Cmd-H from racing the configured
behavior.

### Input modes

```jsonc
{
  "name": "resize",
  "capture_unbound": true,
  "bindings": [ /* same binding schema */ ]
}
```

The implicit default mode uses `keyboard.bindings`. A binding case changes mode
with `"transition": "mode-name"`; `"transition": "default"` exits a mode.
Transitions happen synchronously in the event callback, while window-management
actions remain queued. `capture_unbound` consumes otherwise-unmatched key-down
and key-up events in that mode.

A successful configuration reload installs the complete new keymap atomically
and resets keyboard mode to `default`. A rejected reload retains both the old
keymap and its active mode. Process restart also begins in `default`.

### Native actions

An `action` is either one typed object or an ordered array of typed objects.
Arrays execute in order and stop at the first failure, so later actions depend
on earlier success. For example, move-and-follow is:

```jsonc
"action": [
  { "type": "move_window_to_space", "index": 4 },
  { "type": "focus_space", "index": 4 }
]
```

Supported primitives and workflows are:

| Type | Fields | Behavior |
| --- | --- | --- |
| `focus_window` | `direction`, optional `robust` | Directional focus; robust mode adds geometric fallback and native app activation. |
| `warp_window` | `direction` | Warp the focused managed window. |
| `resize_window` | `handle`, `dx`, `dy` | Relative edge/corner resize. |
| `change_window_ratio` | `delta` | Relative BSP ratio change. |
| `adaptive_resize` | `direction` | Existing shrink/grow/edge fallback behavior. |
| `toggle_floating` | — | Toggle focused-window floating. |
| `toggle_zoom_fullscreen` | — | Toggle yabai zoom-fullscreen. |
| `rotate_space` | `degrees` | Rotate by 90, 180, or 270 degrees. |
| `set_space_layout` | `layout` | Set BSP, stack, float, or accordion layout. |
| `balance_space` | — | Balance both axes. |
| `focus_space` | `index` | Focus a Mission Control Space index. |
| `focus_occupied_space` | `direction` | Previous/next occupied Space on the focused display, with wrapping. |
| `move_window_to_space` | `index` | Move the focused window; fails quietly when no suitable window exists. |
| `close_window` | — | Close the focused window. |
| `close_other_windows` | — | Close every other non-minimized window on the current Space, continuing after failures. |
| `cycle_space_layout` | — | BSP → stack → horizontal accordion → vertical accordion → BSP. |
| `reload_configuration` | — | Run the native transactional reload. |
| `restart_service` | `label` | Spawn `/bin/launchctl kickstart -k gui/UID/LABEL`; launchd owns replacement. |
| `synthesize_key` | key/keycode and modifiers | Post a tagged synthetic chord to the active GUI session. |

Directions are `west`, `south`, `north`, and `east`. Adaptive resize directions
also include `shrink` and `grow`. Resize handles are `top`, `bottom`, `left`,
`right`, and their underscore-separated corner combinations.

## Runtime architecture

1. The schema parser validates the complete candidate model, including keys,
   modifiers, actions, mode references, application cases, and trigger
   conflicts. Diagnostics retain the JSON path and source location.
2. Declarative publication swaps the installed immutable model while the hotkey
   manager lock is held. The old model is retired only after the pointer swap.
3. A `kCGHIDEventTap` at the head of the HID stream listens only for key-down
   and key-up events. Disabled-by-timeout and disabled-by-user-input callbacks
   re-enable it.
4. The callback reads only the immutable keymap, active mode, and cached
   frontmost-app identity. It decides consume/pass-through synchronously,
   performs mode transitions, copies at most 16 POD actions into a small job,
   and posts `HOTKEY_ACTION` to yabai's normal event loop.
5. The event-loop worker executes typed operations directly against yabai's
   managers. No `yabai -m` subprocess is created.
6. Synthetic events carry a private `kCGEventSourceUserData` tag. The event tap
   always passes tagged events through, preventing recursion.

The frontmost bundle identifier and display name are refreshed from
`NSRunningApplication` when yabai commits an application-front-switch event.
No arbitrary typed key data or unrelated keyboard activity is logged.

## Higher-level behavior

### Robust focus

Robust focus first uses normal managed-window directional focus and verifies a
focus-ID change. If focus did not move, it examines visible, non-minimized
windows on the active Space, calculates centers, filters to the requested
half-plane, and chooses the lexicographic minimum of:

1. requested-axis distance;
2. secondary-axis distance;
3. total squared distance.

It focuses that window directly and verifies the result. If the target still
does not own focus, yabai activates its `NSRunningApplication`.

### Occupied-Space navigation

Yabai obtains Spaces from the focused display, always includes the current
Space, includes other Spaces only when their window inventory is non-empty,
sorts by Mission Control index, then focuses the previous or next entry with
wraparound.

### Adaptive resize

- shrink: ratio -0.05, then bottom-right -50/-50;
- grow: ratio +0.05, then bottom-right +50/+50;
- west: left -50, then right -50;
- south: bottom +50, then top +50;
- north: top -50, then bottom -50;
- east: right +50, then left +50.

## Permissions and lifecycle

The keyboard tap adds an **Input Monitoring** requirement for the same stable,
signed `/Applications/Yabai.app` identity that already owns yabai's
**Accessibility** grant. Accessibility is also needed to post synthetic events
and operate windows. No scripting addition is required.

On first use, enable Yabai in:

- System Settings → Privacy & Security → Accessibility;
- System Settings → Privacy & Security → Input Monitoring.

The tap is created only when `keyboard.enabled` is true. It is installed and
removed with yabai's application lifecycle. Reload can replace a live keymap,
disable input handling, or enable it without a daemon restart. If a newly
enabled tap cannot be created, yabai reports an Input Monitoring diagnostic.

`restart_service` delegates replacement to the configured launchd label. The
current daemon never attempts to exec its own successor. launchd's single job
instance plus yabai's existing lock file prevents duplicate daemons.

## Migration from skhd and shell helpers

1. Merge `examples/native-hotkeys.jsonc`'s `keyboard` object into the deployed
   yabai JSONC and set `version` to 3.
2. Set the `restart_service.label` to the actual user LaunchAgent label
   (`org.nixos.yabai` in the referenced Nix configuration).
3. Keep Rex's internal private binding:
   `ctrl+alt+cmd+[KeyH] → client.sidebar.toggle`.
4. Replace Rex and Neovim outer-edge helper calls with
   `yabai -m window --focus-robust DIRECTION`.
5. Validate with `yabai --check-config PATH` and reload with
   `yabai -m config reload`.
6. Stop skhd and exercise the acceptance matrix before removing it.
7. Remove the skhd package/service/generated config and activation hook.
8. Remove `focus-window-direction` and the skhd-only helper applications for
   relative Space focus, move-and-follow, layout cycling, adaptive resize,
   close-others, remapping, and restart.

Do not remove the Rex binding itself; yabai synthesizes the private chord that
Rex consumes.

## Test strategy

Automated tests cover:

- schema-version gating, raw and named keys, modifiers, application cases,
  conflicts, invalid transitions, unknown actions, and source paths;
- app-specific matching and wildcard fallback;
- synchronous mode entry/exit, capture-unbound behavior, and synthetic-event
  recursion avoidance;
- ordered action execution and stop-on-failure semantics;
- complete repository core tests and universal debug compilation.

Real macOS acceptance should use the signed application bundle, with skhd
stopped, and verify:

1. Cmd-H is consumed in Rex and non-Rex apps;
2. Rex receives Ctrl-H/J/K/L and Ctrl-Backspace unchanged;
3. robust focus reaches floating and unmanaged native windows;
4. resize and service modes capture unbound keys;
5. occupied-Space navigation skips empty Spaces and wraps;
6. all nine focus and move-follow bindings work by physical keycode;
7. reload failure preserves the old keymap and successful reload resets mode;
8. launchd restart restores the tap without creating a duplicate process;
9. synthetic Cmd-H remapping does not recurse.
