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

`make` creates an Apple Silicon debug binary at `bin/debug/yabai` with a macOS 26 deployment target. Optimized, AddressSanitizer, and ThreadSanitizer builds are available with `make release`, `make asan`, and `make tsan`, producing `bin/release/yabai`, `bin/asan/yabai`, and `bin/tsan/yabai`. The configurations coexist; use `make clean-debug`, `clean-release`, `clean-asan`, or `clean-tsan` to remove one, `make clean` to remove all build configurations, and `make distclean` to also remove archives under `release/`.

`make install` remains an optimized-build alias and does not copy files. `assets/icon/yabai.icns` is the application icon for app bundles (`Contents/Resources/yabai.icns` with `CFBundleIconFile` set to `yabai`); the Nix package installs it at `share/yabai/yabai.icns`, and `make app-icon` regenerates it from the full-bleed `assets/icon/icon-app.svg`. `make archive` writes installer-compatible archives under `release/`, with an `archive/` top-level directory. Run `make sign` explicitly before `make deploy`; deploy never invokes the signer and instead requires the existing optimized artifact to satisfy an explicit trusted `DEPLOY_REQUIREMENT` that pins a cryptographic signing identity (for example, an exact requirement containing the leaf certificate hash). It then installs that verified artifact and reloads the scripting addition. There is no permissive requirement default. If scripting-addition reload or service restart fails after final verification, deploy reports failure but retains the verified binary.

### Nix

The flake supports only `aarch64-darwin` on macOS 26 and 27. It requires the Xcode command line tools because yabai links against macOS system and private frameworks.

- `nix build` builds the optimized Apple Silicon binary and man page.
- `nix build .#head` builds an ad-hoc-signed `Yabai.app` from the pinned source revision.
- `nix run . -- --version` runs the flake package.
- `nix develop` enters a shell with the repository's build, test, and documentation tools.
- `nix flake check` builds the source package, application bundle, and Home Manager evaluation checks.

### Home Manager

