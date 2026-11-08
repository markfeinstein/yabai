{
  fetchzip,
  lib,
  stdenvNoCC,
  url,
  hash,
  version,
  appPath ? "Yabai.app",
  installSource,
}:
let
  unpacked = fetchzip {
    inherit url hash;
    stripRoot = false;
  };
in
stdenvNoCC.mkDerivation {
  pname = "yabai-app";
  inherit version;

  dontUnpack = true;
  dontFixup = true;

  installPhase = ''
    runHook preInstall

    source_app=${lib.escapeShellArg "${unpacked}/${appPath}"}
    if [ ! -d "$source_app" ] || [ ! -x "$source_app/Contents/MacOS/yabai" ]; then
      echo "the ${installSource} archive does not contain ${appPath}/Contents/MacOS/yabai" >&2
      exit 1
    fi

    /usr/bin/codesign --verify --deep --strict "$source_app"

    mkdir -p "$out/Applications" "$out/bin"
    cp -R "$source_app" "$out/Applications/Yabai.app"
    /usr/bin/codesign --verify --deep --strict "$out/Applications/Yabai.app"
    ln -s ../Applications/Yabai.app/Contents/MacOS/yabai "$out/bin/yabai"

    runHook postInstall
  '';

  passthru = {
    application = "Applications/Yabai.app";
    executable = "Applications/Yabai.app/Contents/MacOS/yabai";
    inherit installSource;
  };

  meta = {
    description = "Developer ID-signed Yabai.app from a ${installSource} build";
    homepage = "https://github.com/markfeinstein/yabai";
    license = lib.licenses.mit;
    mainProgram = "yabai";
    platforms = lib.platforms.darwin;
    sourceProvenance = [ lib.sourceTypes.binaryNativeCode ];
  };
}
