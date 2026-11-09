{
  home-manager,
  lib,
  pkgs,
  yabaiModule,
}:
let
  nightlyPin = builtins.fromJSON (builtins.readFile ../nightly.json);
  releasePin = builtins.fromJSON (builtins.readFile ../release.json);
  mkHome =
    extraModule:
    home-manager.lib.homeManagerConfiguration {
      inherit pkgs;
      modules = [
        yabaiModule
        {
          home = {
            username = "yabai-test";
            homeDirectory = "/Users/yabai-test";
            stateVersion = "26.05";
            enableNixpkgsReleaseCheck = false;
          };
          programs.yabai = {
            enable = true;
            service.enable = lib.mkDefault false;
            settings = {
              version = 3;
              config.layout = "bsp";
            };
          };
        }
        extraModule
      ];
    };

  head = mkHome { };

  commit = mkHome {
    programs.yabai.install = {
      source = "commit";
      commit = {
        rev = "8955858e10ec6bb04ec4bd8c17a443e94c02b200";
        hash = lib.fakeHash;
        version = "0.0.1-nightly.test";
      };
    };
  };

  nightly = mkHome {
    programs.yabai.install = {
      source = "nightly";
      nightly = {
        url = "https://example.invalid/yabai-nightly.zip";
        hash = lib.fakeHash;
        version = "0.0.1-nightly.20261108.test";
      };
    };
  };

  release = mkHome {
    programs.yabai.install = {
      source = "release";
      release = {
        url = "https://example.invalid/Yabai-v0.0.1.zip";
        hash = lib.fakeHash;
        version = "0.0.1";
        appPath = "dist/Yabai.app";
      };
    };
  };

  pinnedNightly =
    if nightlyPin.tag == null then
      null
    else
      mkHome {
        programs.yabai.install.source = "nightly";
      };

  pinnedRelease = mkHome {
    programs.yabai.install.source = "release";
  };

  headPackage = head.config.programs.yabai.finalPackage;
  commitPackage = commit.config.programs.yabai.finalPackage;
  nightlyPackage = nightly.config.programs.yabai.finalPackage;
  releasePackage = release.config.programs.yabai.finalPackage;
  pinnedNightlyPackage =
    if pinnedNightly == null then null else pinnedNightly.config.programs.yabai.finalPackage;
  pinnedReleasePackage = pinnedRelease.config.programs.yabai.finalPackage;

  stableExecutable = "/Users/yabai-test/Applications/Home Manager Apps/Yabai.app/Contents/MacOS/yabai";
  stableService = mkHome {
    programs.yabai.service = {
      enable = true;
      executable = stableExecutable;
    };
  };

  locallySignedHead = mkHome {
    programs.yabai = {
      install.localSigning = {
        bundleIdentifier = "com.example.yabai.local-development";
        identity = "Developer ID Application: Example (TEAMID1234)";
        teamIdentifier = "TEAMID1234";
      };
      service = {
        enable = true;
        executable = stableExecutable;
      };
    };
  };
  locallySignedActivation = locallySignedHead.config.home.activation.yabaiService.data;

  ensuredSpaces = mkHome {
    programs.yabai = {
      service.enable = true;
      spaces.ensureCount = 9;
    };
  };
  ensureSpacesActivation = ensuredSpaces.config.home.activation.yabaiEnsureSpaces.data;
  ensureSpacesScript = pkgs.writeText "yabai-ensure-spaces" ensureSpacesActivation;

  invalidRelativeExecutable = mkHome {
    programs.yabai.service.executable = "Applications/Yabai.app/Contents/MacOS/yabai";
  };

  invalidCommit = mkHome {
    programs.yabai.install.source = "commit";
  };

  invalidNightly = mkHome {
    programs.yabai.install = {
      source = "nightly";
      nightly = {
        url = null;
        hash = null;
        version = null;
      };
    };
  };

  invalidSettingsFile = mkHome {
    programs.yabai.settingsFile = pkgs.writeText "yabai-settings.jsonc" "{}";
  };

  invalidEnsureSpaces = mkHome {
    programs.yabai.spaces.ensureCount = 9;
  };

  invalidLocalSigningPair = mkHome {
    programs.yabai.install.localSigning.identity = "Developer ID Application: Example (TEAMID1234)";
  };

  invalidReleaseLocalSigning = mkHome {
    programs.yabai.install = {
      source = "release";
      localSigning = {
        bundleIdentifier = "com.example.yabai.local-development";
        identity = "Developer ID Application: Example (TEAMID1234)";
        teamIdentifier = "TEAMID1234";
      };
    };
  };

  invalidStoreLocalSigning = mkHome {
    programs.yabai = {
      install.localSigning = {
        bundleIdentifier = "com.example.yabai.local-development";
        identity = "Developer ID Application: Example (TEAMID1234)";
        teamIdentifier = "TEAMID1234";
      };
      service.enable = true;
    };
  };

  invalidSharedLocalSigningIdentifier = mkHome {
    programs.yabai = {
      install.localSigning = {
        bundleIdentifier = "com.markfeinstein.yabai.development";
        identity = "Developer ID Application: Example (TEAMID1234)";
        teamIdentifier = "TEAMID1234";
      };
      service = {
        enable = true;
        executable = stableExecutable;
      };
    };
  };