The flake exports `homeManagerModules.yabai`. The module defaults to `head`, which means the exact source revision pinned by the consumer's `yabai` flake input; updating that input advances the build. Source builds are ad-hoc signed development bundles. Version tags publish immutable Developer ID-signed `Yabai.app` archives through the protected `release-signing` GitHub environment.

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

          programs.yabai = {
            enable = true;
            install.source = "head";
            settings = {
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

The module installs the selected application package, validates declarative `settings` with `yabai --check-config`, writes the default XDG configuration paths, and manages a direct `com.asmvik.yabai` GUI LaunchAgent. Disable any nix-darwin, Homebrew, or hand-written yabai agent before enabling it so two daemons cannot race for the same runtime resources. The module refuses to overwrite an unmanaged or locally modified `com.asmvik.yabai` plist unless `programs.yabai.service.force = true` is set explicitly.

By default the agent launches the immutable executable inside the selected package. A configuration that enables Home Manager's Darwin application copying can instead launch the stable copied bundle path so macOS privacy grants apply to the application users see in System Settings:

```nix
programs.yabai.service.executable =
  "${config.home.homeDirectory}/Applications/Home Manager Apps/Yabai.app/Contents/MacOS/yabai";
```

The service activation runs after Home Manager's `copyApps` step and refuses to load the agent if the configured executable does not exist or is not executable.

Home Manager can also ensure a minimum global count of ordinary Spaces after the service starts:

```nix
programs.yabai.spaces.ensureCount = 9;
```

Missing Spaces are created on the active display with `yabai -m space --create`; native-fullscreen Spaces do not count toward the requested total, and extra Spaces are never removed. When Spaces are added, the module reloads declarative settings so previously unavailable native rule targets can bind. Creation requires partial SIP disablement and a working scripting addition, and activation fails if the requested count cannot be reached. Leave the option at its default `null` to manage Spaces outside Home Manager.

Available installation sources are:

- `head`: compile the revision pinned by the flake input. This is the current default.
- `commit`: fetch and compile an explicit revision using `install.commit.rev` and its Nix source `hash`.
- `nightly`: install a CI-produced signed app archive pinned by `url`, unpacked `hash`, and `version`.
- `release`: install a signed release archive with the same pinned artifact metadata.

The `head` and `commit` application bundles use the development identifier
`com.markfeinstein.yabai.development`. They are ad-hoc signed, so a content-changing
source update still requires granting the rebuilt development app Accessibility again.
The separate identifier prevents macOS from confusing that per-build CDHash grant with
the stable Developer ID requirement used by signed nightly and release artifacts.

For example, an explicit source revision uses:

```nix
programs.yabai.install = {
  source = "commit";
  commit = {
    rev = "8955858e10ec6bb04ec4bd8c17a443e94c02b200";
    hash = "sha256-...";
    version = "0.0.1-nightly.8955858e10ec"; # optional
  };
};
```

The module pins the latest verified signed stable release in `nix/release.json` and, after the first nightly is published, the latest verified nightly in `nix/nightly.json`. Selecting either channel only requires:

```nix
programs.yabai.install.source = "release";
# or
programs.yabai.install.source = "nightly";
```

After any stable or nightly artifact is signed, notarized, and published, the release workflow dispatches a separate least-privilege workflow that verifies its checksum, Developer ID signature, stapled ticket, Gatekeeper assessment, and version metadata; calculates the normalized unpacked Nix hash; validates the module; and advances the matching metadata file on `main`. It rejects downgrades and immutable-tag metadata changes. The nightly scheduler ignores commits that change only these pin files, preventing pin updates from generating an endless sequence of otherwise identical nightlies. Custom archives can still be selected explicitly with the `programs.yabai.install.release` or `programs.yabai.install.nightly` metadata options.

Artifact hashes are mandatory: the module never follows an unpinned mutable download. Release and nightly packages require the Developer ID identity `com.markfeinstein.yabai` from Apple team `GYXXG6S7KM`; the executable and enclosing bundle are checked against that requirement before and after the archive is copied into the Nix store. The archive is not stripped, re-signed, or otherwise modified.

### Protected release signing

Pushing a `v<major>.<minor>.<patch>` release tag or `nightly-YYYYMMDD` nightly tag starts `.github/workflows/release.yml`. The release tag must match the base version in `VERSION`; nightly binaries use `<base>-nightly.<date>.<commit>` while retaining the base version in bundle metadata. `.github/workflows/nightly.yml` checks the default branch at 11:00 PM Pacific, with recovery attempts at 2:00 AM and 6:00 AM Pacific. When non-pin content has changed since the latest published nightly, it creates the immutable UTC-dated nightly tag and explicitly dispatches the protected release workflow. It skips pin-only or unchanged revisions and already-published dates, suppresses duplicate active release runs, and retries an existing date tag without a release. The explicit dispatch is intentional because tag pushes made with GitHub Actions' repository token do not start another workflow run. The scheduler can also be started manually without changing these safeguards.

An unprivileged job builds and tests the macOS 26+ Apple Silicon binary, stages `Yabai.app`, and uploads a one-day unsigned artifact. The release job cannot read signing or notarization credentials until the `release-signing` environment is approved. It imports the certificate into an ephemeral keychain, signs both the executable and bundle as `com.markfeinstein.yabai` with hardened runtime and a secure timestamp, verifies the Apple team and designated requirement, and submits a temporary ZIP to Apple's automated notary service. An accepted submission is logged, stapled to `Yabai.app`, validated with `stapler` and Gatekeeper, and repackaged as `Yabai-<tag>.zip` with its SHA-256 checksum. Temporary signing and notarization credentials are deleted before the runner exits.

Configure the `release-signing` environment with these non-secret variables:

- `SIGNING_IDENTIFIER=com.markfeinstein.yabai`
- `APPLE_TEAM_ID=GYXXG6S7KM`
- `SIGNING_IDENTITY_SHA1` set to the Developer ID Application certificate fingerprint
- `APPLE_NOTARY_KEY_ID` set to the App Store Connect Team API Key ID
- `APPLE_NOTARY_ISSUER_ID` set to the App Store Connect Team API Issuer ID

Configure these environment secrets without placing any value in the repository:

- `MACOS_CERTIFICATE_P12_BASE64`, containing the exported Developer ID Application identity
- `MACOS_CERTIFICATE_PASSWORD`, containing the P12 export password
- `APPLE_NOTARY_API_KEY_BASE64`, containing the one-time-download App Store Connect Team API private key

Individual App Store Connect API keys cannot authenticate `notarytool`; use a Team Key with the least role that permits notarization. Because notarization requires hardened runtime, exercise the notarized build on a prepared Mac before treating a release as validated, including Accessibility-driven window management and any optional scripting-addition workflow. The workflow submits once, records the submission ID, and waits up to 20 minutes. If waiting times out, Apple continues processing the existing submission and the workflow fails without resubmitting it. Submission, result, and notary log JSON are retained as a workflow artifact for 30 days. The notary service typically completes within 5–15 minutes, but only an `Accepted` result can reach release publication.

The environment should require the certificate owner as reviewer, permit self-review when that owner is the sole maintainer, and permit `v*` and `nightly-*` tags. Scheduled nightlies therefore prepare and dispatch automatically but still wait for the environment's signing approval. The workflow also accepts a protected manual dispatch with an existing tag so a missed or failed run can be retried without moving the tag; permit the canonical `main` branch in the environment when using scheduled or manual dispatch. After publishing, calculate the unpacked Nix hash for the immutable release URL and pin it in `services.yabai.install.release.hash`.

The module manages only the user service. It intentionally does not install the optional scripting addition, which requires root access and a separate SIP-aware system configuration.

## Testing

`make test` runs the ordinary, offline behavior suite. `make test-asan` and `make test-tsan` compile and execute the core and runtime-path suites with ASan+UBSan and TSan+UBSan respectively. `make test-arm64` and `make test-architectures` build the thin Apple Silicon core binaries, verify their slices, and execute them natively. `make syntax` checks independently compiled pure modules and public headers. `make analyze` runs the whole-program Clang static analyzer and rejects diagnostics not present in the reviewed baseline. `make man-contract` is generator-independent and checks deterministic source/generated numeric-domain contracts. `make man-check` requires Asciidoctor and performs both the contract check and regeneration-parity comparison. `make test-all` combines the generator-independent offline gates. Build and test outputs are configuration-specific, so one target does not delete another target's artifacts.

For focused core checks, build with `make -C tests core-build`, then run `tests/bin/ordinary/tests --list` or `tests/bin/ordinary/tests --filter <substring>`. Listing and filtering compose; no match fails rather than silently passing. See [development testing](doc/testing.md) for exact argument and exit-status behavior.

`make fuzz-build` builds deterministic ASan+UBSan fuzz-smoke binaries for regex patterns, daemon frames, Mach-O parsing, and native JSONC configuration parsing under `bin/fuzz/`. `make fuzz-smoke` runs a bounded 1,000-input pass for each harness. The harness sources expose the standard `LLVMFuzzerTestOneInput` entry point and can also be linked with libFuzzer when its runtime is installed.

`make private-api-smoke` is an opt-in, read-only **live** WindowServer query check. It needs a logged-in native Mac with an existing visible app window, but no running daemon or scripting addition. It records redacted OS/build/architecture evidence and fails on missing prerequisites or a 15-second probe timeout. The manual prepared-host workflow supports macOS 26 and 27 on native Apple Silicon; it does not provision hosts or treat cross-builds as live evidence. See [development testing](doc/testing.md) for scope, safety, and runner setup.

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
|Operating&nbsp;System&nbsp;Apple Silicon|Tahoe 26.0+ and Golden Gate 27.0+ are supported. Intel Macs and macOS 15 or older are not supported.|
|Accessibility&nbsp;API|yabai must be given permission to utilize the Accessibility API and will request access upon launch. The application must be restarted after access has been granted.|
|Screen Recording|yabai must be given Screen Recording permission if and only if you want to enable window animations, and will request access when necessary. The application must be restarted after access has been granted.|
|System&nbsp;Settings&nbsp;(macOS 26.x, 27.x)|In the Desktop & Dock tab, inside the Mission Control pane, the setting "Displays have separate Spaces" must be enabled.|

Please also take note of the following caveats.

|Caveat|Note|
|-:|:-|
|System&nbsp;Integrity&nbsp;Protection (Optional)|System Integrity Protection can be (partially) disabled for yabai to inject a scripting addition into Dock.app for controlling windows with functions that require elevated privileges. This enables control of the window server, which is the sole owner of all window connections, and enables additional features of yabai.|
|Code&nbsp;Signing|When building from source (or installing from HEAD), it is necessary to codesign the binary so it retains its accessibility and automation privileges when updated or rebuilt.|
|Secure&nbsp;Input|macOS suspends global keyboard taps while Secure Input is active (for example, in some password fields). Native shortcuts resume when Secure Input is released; `yabai --doctor` reports the current state.|
|Finder&nbsp;Desktop|Some people disable the Finder Desktop window using an undocumented defaults write command. This breaks focusing of empty spaces and should be avoided when using yabai. To re-activate the Finder Desktop, run: "defaults write com.apple.finder CreateDesktop -bool true".|
|NSDocument-based&nbsp;Applications|Windows that utilize native macOS tabs such as Terminal and Finder, [do not behave correctly when creating tabs](https://github.com/asmvik/yabai/issues/68). Avoid creating tabs in these applications, consider alternatives that do not use NSDocument's tab system, or make these windows float using rules.|
|System&nbsp;Settings&nbsp;(macOS 26.x, 27.x)|In the Desktop & Dock tab, inside the Mission Control pane, the setting "Automatically rearrange Spaces based on most recent use" should be disabled for commands that rely on the ordering of spaces to work reliably.|
|System&nbsp;Settings&nbsp;(macOS 26.x, 27.x)|In the Desktop & Dock tab, inside the Desktop & Stage Manager pane, the setting "Show Items On Desktop" should be enabled for display and space focus commands to work reliably in multi-display configurations.|
|System&nbsp;Settings&nbsp;(macOS 26.x, 27.x)|In the Desktop & Dock tab, inside the Desktop & Stage Manager pane, the setting "Click wallpaper to reveal Desktop" should be set to "Only in Stage Manager" for display and space focus commands to work reliably.|

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
