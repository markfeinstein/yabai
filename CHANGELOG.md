# Changelog

All notable changes to the FeinSoftware yabai fork are documented in this file.

The fork uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html) beginning at
version 0.0.1. The inherited upstream release history through the fork point is
preserved separately in [UPSTREAM_CHANGELOG.md](UPSTREAM_CHANGELOG.md).

## [Unreleased]

### Added
- Update Home Manager stable and nightly artifact pins automatically after signed and notarized publication, including checksum and identity verification, normalized Nix hashing, downgrade protection, race-safe publication, and nightly feedback-loop prevention.
- Schedule a Pacific-time nightly with two recovery attempts that creates and dispatches a signed build only when `main` has changed, while suppressing duplicate runs and safely retrying an unpublished immutable nightly tag.
- Let the Home Manager module ensure a minimum global count of ordinary Spaces, creating missing Spaces after service startup and reloading settings so native rule targets can bind.

### Changed
- Reuse tracked keyboard delivery decisions on key release, avoiding redundant native modifier queries and shortcut matching while preserving pass-through and consumed key pairs.
- Allocate queued native shortcut jobs for their actual action count instead of always reserving the maximum 16-action sequence.
- Resolve a scratchpad window's Space and stickiness from one WindowServer membership query per toggle instead of two.
- Resolve paired Mission Control indices for `space_changed` signals, Space swaps and moves, and gesture-based Space focus from one managed-topology copy instead of two.
- Require macOS 26 or 27 on Apple Silicon, publish thin arm64 releases with a 26.0 deployment floor, and remove Intel builds, implementation paths, tests, and package outputs.
- Upgrade checkout and artifact-transfer workflow actions to pinned Node.js 24 releases.
- Enable the menu bar by default with the panel interface and automatic layout, and default auto-balance and native edge resizing to on.
- Move the Home Manager interface to `programs.yabai` and expose declarative JSONC through the conventional `settings` and `settingsFile` options.

### Fixed
- Reject missing scratchpad mutation targets instead of dereferencing null window pointers.
- Move hosted CI, signing, and artifact verification to macOS 26 Apple Silicon runners.
- Run release-note tooling from the release workflow's own revision so an immutable nightly tag created before a tooling change can still be built and published.

## [0.0.1] - 2026-11-08

This is the first independently versioned fork release. It is based on upstream
yabai 7.1.25 at commit `dd845723416f5fe92af49fad5ebab00369e07edd` and includes
all fork changes through this release.

### Added
- Export a Home Manager module that builds the pinned source revision by default and can select an explicit commit or pinned signed nightly/release application archive.
- Match window rules by Accessibility identifier and expose that identifier in window queries.
- Expand native keyboard workflows with layout-aware key names, F-key/navigation/keypad names, Fn/Hyper and sided modifiers, idle mode timeouts, mode query/menu/signal status, cross-display robust focus, and in-process `yabai_command` actions.
- Ship a full-bleed macOS application and notification icon, and let the menu-bar restart action relaunch unmanaged yabai instances.

### Fixed
- Give the native menu-bar status item a stable AppKit autosave identity so Bartender and macOS preserve one item instead of accumulating and misplacing `Item-N` identities across restarts.
- Let the Home Manager service launch a stable copied `Yabai.app` executable so macOS privacy grants and the managed LaunchAgent identify the same application.
- Treat the `SecondaryFn` flag that macOS adds to navigation events without a physical Fn press as optional unless a binding explicitly requires Fn, restoring Ctrl+Arrow Space switching.
- Reject the impossible zero value returned by the optional WindowServer iterator Space-count getter on macOS 27.2, preserving the authoritative per-window Space-membership fallback.
- Bound menu-initiated launchctl commands, verify launchd job ownership consistently, and report rejected restart requests.
- Preserve complete truncated JSON error paths and type-check daemon failure format strings at compile time.

