<!-- Please be careful editing the below HTML, as GitHub is quite finicky with anything that looks like an HTML tag in GitHub Flavored Markdown. -->
<p align="center">
  <img width="75%" src="assets/banner/banner.svg" alt="Banner">
</p>
<p align="center">
  <b>Tiling window management for the Mac.</b>
</p>
<p align="center">
  <a href="LICENSE.txt">
    <img src="https://img.shields.io/github/license/asmvik/yabai.svg?color=green" alt="License Badge">
  </a>
  <a href="doc/yabai.asciidoc">
    <img src="https://img.shields.io/badge/view-documentation-green.svg" alt="Documentation Badge">
  </a>
  <a href="https://github.com/asmvik/yabai/wiki">
    <img src="https://img.shields.io/badge/view-upstream_wiki-green.svg" alt="Upstream Wiki Badge">
  </a>
  <a href="CHANGELOG.md">
    <img src="https://img.shields.io/badge/view-changelog-green.svg" alt="Changelog Badge">
  </a>
  <a href="https://github.com/asmvik/yabai/releases">
    <img src="https://img.shields.io/github/commits-since/asmvik/yabai/latest.svg?color=green" alt="Upstream Version Badge">
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

### Nix

The flake supports `aarch64-darwin` and `x86_64-darwin`. It requires the Xcode command line tools because yabai links against macOS system and private frameworks.

- `nix build` builds the optimized universal binary and man page.
- `nix build .#head` builds an ad-hoc-signed `Yabai.app` from the pinned source revision.
- `nix run . -- --version` runs the flake package.
- `nix develop` enters a shell with the repository's build, test, and documentation tools.
- `nix flake check` builds the source package, application bundle, and Home Manager evaluation checks.

### Home Manager

The flake exports `homeManagerModules.yabai`. The module defaults to `head`, which means the exact source revision pinned by the consumer's `yabai` flake input; updating that input advances the build. Source builds are ad-hoc signed development bundles until CI publishes immutable Developer ID-signed artifacts.

```nix
{
  inputs.yabai.url = "github:markfeinstein/yabai/main";

  outputs = { home-manager, nixpkgs, yabai, ... }: {
    homeConfigurations.mark = home-manager.lib.homeManagerConfiguration {
      pkgs = nixpkgs.legacyPackages.aarch64-darwin;
      modules = [
        yabai.homeManagerModules.yabai
        {
          home = {
            username = "mark";
            homeDirectory = "/Users/mark";
            stateVersion = "26.05";
          };

          services.yabai = {
            enable = true;
            install.source = "head";
            config.nativeConfig = {
              version = 3;
              config.layout = "bsp";
            };
          };
        }
      ];
    };
  };
}
```

The module installs the selected application package, validates native configuration with `yabai --check-config`, writes the default XDG configuration paths, and manages a direct `com.asmvik.yabai` GUI LaunchAgent. Disable any nix-darwin, Homebrew, or hand-written yabai agent before enabling it so two daemons cannot race for the same runtime resources. The module refuses to overwrite an unmanaged or locally modified `com.asmvik.yabai` plist unless `services.yabai.service.force = true` is set explicitly.

By default the agent launches the immutable executable inside the selected package. A configuration that enables Home Manager's Darwin application copying can instead launch the stable copied bundle path so macOS privacy grants apply to the application users see in System Settings:

```nix
services.yabai.service.executable =
  "${config.home.homeDirectory}/Applications/Home Manager Apps/Yabai.app/Contents/MacOS/yabai";
```

The service activation runs after Home Manager's `copyApps` step and refuses to load the agent if the configured executable does not exist or is not executable.

Available installation sources are:

- `head`: compile the revision pinned by the flake input. This is the current default.
- `commit`: fetch and compile an explicit revision using `install.commit.rev` and its Nix source `hash`.
- `nightly`: install a CI-produced signed app archive pinned by `url`, unpacked `hash`, and `version`.
- `release`: install a signed release archive with the same pinned artifact metadata.

For example, an explicit source revision uses:

```nix
services.yabai.install = {
  source = "commit";
  commit = {
    rev = "8955858e10ec6bb04ec4bd8c17a443e94c02b200";
    hash = "sha256-...";
    version = "7.1.25-unstable-8955858e10ec"; # optional
  };
};
```

A future signed release uses an archive whose root contains `Yabai.app`:

```nix
services.yabai.install = {
  source = "release";
  release = {
    url = "https://github.com/markfeinstein/yabai/releases/download/v7.2.0/Yabai.zip";
    hash = "sha256-...";
    version = "7.2.0";
    # appPath = "Yabai.app"; # relative to the unpacked archive root
  };
};
```

Nightly builds use the equivalent `install.nightly` options. Artifact hashes are mandatory: the module never follows an unpinned mutable download. The archive is verified with `codesign --verify --deep --strict` while packaging and is not stripped, re-signed, or otherwise modified.

The module manages only the user service. It intentionally does not install the optional scripting addition, which requires root access and a separate SIP-aware system configuration.

## Testing

