{
  description = "yabai — a tiling window manager for macOS";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-26.05-darwin";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "aarch64-darwin"
        "x86_64-darwin"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.stdenvNoCC.mkDerivation {
            pname = "yabai";
            version = "7.1.25-unstable-${self.shortRev or "dirty"}";
            src = self;

            strictDeps = true;
            nativeBuildInputs = [
              pkgs.gnumake
              pkgs.xxd
            ];

            buildPhase = ''
              runHook preBuild
              make release \
                BUILD_PATH="$TMPDIR/build" \
                XCRUN=/usr/bin/xcrun \
                XXD=${pkgs.xxd}/bin/xxd
              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall
              install -Dm755 "$TMPDIR/build/release/yabai" "$out/bin/yabai"
              install -Dm644 doc/yabai.1 "$out/share/man/man1/yabai.1"
              runHook postInstall
            '';

            # Stripping would invalidate the Mach-O signatures produced by ld.
            dontStrip = true;

            meta = {
              description = "Tiling window manager for macOS based on binary space partitioning";
              homepage = "https://github.com/asmvik/yabai";
              license = pkgs.lib.licenses.mit;
              mainProgram = "yabai";
              platforms = systems;
            };
          };
        }
      );

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/yabai";
          meta.description = "Run yabai";
        };
      });

      checks = forAllSystems (system: {
        default = self.packages.${system}.default;
      });

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
