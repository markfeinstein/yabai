{
  description = "yabai — a tiling window manager for macOS";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-26.05-darwin";
    home-manager = {
      url = "github:nix-community/home-manager/release-26.05";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      home-manager,
    }:
    let
      systems = [ "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
      baseVersion = nixpkgs.lib.removeSuffix "\n" (builtins.readFile ./VERSION);
      version = "${baseVersion}-nightly.${self.shortRev or "dirty"}";
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          yabai = pkgs.callPackage ./nix/packages/source.nix {
            src = self;
            inherit version;
          };
          head = pkgs.callPackage ./nix/packages/app-bundle.nix { inherit yabai; };
        in
        {
          default = yabai;
          inherit yabai head;
          app-bundle = head;
        }
      );

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/yabai";
          meta.description = "Run yabai";
        };
      });

      homeManagerModules = {
        default =
          { pkgs, ... }:
          {
            imports = [ ./nix/modules/home-manager/yabai.nix ];
            _module.args = {
              inherit baseVersion;
              yabaiHeadPackage =
                if pkgs.stdenv.hostPlatform.system == "aarch64-darwin" then
                  self.packages.aarch64-darwin.head
                else
                  throw "yabai supports only aarch64-darwin on macOS 26 and 27";
            };
          };
        yabai = self.homeManagerModules.default;
      };

      checks = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          home = home-manager.lib.homeManagerConfiguration {
            inherit pkgs;
            modules = [
              self.homeManagerModules.default
              {
                home = {
                  username = "yabai-test";
                  homeDirectory = "/Users/yabai-test";
                  stateVersion = "26.05";
                  enableNixpkgsReleaseCheck = false;
                };
                programs.yabai = {
                  enable = true;
                  settings = {
                    version = 3;
                    config.layout = "bsp";
                  };
                };
              }
            ];
          };
        in
        {
          default = self.packages.${system}.default;
          app-bundle = self.packages.${system}.head;
          head-identity = pkgs.runCommand "yabai-head-development-identity" { } ''
            app=${self.packages.${system}.head}/Applications/Yabai.app
            executable="$app/Contents/MacOS/yabai"
            test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Contents/Info.plist")" = \
              com.markfeinstein.yabai.development
            test "$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$app/Contents/Info.plist")" = 26.0
            /usr/bin/lipo "$executable" -verify_arch arm64
            test "$(/usr/bin/lipo -archs "$executable")" = arm64
            test "$(/usr/bin/otool -arch arm64 -l "$executable" | /usr/bin/awk '$1 == "minos" { print $2 }')" = 26.0
            /usr/bin/codesign --verify --deep --strict --verbose=4 "$app"
            /usr/bin/codesign -dv --verbose=2 "$app" 2>&1 \
              | /usr/bin/grep -Fx 'Identifier=com.markfeinstein.yabai.development'
            /usr/bin/codesign -dv --verbose=2 "$executable" 2>&1 \
              | /usr/bin/grep -Fx 'Identifier=com.markfeinstein.yabai.development'
            touch "$out"
          '';
          home-manager = home.activationPackage;
          home-manager-module = import ./nix/tests/home-manager-module.nix {
            inherit home-manager pkgs;
            inherit (nixpkgs) lib;
            yabaiModule = self.homeManagerModules.default;
          };
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShellNoCC {
            packages = [
              pkgs.asciidoctor
              pkgs.gnumake
              pkgs.python3
              pkgs.xxd
            ];

            shellHook = ''
              export XCRUN=/usr/bin/xcrun
            '';
          };
        }
      );

      formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.nixfmt);
    };
}