### Changed
- Track WindowServer app-unresponsive/app-responsive notifications on macOS 14 and 15, avoiding Accessibility reads and writes while an application is hung and refreshing it after recovery; newer WindowServer generations retain the existing bounded behavior pending ABI evidence.
- Drive window animations from the destination display's refresh cadence instead of the aggregate active-display clock.
- Require meaningful horizontal progress or terminal velocity before committing a direct trackpad Space switch, so small or cancelled swipes do not change Spaces.
- Add opt-in direct native Space switching through the validated WindowManagement set-and-hide bridge, including animation-free explicit focus and horizontal trackpad swipe interception with native fallback when the private ABI is unavailable.
- Add a `manage` policy that can float windows by default while preserving rule-managed and manually tiled windows across runtime changes and native configuration reloads.
- Add `window_sublayer_auto` control for keeping automatically layered tiled and floating windows in the normal sub-layer while preserving explicit layer choices.
- Add opt-in live BSP sibling reflow during modifier-assisted resizing. Ordinary macOS edge and corner drags remain pass-through and reconcile the real BSP layout once on release; remove the rejected divider and captured-proxy presentation paths.
- Restore capability-gated scripting-addition Space creation, destruction,
  reordering, display movement, focus, and animation lookup support on macOS 27,
  including the macOS 27.2 Dock Swift move-report handler and both arm64e slices.
- Bound Dock byte-pattern searches to executable code and encode the Intel
  zero-duration animation patch in the correct little-endian instruction order.
- Reject rule and signal regular expressions whose compiled automata would grow
  far beyond their input, while retaining the full literal request capacity.
- Retry temporarily unavailable AX window identities with bounded backoff,
  preserve the configured origin captured when a window opened, and keep later
  inventory recovery on the window's existing Space.
- Re-tile reordered-in windows on their own visible display, recover a drag
  whose mouse-up event was lost, and coalesce delayed per-Space reconciliation
  for applications that silently destroy windows.
- Add an opt-in, click-through `active_window_highlight` outline for the focused
  eligible window, with configurable color, radius, and width; fail closed during
  Mission Control, native fullscreen, visibility transitions, and yabai animations.
- Add opt-in proportional `single_window_width_ratio` geometry, with an
  absolute `single_window_min_width` floor and an optional
  `single_window_max_width` ceiling, that centers one managed window while
  preserving ordinary multi-window layouts; `zoom-fullscreen` temporarily
  bypasses responsive singleton sizing.
- Choose the first automatic BSP split from the ordinary full root when a
  responsive singleton opens a second managed window.
- Compile the core test translation unit with warnings treated as errors while
  retaining narrow exceptions for deliberate fixtures and legacy APIs.
- Parse the injected process's main-image Mach-O load commands directly instead
  of using deprecated segment lookup APIs.
- Pass the embedded Info.plist section flag only when linking daemon binaries,
  not when compiling the standalone menu-bar model object.