`make test` runs the ordinary, offline behavior suite. `make test-asan` and `make test-tsan` compile and execute the core and runtime-path suites with ASan+UBSan and TSan+UBSan respectively. `make test-arm64`, `make test-x86_64`, and `make test-architectures` build thin core binaries, verify their slices, and execute only when the native architecture or Rosetta is available. `make syntax` checks independently compiled pure modules and public headers. `make analyze` runs the whole-program Clang static analyzer and rejects diagnostics not present in the reviewed baseline. `make man-contract` is generator-independent and checks deterministic source/generated numeric-domain contracts. `make man-check` requires Asciidoctor and performs both the contract check and regeneration-parity comparison. `make test-all` combines the generator-independent offline gates. Build and test outputs are configuration-specific, so one target does not delete another target's artifacts.

For focused core checks, build with `make -C tests core-build`, then run `tests/bin/ordinary/tests --list` or `tests/bin/ordinary/tests --filter <substring>`. Listing and filtering compose; no match fails rather than silently passing. See [development testing](doc/testing.md) for exact argument and exit-status behavior.

`make fuzz-build` builds deterministic ASan+UBSan fuzz-smoke binaries for regex patterns, daemon frames, Mach-O parsing, and native JSONC configuration parsing under `bin/fuzz/`. `make fuzz-smoke` runs a bounded 1,000-input pass for each harness. The harness sources expose the standard `LLVMFuzzerTestOneInput` entry point and can also be linked with libFuzzer when its runtime is installed.

`make private-api-smoke` is an opt-in, read-only **live** WindowServer query check. It needs a logged-in native Mac with an existing visible app window, but no running daemon or scripting addition. It records redacted OS/build/architecture evidence and fails on missing prerequisites or a 15-second probe timeout. The manual prepared-host workflow supports macOS 14, 15, 26, and 27 on native arm64 or x86_64; it does not provision hosts or treat cross-builds as live evidence. See [development testing](doc/testing.md) for scope, safety, and runner setup.

The live `make integration-hide-on-close` target is deliberately excluded from ordinary, sanitizer, architecture, and pull-request CI. It requires a current `bin/debug/yabai`, a reachable running daemon, `jq`, `swiftc`, UI scripting/Accessibility permission, and a current BSP Space. It opens a persistent anchor and a target window, changes window focus, and uses System Events to click the target close button; cleanup terminates the fixture. The self-hosted workflow enables strict mode, so missing prerequisites are failures. Its bounded integration diagnostic, `yabai -m query --daemon-identity`, reports the responding daemon PID, loaded architecture, and loaded main-image Mach-O UUID so the fixture can match the running image to the freshly built slice. Interactive local callers may explicitly set `YABAI_INTEGRATION_ALLOW_SKIP=1` to skip only unsupported host capabilities; stale binaries, daemon mismatches, unreachable daemons, and invalid layout remain failures. It does not load the scripting addition, alter services, sign, install, or deploy.

## Installation and Configuration

- The [&nearr;&nbsp;upstream&nbsp;yabai&nbsp;wiki][yabai-wiki] covers general installation and uninstallation. For this fork’s configuration and added features, use the [local documentation][yabai-docs] and [examples][yabai-examples]; upstream documentation and releases may differ.
- Native settings, per-Space policy, named layout modes, persistent rules, and global keyboard workflows can be written in `~/.config/yabai/yabai.jsonc`. Version 3 adds application- and focused-window-conditional shortcuts, layout-aware names and raw keycodes, sided/Fn/Hyper modifiers, timed transient input modes, synthetic remapping, typed native actions, in-process yabai commands, and ordered action sequences without skhd or per-action subprocesses. Version 2 layout modes can still be selected with `yabai -m mode --activate <name>` and inspected with `yabai -m mode --query` or `--list`. Validate the file without starting the daemon with `yabai --check-config ~/.config/yabai/yabai.jsonc`. See [native hotkeys](doc/native_hotkeys.md) and the [complete parity example](examples/native-hotkeys.jsonc).
- Run `yabai --doctor` for bounded, read-only `PASS`/`WARN`/`FAIL`/`UNKNOWN` checks of the yabai binary, permissions, SIP requirements, launch agent, configuration readability, and optional scripting addition. It never changes services or system state and returns nonzero only when a check fails.
- Native JSONC loads before `yabairc`. Keep `yabairc` only for runtime overrides, signals, startup actions, or shell logic not represented by typed native configuration. Sample files are in the [&nearr;&nbsp;examples][yabai-examples] directory; the complete JSONC schema and reload behavior are in the [&nearr;&nbsp;documentation][yabai-docs].
- Yabai can own global keyboard shortcuts directly. External hotkey daemons remain optional for workflows outside the native action inventory.

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
|Secure&nbsp;Input|macOS suspends global keyboard taps while Secure Input is active (for example, in some password fields). Native shortcuts resume when Secure Input is released; `yabai --doctor` reports the current state.|
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
[yabai-examples]: examples
[yabai-docs]: doc/yabai.asciidoc

<!-- Links to other GitHub projects/users -->
[gh-skhd]: https://github.com/asmvik/skhd
[gh-fools-mate]: https://github.com/fools-mate
[gh-dominiklohmann]: https://github.com/dominiklohmann

<!-- External links -->
[yabai-wiki]: https://github.com/asmvik/yabai/wiki
[external-about-sip]: https://support.apple.com/en-us/HT204899
