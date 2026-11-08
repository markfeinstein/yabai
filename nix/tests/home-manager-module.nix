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
        version = "7.1.25-unstable-test";
      };
    };
  };

  nightly = mkHome {
    services.yabai.install = {
      source = "nightly";
      nightly = {
        url = "https://example.invalid/yabai-nightly.zip";
        hash = lib.fakeHash;
        version = "7.2.0-nightly";
      };
    };
  };

  release = mkHome {
    services.yabai.install = {
      source = "release";
      release = {
        url = "https://example.invalid/yabai-7.2.0.zip";
        hash = lib.fakeHash;
        version = "7.2.0";
        appPath = "dist/Yabai.app";
      };
    };
  };

  headPackage = head.config.services.yabai.finalPackage;
  commitPackage = commit.config.services.yabai.finalPackage;
  nightlyPackage = nightly.config.services.yabai.finalPackage;
  releasePackage = release.config.services.yabai.finalPackage;

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

  invalidRelease = mkHome {
    services.yabai.install.source = "release";
  };
in
assert head.config.services.yabai.install.source == "head";
assert headPackage.installSource == "source";
assert commitPackage.installSource == "source";
assert commitPackage.version == "7.1.25-unstable-test";
assert nightlyPackage.installSource == "nightly";
assert nightlyPackage.version == "7.2.0-nightly";
assert releasePackage.installSource == "release";
assert releasePackage.version == "7.2.0";
assert releasePackage.signingIdentifier == "com.markfeinstein.yabai";
assert releasePackage.teamIdentifier == "GYXXG6S7KM";
assert stableService.config.services.yabai.service.finalExecutable == stableExecutable;
assert lib.hasInfix stableExecutable stableService.config.home.activation.yabaiService.data;
assert toString head.config.xdg.configFile."yabai/yabai.jsonc".source != "";
assert !(builtins.tryEval invalidRelativeExecutable.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidCommit.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidNightly.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidRelease.activationPackage.drvPath).success;
pkgs.runCommand "yabai-home-manager-module-eval" { } ''
  touch "$out"
''