- Recover windows whose accessibility reference is replaced, and adopt focused
  windows when applications omit `kAXCreatedNotification` [#2836](https://github.com/asmvik/yabai/issues/2836) [#2837](https://github.com/asmvik/yabai/issues/2837).
- Preserve BSP membership and complete-tree geometry across native-fullscreen
  and zoom transitions.
- Move cross-Space swap participants before mutating their layout trees, with
  bounded postcondition checks and rollback when a transport fails.
- Prefer the observable scripting-addition window-move operation, then verify
  bridged and compatibility fallbacks instead of accepting silent asynchronous
  failure [#2789](https://github.com/asmvik/yabai/issues/2789).
- Add exact Intel Sonoma 14.8.5 and 14.8.9 scripting-addition scan windows and
  build-specific desktop-picture handling [#2839](https://github.com/asmvik/yabai/issues/2839) [#2827](https://github.com/asmvik/yabai/issues/2827).
- Have `yabai --doctor` perform a bounded, read-only live scripting-addition
  handshake after validating socket ownership and permissions.
- Report WindowServer notification-subscription failures and avoid cursor warps
  for focus transitions involving ineligible auxiliary windows.
- Restore *focus follows mouse* when an application terminates while one of its
  menus is open, without consuming menu-bar-owned suspension state [#2834](https://github.com/asmvik/yabai/issues/2834).
- Avoid permanently blocking Space operations on Big Sur when its private
  display-animation query remains latched after application activation [#2841](https://github.com/asmvik/yabai/issues/2841).
- Keep drag insertion feedback consistent for source stacks and stack-layout
  destinations [#2840](https://github.com/asmvik/yabai/issues/2840).
- Refresh complete BSP geometry after window removal so surviving windows use
  current display bounds without changing configured split ratios.
- Leave actively dragged cross-display windows under mouse-up ownership instead
  of reconciling them into the destination Space early [#2843](https://github.com/asmvik/yabai/issues/2843).
- Preserve the active Space during daemon startup by applying persistent rule
  target moves without following them until each pre-existing application's
  initial window inventory converges.
- Ignore zero-area helper windows unless an explicit rule manages them.
- Use modern Objective-C ivar accessors for scripting-addition compatibility
  with current Apple Silicon SDKs.
- Fail conservatively when Core Foundation allocation fails during display ordering,
  accessibility checks, shadow configuration, or window/Space list construction.
- Reuse one WindowServer Space-membership result when window queries serialize both
  Space-dependent fields and sticky state, avoiding a duplicate native request per window.
- Deduplicate large application-lifecycle view batches in linear time while preserving
  first-seen flush order and the allocation-free path for ordinary small batches.
- Reuse window, Space and managed-display topology metadata within each integrated
  menu-bar snapshot, avoiding redundant synchronous WindowServer queries per window,
  Space and display.
- Improve the native Spaces indicator with per-display submenus and section headers,
  directional overflow counts, structured accessibility summaries, and an automatic
  layout that expands within a 260-point budget before falling back to compact mode.
- Reuse managed-Space topology and ordered-display metadata across each window-query
  request, avoiding repeated IPC for Space enumeration and serialized Space/display indices.
- Apply the same per-request topology reuse to `query --spaces`, which previously
  re-fetched the full managed-Space topology for every serialized index and display.
- Add non-mutating window diagnostics for raw parent id, WindowServer tags,
  ordered-in state, and CoreGraphics on-screen presence, plus verbose visibility-event
  and Space-focus-path tracing for cross-Space compositor investigations.
- Remove the automatic synthetic Space transition from ordinary passive application
  and window activation. Explicit Space commands and explicitly configured
  Space-follow or fullscreen rules retain their intentional Space operations.
- Accept native rule and Space-policy targets that are not yet present in an
  otherwise valid topology; native rules bind on explicit reload while Space
  policies remain dormant until topology reconciliation finds their target.
- Add ordered managed-window scaffolds to native per-Space policy, with
  opening-order-independent one-, two-, and three-primary BSP geometry.
- Add native managed `accordion_horizontal` and `accordion_vertical` layouts
  with stable focus order, configurable `accordion_padding`, bounded overlapping
  geometry and transactional JSONC startup, reload and topology integration.
- Add native JSONC configuration for settings, per-Space policy and persistent
  rules, with offline validation and transactional runtime reload.
- Add the typed `window_focus_fallback` policy for event-loop-native recent-window
  focus after window destruction, minimization, or application hiding, without
  raising a non-sticky fallback window from an inactive Space.
- Resolve the macOS 27 Dock Spaces singleton through Objective-C runtime
  metadata before falling back to build-specific byte patterns.
- Return scripting-addition operation status to the client so unsupported
  Space operations can use their existing fallback paths.
- Add experimental macOS 27 scripting-addition support for focusing Spaces and
  moving windows between Spaces on Apple Silicon.
- Apply the Tahoe compatibility path to newer macOS versions by default.

- Extract managed-window frame correction from the event loop, enable shadow-variable diagnostics, and expand the JSONC structural parser for maintainability without changing configuration semantics.

### Release engineering
- Reset the fork version line to 0.0.1 with a tracked `VERSION` source shared by C builds, Nix packages, application metadata, and release validation.
- Add distinct stable release and nightly identities. Stable tags use `v<version>`; nightly tags use `nightly-YYYYMMDD` and produce `<version>-nightly.<date>.<commit>` binaries.
- Build immutable `Yabai.app` artifacts signed with the FeinSoftware Developer ID identity, hardened runtime, and secure timestamps.
- Submit protected release artifacts to Apple's automated notary service, retain diagnostic evidence, staple accepted tickets, and verify Gatekeeper acceptance before publication.
- Pin Home Manager release and nightly archives by URL, unpacked hash, exact code-signing identifier, and Apple team.
- Correct literal code-requirement handling in deployment verification so `codesign` compiles the configured requirement instead of treating it as a file path.

### Complete fork change inventory

The following commit subjects provide the complete implementation-level inventory
from the upstream fork point through 0.0.1. Temporary implementation experiments
that were later reverted remain listed for auditability; the sections above
describe the resulting user-visible state.

#### Added capabilities
- `feat: establish yabai fork baseline`
- `feat(test): add test listing and substring filtering`
- `feat(config): define native keyboard keymaps`
- `feat(input): capture hotkeys and dispatch native actions`
- `feat(ipc): expose robust directional focus`
- `feat(input): expand native keyboard workflows`
- `feat(rules): match windows by accessibility identifier`
- `feat(menu-bar): add reload and quit actions`
- `feat(config): describe menu reload changes`
- `feat(menu-bar): add restart action`
- `feat(nix): add Home Manager install profiles`
- `feat(menu-bar): relaunch unmanaged instances on restart`
- `feat(assets): ship a macOS application icon`

#### Correctness and reliability
- `fix(window): stabilize highlights and wake restoration`
- `fix(window): report failed close-button actions`
- `fix(signal): diagnose command dispatch failures`
- `fix(window): reconcile stale minimized state after wake`
- `fix(config): reject truncated discovery paths`
- `fix(process): skip discovery retries for ignored processes`
- `fix(declarative): free space snapshot on startup count mismatch`
- `fix(hashtable): stop table_for shadowing caller loop variables`
- `fix(config): bound native matcher compilation`
- `fix(window): reconcile stale visibility across all Spaces`
- `fix(config): reject blocking special files`
- `fix(window): tolerate unpaired UTF-16 surrogates in AX strings`
- `fix(window): recover stale refs after any frame write`
- `fix(input): harden hotkey lifecycle`
- `fix(input): make native actions report failures`
- `fix(snapshot): reject impossible Space counts`
- `fix(input): ignore implicit Fn on navigation keys`
- `fix(input): preserve key event delivery symmetry`
- `fix(process): retain responsiveness state across queue loss`
- `fix(memory): make stretch buffer growth fallible`
- `fix(snapshot): validate optional window metadata`
- `fix(input): serialize hotkey lifecycle transitions`
- `fix(core): guard invariants and preserve unsigned identifiers`
- `fix(analyze): eliminate accepted analyzer diagnostics`
- `fix(core): make table publication failures explicit`
- `fix(ui): unwind feedback and workspace setup failures`
- `fix(highlight): hide border during Mission Control`
- `fix(input): route Rex settings navigation globally`
- `fix(log): declare logging format and termination contracts`
- `fix(core): perform startup window inventory on the event-loop thread`
- `fix(memory): make scratch string allocation fallible`
- `fix(build): restore window-server-geometry-probe compilation`
- `fix(window): log stuck animation teardown retries`
- `fix(highlight): stabilize border during window moves`
- `fix(highlight): track native window drags at cursor rate`
- `fix(highlight): remove native drag startup jump`
- `fix(highlight): synchronize native drag startup anchor`
- `fix(highlight): hide border during native window moves`
- `fix(highlight): hide border on initial native drag`
- `fix(input): validate private event payloads and properties`
- `fix(highlight): keep border during ordinary content drags`
- `fix(message): grow response buffers within protocol limits`
- `fix(space): reject duplicate startup space identities`
- `fix(menu-bar): stop service on quit and confirm reload`
- `fix(menu-bar): unload active launchd job on quit`
- `fix(menu-bar): let launchd complete quit bootout`
- `fix(service): wait for commands with ignored SIGCHLD`
- `fix(service): verify and recover launchd lifecycle`
- `fix(hotkeys): reject missing occupied space targets`
- `fix(ui): harden notification and menu allocations`
- `fix(highlight): align overlay bounds to pixel grid`
- `fix(nix): support stable Home Manager app paths`
- `fix(menu-bar): stabilize status item identity`
- `fix(service): bound menu launchctl commands with deadlines`
- `fix(service): report rejected menu restarts`
- `fix(config): mark truncated JSON error paths`
- `fix(notify): use full-bleed artwork for the notification icon`

#### Performance
- `perf(buffer): use full stretch buffer capacity`
- `perf(snapshot): index repeated window lookups`
- `perf(ffm): avoid repeated level queries during autoraise`
- `perf(space): reuse topology during space queries`
- `perf(mouse): cache drag top clamp per display`
- `perf(ffm): skip autoraise window-list capture without floating candidates`
- `perf(space): resolve Space visibility from captured topology`
- `perf(query): reuse WindowServer snapshot metadata`
- `perf(window): read enhanced-UI state once per app per layout flush`
- `perf(space): check window Space membership without copying the list`
- `perf(window): read resize frame and fullscreen state in one AX batch`
- `perf(input): isolate keyboard event tap`
- `perf(core): reduce hot-path work`
- `perf(process): avoid AX calls to hung applications`

#### Internal architecture
- `refactor(core): remove unreachable legacy paths`
- `refactor(test): reduce harness and override duplication`
- `refactor(config): drive imperative settings from descriptors`
- `refactor(test): consolidate fixture inspection`
- `refactor(tools): share probe support`
- `refactor(window): simplify stale frame reference detection`
- `refactor(macos): modernize runtime integrations`
- `refactor(build): extract independently compiled modules`
- `refactor(core): extract focus follows mouse state`
- `refactor(window): extract window animation module`
- `refactor(message): extract token and value parsing module`
- `refactor(message): extract mode-domain message handler`
- `refactor(mission-control): consolidate state and logic into mission_control.c`
- `refactor(service): share one waitable command runner`
- `refactor(service): share launchd job ownership verification`
- `refactor(event-loop): extract managed window frame correction`

#### Style
- `style(config): expand the dense JSONC structural parser`

#### Build and packaging
- `build(nix): add flake packaging`
- `build: add static analysis and broader CI`
- `build: preserve analyzer diagnostic locations`
- `build: enable stricter initialization and fall-through diagnostics`
- `build(signing): package Developer ID app releases`
- `build(version): reset fork numbering to 0.0.1`
- `build(message): type-check daemon_fail format strings`
- `build: enable -Wshadow and remove shadowed locals`

#### Release automation
- `ci(release): publish signed and notarized app channels`

#### Testing
- `test(macos): add opt-in native private-API smoke validation`
- `test: strengthen native validation coverage`
- `test(window): initialize constraint generations in fixtures`
- `test(input): make mode timeout tests deterministic`
- `test(fuzz): add bounded parser fuzz targets`
- `test(fuzz): cover declarative configuration parsing`
- `test(analyze): cover all linked modules`

#### Documentation
- `docs: link fork documentation and examples locally`
- `docs(config): document native hotkey migration`
- `docs(changelog): start fork history at 0.0.1`

#### Reverted experiments
- `revert(highlight): restore native drag cursor anchoring`
- `revert(highlight): remove live native drag anchor`
