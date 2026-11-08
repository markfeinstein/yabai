{
  fetchzip,
  lib,
  stdenvNoCC,
  url,
  hash,
  version,
  appPath ? "Yabai.app",
  installSource,
  signingIdentifier ? "com.markfeinstein.yabai",
  teamIdentifier ? "GYXXG6S7KM",
}:
let
  unpacked = fetchzip {
    inherit url hash;
    stripRoot = false;
  };
  signingRequirement =
    ''identifier "${signingIdentifier}" and anchor apple generic and certificate leaf[subject.OU] = "${teamIdentifier}"'';
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

    source_executable="$source_app/Contents/MacOS/yabai"
    signing_requirement=${lib.escapeShellArg signingRequirement}
    /usr/bin/codesign --verify --strict -R "=$signing_requirement" "$source_executable"
    /usr/bin/codesign --verify --deep --strict -R "=$signing_requirement" "$source_app"

    mkdir -p "$out/Applications" "$out/bin"
    cp -R "$source_app" "$out/Applications/Yabai.app"
    installed_app="$out/Applications/Yabai.app"
    /usr/bin/codesign --verify --strict -R "=$signing_requirement" "$installed_app/Contents/MacOS/yabai"
    /usr/bin/codesign --verify --deep --strict -R "=$signing_requirement" "$installed_app"
    ln -s ../Applications/Yabai.app/Contents/MacOS/yabai "$out/bin/yabai"

    runHook postInstall
  '';

  passthru = {
    application = "Applications/Yabai.app";
    executable = "Applications/Yabai.app/Contents/MacOS/yabai";
    inherit installSource signingIdentifier teamIdentifier;
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
