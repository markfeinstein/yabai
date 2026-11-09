{
  formats,
  lib,
  stdenvNoCC,
  yabai,
  version ? yabai.version,
  bundleIdentifier ? yabai.bundleIdentifier,
}:
let
  bundleVersion = builtins.head (lib.splitString "-" version);
  infoPlist = (formats.plist { }).generate "Info.plist" {
    CFBundleDevelopmentRegion = "en";
    CFBundleExecutable = "yabai";
    CFBundleIconFile = "yabai";
    CFBundleIdentifier = bundleIdentifier;
    CFBundleInfoDictionaryVersion = "6.0";
    CFBundleName = "Yabai";
    CFBundlePackageType = "APPL";
    CFBundleShortVersionString = bundleVersion;
    CFBundleVersion = bundleVersion;
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

    # Keep ad-hoc source builds separate from the Developer ID release identity.
    # TCC records include the signing requirement, so sharing the production
    # bundle identifier makes macOS prefer and enforce the production certificate.
    /usr/bin/codesign --force --sign - --timestamp=none \
      --identifier ${lib.escapeShellArg bundleIdentifier} "$app/Contents/MacOS/yabai"
    /usr/bin/codesign --force --sign - --timestamp=none "$app"

    ln -s ../Applications/Yabai.app/Contents/MacOS/yabai "$out/bin/yabai"

    runHook postInstall
  '';

  passthru = {
    inherit bundleIdentifier yabai;
    application = "Applications/Yabai.app";
    executable = "Applications/Yabai.app/Contents/MacOS/yabai";
    installSource = "source";
  };

  meta = yabai.meta // {
    description = "Ad-hoc-signed Yabai.app built from source";
  };
}
