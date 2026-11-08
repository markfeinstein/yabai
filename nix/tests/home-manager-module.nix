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
            service.enable = false;
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
assert toString head.config.xdg.configFile."yabai/yabai.jsonc".source != "";
assert !(builtins.tryEval invalidCommit.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidNightly.activationPackage.drvPath).success;
assert !(builtins.tryEval invalidRelease.activationPackage.drvPath).success;
pkgs.runCommand "yabai-home-manager-module-eval" { } ''
  touch "$out"
''
