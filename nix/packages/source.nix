{
  lib,
  stdenvNoCC,
  gnumake,
  xxd,
  src,
  version,
  bundleIdentifier ? "com.markfeinstein.yabai.development",
}:
stdenvNoCC.mkDerivation {
  pname = "yabai";
  inherit src version;

  strictDeps = true;
  nativeBuildInputs = [
    gnumake
    xxd
  ];

  postPatch = ''
    substituteInPlace assets/Info.plist \
      --replace-fail com.markfeinstein.yabai ${lib.escapeShellArg bundleIdentifier}
  '';

  buildPhase = ''
    runHook preBuild
    make release \
      BUILD_PATH="$TMPDIR/build" \
      VERSION=${lib.escapeShellArg version} \
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

  passthru = {
    inherit bundleIdentifier;
  };

  meta = {
    description = "Tiling window manager for macOS based on binary space partitioning";
    homepage = "https://github.com/markfeinstein/yabai";
    license = lib.licenses.mit;
    mainProgram = "yabai";
    platforms = lib.platforms.darwin;
  };
}