in
assert head.config.programs.yabai.install.source == "head";
assert head.config.programs.yabai.settings.version == 3;
assert headPackage.installSource == "source";
assert headPackage.bundleIdentifier == "com.markfeinstein.yabai.development";
assert commitPackage.installSource == "source";
assert commitPackage.bundleIdentifier == "com.markfeinstein.yabai.development";
assert commitPackage.version == "0.0.1-nightly.test";
assert nightlyPackage.installSource == "nightly";
assert nightlyPackage.version == "0.0.1-nightly.20261108.test";
assert head.config.programs.yabai.install.nightly.hash == nightlyPin.hash;
assert head.config.programs.yabai.install.nightly.version == nightlyPin.version;
assert
  head.config.programs.yabai.install.nightly.url == (
    if nightlyPin.tag == null then
      null
    else
      "https://github.com/markfeinstein/yabai/releases/download/${nightlyPin.tag}/Yabai-${nightlyPin.tag}.zip"
  );
assert nightlyPin.tag == null || pinnedNightlyPackage.installSource == "nightly";
assert nightlyPin.tag == null || pinnedNightlyPackage.version == nightlyPin.version;
assert releasePackage.installSource == "release";
assert releasePackage.version == "0.0.1";
assert releasePackage.signingIdentifier == "com.markfeinstein.yabai";
assert releasePackage.teamIdentifier == "GYXXG6S7KM";
assert pinnedReleasePackage.installSource == "release";
assert pinnedReleasePackage.version == releasePin.version;
assert
  pinnedRelease.config.programs.yabai.install.release.url
  == "https://github.com/markfeinstein/yabai/releases/download/v${releasePin.version}/Yabai-v${releasePin.version}.zip";
assert pinnedRelease.config.programs.yabai.install.release.hash == releasePin.hash;
assert stableService.config.programs.yabai.service.finalExecutable == stableExecutable;
assert lib.hasInfix stableExecutable stableService.config.home.activation.yabaiService.data;
assert locallySignedHead.config.programs.yabai.install.localSigning.teamIdentifier == "TEAMID1234";
assert
  locallySignedHead.config.programs.yabai.install.localSigning.bundleIdentifier
  == "com.example.yabai.local-development";
assert lib.hasInfix "Developer ID Application: Example (TEAMID1234)" locallySignedActivation;
assert lib.hasInfix "com.example.yabai.local-development" locallySignedActivation;
assert lib.hasInfix "/usr/libexec/PlistBuddy" locallySignedActivation;
assert lib.hasInfix "certificate leaf[subject.OU]" locallySignedActivation;
assert lib.hasInfix "/usr/bin/codesign --force --sign" locallySignedActivation;
assert lib.hasInfix "--timestamp=none" locallySignedActivation;
assert ensuredSpaces.config.programs.yabai.spaces.ensureCount == 9;
assert lib.hasInfix "query --spaces" ensureSpacesActivation;
assert lib.hasInfix "space --create" ensureSpacesActivation;
assert lib.hasInfix "config reload" ensureSpacesActivation;
assert lib.hasInfix "is-native-fullscreen" ensureSpacesActivation;
assert toString head.config.xdg.configFile."yabai/yabai.jsonc".source != "";
assert !(builtins.tryEval invalidRelativeExecutable.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidCommit.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidNightly.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidSettingsFile.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidEnsureSpaces.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidLocalSigningPair.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidReleaseLocalSigning.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidStoreLocalSigning.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidSharedLocalSigningIdentifier.activationPackage.drvPath).success;
pkgs.runCommand "yabai-home-manager-module-eval" { } ''
  fixture=$TMPDIR/yabai-spaces
  mkdir -p "$fixture"
  state="$fixture/count"
  reload="$fixture/reloaded"
  mock="$fixture/yabai"

  cat > "$mock" <<'SH'
  #!${pkgs.bash}/bin/bash
  set -euo pipefail
  state="''${YABAI_SPACE_TEST_STATE:?}"
  reload="''${YABAI_SPACE_TEST_RELOAD:?}"
  case "$*" in
    '-m query --spaces')
      count=$(cat "$state")
      printf '['
      i=1
      while [ "$i" -le "$count" ]; do
        [ "$i" -eq 1 ] || printf ','
        printf '{"is-native-fullscreen":false}'
        i=$((i + 1))
      done
      printf ']\n'
      ;;
    '-m space --create')
      count=$(cat "$state")
      printf '%s\n' "$((count + 1))" > "$state"
      ;;
    '-m config reload')
      touch "$reload"
      ;;
    *) exit 64 ;;
  esac
  SH
  chmod +x "$mock"

  activation="$fixture/activation"
  {
    printf '#!${pkgs.bash}/bin/bash\nset -euo pipefail\nerrorEcho() { printf "%%s\\n" "$*" >&2; }\n'
    sed "1s|^service_executable=.*$|service_executable='$mock'|" ${ensureSpacesScript}
  } > "$activation"
  chmod +x "$activation"
  ${pkgs.bash}/bin/bash -n "$activation"

  printf '6\n' > "$state"
  YABAI_SPACE_TEST_STATE="$state" YABAI_SPACE_TEST_RELOAD="$reload" "$activation"
  test "$(cat "$state")" = 9
  test -f "$reload"

  rm -f "$reload"
  printf '10\n' > "$state"
  YABAI_SPACE_TEST_STATE="$state" YABAI_SPACE_TEST_RELOAD="$reload" "$activation"
  test "$(cat "$state")" = 10
  test ! -e "$reload"

  touch "$out"
''
