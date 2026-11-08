{
  home-manager,
  lib,
  pkgs,
  yabaiModule,
}:
let
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
          services.yabai = {
            enable = true;
            service.enable = lib.mkDefault false;
            config.nativeConfig = {
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
    services.yabai.install = {
      source = "commit";
      commit = {
        rev = "8955858e10ec6bb04ec4bd8c17a443e94c02b200";
        hash = lib.fakeHash;
        version = "0.0.1-nightly.test";
      };
    };
  };

  nightly = mkHome {
    services.yabai.install = {
      source = "nightly";
      nightly = {
        url = "https://example.invalid/yabai-nightly.zip";
        hash = lib.fakeHash;
        version = "0.0.1-nightly.20261108.test";
      };
    };
  };

  release = mkHome {
    services.yabai.install = {
      source = "release";
      release = {
        url = "https://example.invalid/Yabai-v0.0.1.zip";
        hash = lib.fakeHash;
        version = "0.0.1";
        appPath = "dist/Yabai.app";
      };
    };
  };

  pinnedRelease = mkHome {
    services.yabai.install.source = "release";
  };

  headPackage = head.config.services.yabai.finalPackage;
  commitPackage = commit.config.services.yabai.finalPackage;
  nightlyPackage = nightly.config.services.yabai.finalPackage;
  releasePackage = release.config.services.yabai.finalPackage;
  pinnedReleasePackage = pinnedRelease.config.services.yabai.finalPackage;

  stableExecutable = "/Users/yabai-test/Applications/Home Manager Apps/Yabai.app/Contents/MacOS/yabai";
  stableService = mkHome {
    services.yabai.service = {
      enable = true;
      executable = stableExecutable;
    };
  };

  invalidRelativeExecutable = mkHome {
    services.yabai.service.executable = "Applications/Yabai.app/Contents/MacOS/yabai";
  };

  invalidCommit = mkHome {
    services.yabai.install.source = "commit";
  };

  invalidNightly = mkHome {
    services.yabai.install.source = "nightly";
  };
in
assert head.config.services.yabai.install.source == "head";
assert headPackage.installSource == "source";
assert commitPackage.installSource == "source";
assert commitPackage.version == "0.0.1-nightly.test";
assert nightlyPackage.installSource == "nightly";
assert nightlyPackage.version == "0.0.1-nightly.20261108.test";
assert releasePackage.installSource == "release";
assert releasePackage.version == "0.0.1";
assert releasePackage.signingIdentifier == "com.markfeinstein.yabai";
assert releasePackage.teamIdentifier == "GYXXG6S7KM";
assert pinnedReleasePackage.installSource == "release";
assert pinnedReleasePackage.version == "0.0.1";
assert
  pinnedRelease.config.services.yabai.install.release.url
  == "https://github.com/markfeinstein/yabai/releases/download/v0.0.1/Yabai-v0.0.1.zip";
assert
  pinnedRelease.config.services.yabai.install.release.hash
  == "sha256-ARO20dOA4NefMRsYQaipzGpxEybZhs1uySK/eP5U2l4=";
assert stableService.config.services.yabai.service.finalExecutable == stableExecutable;
assert lib.hasInfix stableExecutable stableService.config.home.activation.yabaiService.data;
assert toString head.config.xdg.configFile."yabai/yabai.jsonc".source != "";
assert !(builtins.tryEval invalidRelativeExecutable.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidCommit.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidNightly.activationPackage.drvPath).success;
pkgs.runCommand "yabai-home-manager-module-eval" { } ''
  touch "$out"
''
