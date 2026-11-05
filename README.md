<!-- Please be careful editing the below HTML, as GitHub is quite finicky with anything that looks like an HTML tag in GitHub Flavored Markdown. -->
<p align="center">
  <img width="75%" src="assets/banner/banner.svg" alt="Banner">
</p>
<p align="center">
  <b>Tiling window management for the Mac.</b>
</p>
<p align="center">
  <a href="https://github.com/asmvik/yabai/blob/master/LICENSE.txt">
    <img src="https://img.shields.io/github/license/asmvik/yabai.svg?color=green" alt="License Badge">
  </a>
  <a href="https://github.com/asmvik/yabai/blob/master/doc/yabai.asciidoc">
    <img src="https://img.shields.io/badge/view-documentation-green.svg" alt="Documentation Badge">
  </a>
  <a href="https://github.com/asmvik/yabai/wiki">
    <img src="https://img.shields.io/badge/view-wiki-green.svg" alt="Wiki Badge">
  </a>
  <a href="https://github.com/asmvik/yabai/blob/master/CHANGELOG.md">
    <img src="https://img.shields.io/badge/view-changelog-green.svg" alt="Changelog Badge">
  </a>
  <a href="https://github.com/asmvik/yabai/releases">
    <img src="https://img.shields.io/github/commits-since/asmvik/yabai/latest.svg?color=green" alt="Version Badge">
  </a>
</p>

## About

<img align="right" width="40%" src="assets/screenshot.png" alt="Screenshot">

yabai is a window management utility that is designed to work as an extension to the built-in window manager of macOS.
yabai allows you to control your windows, spaces and displays freely using an intuitive command line interface and optionally set user-defined keyboard shortcuts using [&nearr;&nbsp;skhd][gh-skhd] and other third-party software.

The primary function of yabai is tiling window management; automatically modifying your window layout using a binary space partitioning algorithm to allow you to focus on the content of your windows without distractions.
Additional features of yabai include focus-follows-mouse, disabling animations for switching spaces, creating spaces past the limit of 16 spaces, and much more.

## Building from Source

`make` creates a universal debug binary at `bin/debug/yabai`. Optimized, AddressSanitizer, and ThreadSanitizer builds are available with `make release`, `make asan`, and `make tsan`, producing `bin/release/yabai`, `bin/asan/yabai`, and `bin/tsan/yabai`. The configurations coexist; use `make clean-debug`, `clean-release`, `clean-asan`, or `clean-tsan` to remove one, `make clean` to remove all build configurations, and `make distclean` to also remove archives under `release/`.

`make install` remains an optimized-build alias and does not copy files. `make archive` writes installer-compatible archives under `release/`, with an `archive/` top-level directory. Run `make sign` explicitly before `make deploy`; deploy never invokes the signer and instead requires the existing optimized artifact to satisfy an explicit trusted `DEPLOY_REQUIREMENT` that pins a cryptographic signing identity (for example, an exact requirement containing the leaf certificate hash). It then installs that verified artifact and reloads the scripting addition. There is no permissive requirement default. If scripting-addition reload or service restart fails after final verification, deploy reports failure but retains the verified binary.

## Testing

`make test` runs the ordinary, offline behavior suite. `make test-asan` and `make test-tsan` compile and execute the core and runtime-path suites with ASan+UBSan and TSan+UBSan respectively. `make test-arm64`, `make test-x86_64`, and `make test-architectures` build thin core binaries, verify their slices, and execute only when the native architecture or Rosetta is available. `make syntax` checks independently compiled pure modules and public headers. `make man-contract` is generator-independent and checks deterministic source/generated numeric-domain contracts. `make man-check` requires Asciidoctor and performs both the contract check and regeneration-parity comparison. `make test-all` combines the generator-independent offline gates. Build and test outputs are configuration-specific, so one target does not delete another target's artifacts.

The live `make integration-hide-on-close` target is deliberately excluded from ordinary, sanitizer, architecture, and pull-request CI. It requires a current `bin/debug/yabai`, a reachable running daemon, `jq`, `swiftc`, UI scripting/Accessibility permission, and a current BSP Space. It opens a persistent anchor and a target window, changes window focus, and uses System Events to click the target close button; cleanup terminates the fixture. The self-hosted workflow enables strict mode, so missing prerequisites are failures. Its bounded integration diagnostic, `yabai -m query --daemon-identity`, reports the responding daemon PID, loaded architecture, and loaded main-image Mach-O UUID so the fixture can match the running image to the freshly built slice. Interactive local callers may explicitly set `YABAI_INTEGRATION_ALLOW_SKIP=1` to skip only unsupported host capabilities; stale binaries, daemon mismatches, unreachable daemons, and invalid layout remain failures. It does not load the scripting addition, alter services, sign, install, or deploy.

## Installation and Configuration

