{
  formats,
  lib,
  stdenvNoCC,
  yabai,
  version ? yabai.version,
}:
let
  infoPlist = (formats.plist { }).generate "Info.plist" {
    CFBundleDevelopmentRegion = "en";
    CFBundleExecutable = "yabai";
    CFBundleIconFile = "yabai";
    CFBundleIdentifier = "com.asmvik.yabai";
    CFBundleInfoDictionaryVersion = "6.0";
    CFBundleName = "Yabai";
    CFBundlePackageType = "APPL";
    CFBundleShortVersionString = version;
    CFBundleVersion = version;
    LSUIElement = true;
    NSHumanReadableCopyright = "Copyright © 2019 Åsmund Vikane. All rights reserved.";
  };
in
stdenvNoCC.mkDerivation {
  pname = "yabai-app";
  inherit version;

  dontUnpack = true;
  dontFixup = true;

  installPhase = ''
    runHook preInstall

    app="$out/Applications/Yabai.app"
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" "$out/bin"
    install -m755 ${yabai}/bin/yabai "$app/Contents/MacOS/yabai"
    install -m644 ${yabai}/share/yabai/yabai.icns "$app/Contents/Resources/yabai.icns"
    install -m644 ${infoPlist} "$app/Contents/Info.plist"

    # Source builds are an explicitly development-oriented fallback until CI
    # publishes immutable Developer ID-signed application bundles. Give the
    # bundle a coherent ad-hoc identity without pretending it is a release.
    /usr/bin/codesign --force --sign - --timestamp=none \
      --identifier com.asmvik.yabai "$app/Contents/MacOS/yabai"
    /usr/bin/codesign --force --sign - --timestamp=none "$app"

    ln -s ../Applications/Yabai.app/Contents/MacOS/yabai "$out/bin/yabai"

    runHook postInstall
  '';

  passthru = {
    inherit yabai;
    application = "Applications/Yabai.app";
    executable = "Applications/Yabai.app/Contents/MacOS/yabai";
    installSource = "source";
  };

  meta = yabai.meta // {
    description = "Ad-hoc-signed Yabai.app built from source";
  };
}
