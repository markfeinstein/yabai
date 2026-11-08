{
  lib,
  stdenvNoCC,
  gnumake,
  xxd,
  src,
  version,
}:
stdenvNoCC.mkDerivation {
  pname = "yabai";
  inherit src version;

  strictDeps = true;
  nativeBuildInputs = [
    gnumake
    xxd
  ];

  buildPhase = ''
    runHook preBuild
    make release \
      BUILD_PATH="$TMPDIR/build" \
      XCRUN=/usr/bin/xcrun \
      XXD=${xxd}/bin/xxd
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 "$TMPDIR/build/release/yabai" "$out/bin/yabai"
    install -Dm644 doc/yabai.1 "$out/share/man/man1/yabai.1"
    install -Dm644 assets/icon/yabai.icns "$out/share/yabai/yabai.icns"
    runHook postInstall
  '';

  # Stripping would invalidate the Mach-O signatures produced by ld.
  dontStrip = true;

  meta = {
    description = "Tiling window manager for macOS based on binary space partitioning";
    homepage = "https://github.com/asmvik/yabai";
    license = lib.licenses.mit;
    mainProgram = "yabai";
    platforms = lib.platforms.darwin;
  };
}