- The [&nearr;&nbsp;yabai&nbsp;wiki][yabai-wiki] has both brief and detailed installation instructions for multiple installation methods, and also explains how to uninstall yabai completely.
- Native settings, per-Space policy, named layout modes, and persistent rules can be written in `~/.config/yabai/yabai.jsonc`. This includes the typed `window_focus_fallback` policy for focusing the recent managed window after destruction, minimization, or application hiding without shell-backed signals. Version 2 modes can be selected with `yabai -m mode --activate <name>` and inspected with `yabai -m mode --query` or `--list`. Validate the file without starting the daemon with `yabai --check-config ~/.config/yabai/yabai.jsonc`.
- Run `yabai --doctor` for bounded, read-only `PASS`/`WARN`/`FAIL`/`UNKNOWN` checks of the yabai binary, permissions, SIP requirements, launch agent, configuration readability, and optional scripting addition. It never changes services or system state and returns nonzero only when a check fails.
- Native JSONC loads before `yabairc`. Keep `yabairc` for runtime overrides, signals, startup actions, and shell logic. Sample files are in the [&nearr;&nbsp;examples][yabai-examples] directory; the complete JSONC schema and reload behavior are in the [&nearr;&nbsp;documentation][yabai-docs].
- Keyboard shortcuts can be defined with [&nearr;&nbsp;skhd][gh-skhd] or any other suitable software you may prefer.

## Requirements and Caveats

Please read the below requirements carefully.
Make sure you fulfil all of them before filing an issue.

|Requirement|Note|
|-:|:-|
|Operating&nbsp;System&nbsp;Intel x86-64|Sonoma 14.0.0+, Sequoia 15.0+, Tahoe 26.0+, and Golden Gate 27.0+ are supported.|
|Operating&nbsp;System&nbsp;Apple Silicon|Sonoma 14.0.0+, Sequoia 15.0+, Tahoe 26.0+, and Golden Gate 27.0+ are supported.|
|Accessibility&nbsp;API|yabai must be given permission to utilize the Accessibility API and will request access upon launch. The application must be restarted after access has been granted.|
|Screen Recording|yabai must be given Screen Recording permission if and only if you want to enable window animations, and will request access when necessary. The application must be restarted after access has been granted.|
|System&nbsp;Settings&nbsp;(macOS 14.x, 15.x, 26.x, 27.x)|In the Desktop & Dock tab, inside the Mission Control pane, the setting "Displays have separate Spaces" must be enabled.|

Please also take note of the following caveats.

|Caveat|Note|
|-:|:-|
|System&nbsp;Integrity&nbsp;Protection (Optional)|System Integrity Protection can be (partially) disabled for yabai to inject a scripting addition into Dock.app for controlling windows with functions that require elevated privileges. This enables control of the window server, which is the sole owner of all window connections, and enables additional features of yabai.|
|Code&nbsp;Signing|When building from source (or installing from HEAD), it is necessary to codesign the binary so it retains its accessibility and automation privileges when updated or rebuilt.|
|Finder&nbsp;Desktop|Some people disable the Finder Desktop window using an undocumented defaults write command. This breaks focusing of empty spaces and should be avoided when using yabai. To re-activate the Finder Desktop, run: "defaults write com.apple.finder CreateDesktop -bool true".|
|NSDocument-based&nbsp;Applications|Windows that utilize native macOS tabs such as Terminal and Finder, [do not behave correctly when creating tabs](https://github.com/asmvik/yabai/issues/68). Avoid creating tabs in these applications, consider alternatives that do not use NSDocument's tab system, or make these windows float using rules.|
|System&nbsp;Settings&nbsp;(macOS 14.x, 15.x, 26.x, 27.x)|In the Desktop & Dock tab, inside the Mission Control pane, the setting "Automatically rearrange Spaces based on most recent use" should be disabled for commands that rely on the ordering of spaces to work reliably.|
|System&nbsp;Settings&nbsp;(macOS 14.x, 15.x, 26.x, 27.x)|In the Desktop & Dock tab, inside the Desktop & Stage Manager pane, the setting "Show Items On Desktop" should be enabled for display and space focus commands to work reliably in multi-display configurations.|
|System&nbsp;Settings&nbsp;(macOS 14.x, 15.x, 26.x, 27.x)|In the Desktop & Dock tab, inside the Desktop & Stage Manager pane, the setting "Click wallpaper to reveal Desktop" should be set to "Only in Stage Manager" for display and space focus commands to work reliably.|

## License and Attribution

yabai is licensed under the [&nearr;&nbsp;MIT&nbsp;License][yabai-license], a short and simple permissive license with conditions only requiring preservation of copyright and license notices.
Licensed works, modifications, and larger works may be distributed under different terms and without source code.

Thanks to [@fools-mate][gh-fools-mate] for creating a logo and banner for this project and making them available for free.

Thanks to [@dominiklohmann][gh-dominiklohmann] for contributing great documentation, support, and more, for free.

## Disclaimer

Use at your own discretion.
I take no responsibility if anything should happen to your machine while trying to install, test or otherwise use this software in any form.
You acknowledge that you understand the potential risk that may come from disabling [&nearr;&nbsp;System&nbsp;Integrity&nbsp;Protection][external-about-sip] on your system, and I make no recommendation as to whether you should or should not disable System Integrity Protection.

<!-- Project internal links -->
[yabai-license]: LICENSE.txt
[yabai-examples]: https://github.com/asmvik/yabai/tree/master/examples
[yabai-wiki]: https://github.com/asmvik/yabai/wiki
[yabai-docs]: https://github.com/asmvik/yabai/blob/master/doc/yabai.asciidoc

<!-- Links to other GitHub projects/users -->
[gh-skhd]: https://github.com/asmvik/skhd
[gh-fools-mate]: https://github.com/fools-mate
[gh-dominiklohmann]: https://github.com/dominiklohmann

<!-- External links -->
[external-about-sip]: https://support.apple.com/en-us/HT204899
