{
  config,
  lib,
  pkgs,
  baseVersion,
  yabaiHeadPackage,
  ...
}:
let
  cfg = config.services.yabai;

  artifactOptions = {
    options = {
      url = lib.mkOption {
        type = with lib.types; nullOr str;
        default = null;
        example = "https://github.com/markfeinstein/yabai/releases/download/nightly/Yabai.zip";
        description = "URL of an archive whose root contains the signed application bundle.";
      };

      hash = lib.mkOption {
        type = with lib.types; nullOr str;
        default = null;
        example = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
        description = "Nix hash of the unpacked application archive.";
      };

      version = lib.mkOption {
        type = with lib.types; nullOr str;
        default = null;
        example = "0.0.1";
        description = "Version attached to the packaged application.";
      };

      appPath = lib.mkOption {
        type = lib.types.str;
        default = "Yabai.app";
        description = "Path to the application bundle within the unpacked archive.";
      };
    };
  };

  commitRevision = if cfg.install.commit.rev == null then "missing" else cfg.install.commit.rev;
  commitVersion =
    if cfg.install.commit.version != null then
      cfg.install.commit.version
    else
      "${baseVersion}-nightly.${builtins.substring 0 12 commitRevision}";

  commitPackage =
    if cfg.install.commit.rev != null && cfg.install.commit.hash != null then
      let
        source = pkgs.fetchFromGitHub {
          owner = "markfeinstein";
          repo = "yabai";
          rev = cfg.install.commit.rev;
          hash = cfg.install.commit.hash;
        };
        yabai = pkgs.callPackage ../../packages/source.nix {
          src = source;
          version = commitVersion;
        };
      in
      pkgs.callPackage ../../packages/app-bundle.nix { inherit yabai; }
    else
      yabaiHeadPackage;

  artifactPackage =
    source: artifact:
    if artifact.url != null && artifact.hash != null && artifact.version != null then
      pkgs.callPackage ../../packages/prebuilt-app.nix {
        inherit (artifact)
          url
          hash
          version
          appPath
          ;
        installSource = source;
      }
    else
      yabaiHeadPackage;

  selectedPackage =
    if cfg.install.source == "head" then
      yabaiHeadPackage
    else if cfg.install.source == "commit" then
      commitPackage
    else if cfg.install.source == "nightly" then
      artifactPackage "nightly" cfg.install.nightly
    else
      artifactPackage "release" cfg.install.release;

  packageExecutable = "${selectedPackage}/${selectedPackage.executable}";
  serviceExecutable =
    if cfg.service.executable != null then cfg.service.executable else packageExecutable;

  authoredNativeConfig =
    if cfg.config.nativeConfig != null then
      pkgs.writeText "yabai-authored.jsonc" (builtins.toJSON cfg.config.nativeConfig)
    else
      cfg.config.nativeConfigFile;

  validatedNativeConfig =
    if authoredNativeConfig == null then
      null
    else
      pkgs.runCommand "yabai.jsonc" { } ''
        cp ${lib.escapeShellArg (toString authoredNativeConfig)} "$out"
        ${lib.escapeShellArg packageExecutable} --check-config "$out"
      '';

  legacyConfig = pkgs.writeText "yabairc" (cfg.config.extraConfig + "\n");

  launchPath = lib.concatStringsSep ":" (
    [ "${config.home.profileDirectory}/bin" ]
    ++ config.home.sessionPath
    ++ [
      "/etc/profiles/per-user/${config.home.username}/bin"
      "/run/current-system/sw/bin"
      "/nix/var/nix/profiles/default/bin"
      "/usr/local/bin"
      "/usr/bin"
      "/bin"
      "/usr/sbin"
      "/sbin"
    ]
  );

  plist = (pkgs.formats.plist { }).generate "com.asmvik.yabai.plist" {
    Label = "com.asmvik.yabai";
    ProgramArguments = [ serviceExecutable ];
    EnvironmentVariables.PATH = launchPath;
    RunAtLoad = true;
    KeepAlive = {
      SuccessfulExit = false;
      Crashed = true;
    };
    StandardOutPath = "/tmp/yabai_${config.home.username}.out.log";
    StandardErrorPath = "/tmp/yabai_${config.home.username}.err.log";
    ProcessType = "Interactive";
    Nice = -20;
  };

  restartIdentity = pkgs.writeText "yabai-restart-identity" ''
    ${serviceExecutable}
    ${if validatedNativeConfig == null then "no-native-config" else toString validatedNativeConfig}
    ${legacyConfig}
  '';

  stateDirectory = "${config.xdg.stateHome}/home-manager/yabai";
  launchAgentPath = "${config.home.homeDirectory}/Library/LaunchAgents/com.asmvik.yabai.plist";

  enableService =
    lib.hm.dag.entryAfter
      [
        "writeBoundary"
        "linkGeneration"
        "copyApps"
      ]
      ''
        service_label=com.asmvik.yabai
        service_domain="gui/$UID"
        service_target="$service_domain/$service_label"
        desired_plist=${lib.escapeShellArg (toString plist)}
        desired_trigger=${lib.escapeShellArg (toString restartIdentity)}
        service_executable=${lib.escapeShellArg serviceExecutable}
        destination=${lib.escapeShellArg launchAgentPath}
        state_directory=${lib.escapeShellArg stateDirectory}
        applied_plist="$state_directory/applied.plist"
        applied_trigger="$state_directory/applied-trigger"
        force_takeover=${if cfg.service.force then "1" else "0"}

        run /bin/mkdir -p "$(/usr/bin/dirname "$destination")" "$state_directory"

        if [ ! -x "$service_executable" ]; then
          errorEcho "Configured yabai service executable is not executable: $service_executable"
          exit 1
        fi

        if [ -e "$destination" ] && [ ! -f "$applied_plist" ] \
          && ! /usr/bin/cmp -s "$desired_plist" "$destination" \
          && [ "$force_takeover" -ne 1 ]; then
          errorEcho "Refusing to replace unmanaged yabai LaunchAgent at $destination"
          errorEcho "Set services.yabai.service.force = true to adopt it explicitly."
          exit 1
        fi

        if [ -f "$applied_plist" ] && [ -e "$destination" ] \
          && ! /usr/bin/cmp -s "$applied_plist" "$destination" \
          && ! /usr/bin/cmp -s "$desired_plist" "$destination" \
          && [ "$force_takeover" -ne 1 ]; then
          errorEcho "The Home Manager yabai LaunchAgent was modified outside Home Manager."
          errorEcho "Restore it or set services.yabai.service.force = true to replace it."
          exit 1
        fi

        plist_changed=1
        if [ -f "$destination" ] && /usr/bin/cmp -s "$desired_plist" "$destination"; then
          plist_changed=0
        fi

        service_loaded=0
        if /bin/launchctl print "$service_target" >/dev/null 2>&1; then
          service_loaded=1
        fi

        previous_plist=""
        if [ "$plist_changed" -eq 1 ]; then
          previous_plist="$(/usr/bin/mktemp "$state_directory/previous.XXXXXX")"
          if [ -f "$destination" ]; then
            run /bin/cp "$destination" "$previous_plist"
          else
            run /bin/rm -f "$previous_plist"
            previous_plist=""
          fi

          if [ "$service_loaded" -eq 1 ]; then
            run /bin/launchctl bootout "$service_target"
            service_loaded=0
          fi

          if ! run /usr/bin/install -m 0644 "$desired_plist" "$destination"; then
            [ -z "$previous_plist" ] || run /bin/rm -f "$previous_plist"
            exit 1
          fi
        fi

        run /bin/launchctl enable "$service_target"
        if [ "$service_loaded" -eq 0 ]; then
          if ! run /bin/launchctl bootstrap "$service_domain" "$destination"; then
            errorEcho "Failed to load the new yabai LaunchAgent; restoring the previous plist."
            if [ -n "$previous_plist" ] && [ -f "$previous_plist" ]; then
              run /usr/bin/install -m 0644 "$previous_plist" "$destination"
              run /bin/launchctl bootstrap "$service_domain" "$destination" || true
            else
              run /bin/rm -f "$destination"
            fi
            [ -z "$previous_plist" ] || run /bin/rm -f "$previous_plist"
            exit 1
          fi
          service_loaded=1
        fi

        trigger_changed=1
        if [ -f "$applied_trigger" ] && /usr/bin/cmp -s "$desired_trigger" "$applied_trigger"; then
          trigger_changed=0
        fi
        if [ "$plist_changed" -eq 0 ] && [ "$trigger_changed" -eq 1 ]; then
          run /bin/launchctl kickstart -k "$service_target"
        fi

        run /usr/bin/install -m 0600 "$desired_plist" "$applied_plist.new"
        run /bin/mv -f "$applied_plist.new" "$applied_plist"
        run /usr/bin/install -m 0600 "$desired_trigger" "$applied_trigger.new"
        run /bin/mv -f "$applied_trigger.new" "$applied_trigger"
        [ -z "$previous_plist" ] || run /bin/rm -f "$previous_plist"
      '';

  disableService = lib.hm.dag.entryAfter [ "writeBoundary" ] ''
    service_label=com.asmvik.yabai
    service_target="gui/$UID/$service_label"
    destination=${lib.escapeShellArg launchAgentPath}
    state_directory=${lib.escapeShellArg stateDirectory}
    applied_plist="$state_directory/applied.plist"

    if [ -f "$applied_plist" ]; then
      if [ ! -e "$destination" ] || /usr/bin/cmp -s "$applied_plist" "$destination"; then
        if /bin/launchctl print "$service_target" >/dev/null 2>&1; then
          run /bin/launchctl bootout "$service_target"
        fi
        run /bin/launchctl disable "$service_target" || true
        run /bin/rm -f "$destination" "$state_directory/applied.plist" "$state_directory/applied-trigger"
        run /bin/rmdir "$state_directory" 2>/dev/null || true
      else
        warnEcho "Not removing divergent yabai LaunchAgent at $destination"
      fi
    fi
  '';
in
{
  options.services.yabai = {
    enable = lib.mkEnableOption "the yabai window manager";

    install = {
      source = lib.mkOption {
        type = lib.types.enum [
          "head"
          "commit"
          "nightly"
          "release"
        ];
        default = "head";
        description = ''
          Installation source. `head` builds the revision pinned by the yabai
          flake input, `commit` builds an explicitly fetched revision, and the
          artifact modes install immutable signed application archives.
        '';
      };

      commit = {
        rev = lib.mkOption {
          type = with lib.types; nullOr str;
          default = null;
          example = "8955858e10ec6bb04ec4bd8c17a443e94c02b200";
          description = "Git revision to compile when install.source is `commit`.";
        };

        hash = lib.mkOption {
          type = with lib.types; nullOr str;
          default = null;
          example = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
          description = "Nix source hash for the selected Git revision.";
        };

        version = lib.mkOption {
          type = with lib.types; nullOr str;
          default = null;
          example = "0.0.1-nightly.8955858e10ec";
          description = "Optional package version for the selected Git revision.";
        };
      };

      nightly = lib.mkOption {
        type = lib.types.submodule artifactOptions;
        default = { };
        description = "Pinned metadata for a CI-produced signed nightly application archive.";
      };

      release = lib.mkOption {
        type = lib.types.submodule artifactOptions;
        default = { };
        description = "Pinned metadata for a CI-produced signed release application archive.";
      };
    };

    config = {
      nativeConfig = lib.mkOption {
        type = with lib.types; nullOr attrs;
        default = null;
        description = "Native yabai configuration serialized as JSON and validated before activation.";
      };

      nativeConfigFile = lib.mkOption {
        type = with lib.types; nullOr path;
        default = null;
        description = "Existing JSONC configuration file to validate and install.";
      };

      extraConfig = lib.mkOption {
        type = lib.types.lines;
        default = "";
        description = "Legacy yabairc commands executed after native configuration.";
      };

      force = lib.mkOption {
        type = lib.types.bool;
        default = false;
        description = "Whether Home Manager may replace existing yabai configuration paths.";
      };
    };

    service = {
      enable = lib.mkOption {
        type = lib.types.bool;
        default = true;
        description = "Whether to install and load the direct com.asmvik.yabai LaunchAgent.";
      };

      executable = lib.mkOption {
        type = with lib.types; nullOr str;
        default = null;
        example = "/Users/alice/Applications/Home Manager Apps/Yabai.app/Contents/MacOS/yabai";
        description = ''
          Absolute executable path written to the LaunchAgent. The selected
          package executable is used by default. Set this to a stable copied
          application path when macOS privacy grants should follow that path.
        '';
      };

      finalExecutable = lib.mkOption {
        type = lib.types.str;
        readOnly = true;
        description = "Resolved executable path written to the LaunchAgent.";
      };

      force = lib.mkOption {
        type = lib.types.bool;
        default = false;
        description = "Whether to replace an unmanaged or locally modified yabai LaunchAgent.";
      };
    };

    finalPackage = lib.mkOption {
      type = lib.types.package;
      readOnly = true;
      description = "Resolved application package selected by services.yabai.install.";
    };
  };

  config = lib.mkMerge [
    {
      assertions = [
        {
          assertion = !cfg.enable || pkgs.stdenv.hostPlatform.isDarwin;
          message = "services.yabai is only supported on Darwin";
        }
        {
          assertion = cfg.config.nativeConfig == null || cfg.config.nativeConfigFile == null;
          message = "Set only one of services.yabai.config.nativeConfig and nativeConfigFile";
        }
        {
          assertion = cfg.service.executable == null || lib.hasPrefix "/" cfg.service.executable;
          message = "services.yabai.service.executable must be an absolute path";
        }
        {
          assertion =
            !cfg.enable
            || cfg.install.source != "commit"
            || (cfg.install.commit.rev != null && cfg.install.commit.hash != null);
          message = "The commit install source requires services.yabai.install.commit.rev and hash";
        }
        {
          assertion =
            !cfg.enable
            || cfg.install.source != "nightly"
            || (
              cfg.install.nightly.url != null
              && cfg.install.nightly.hash != null
              && cfg.install.nightly.version != null
            );
          message = "The nightly install source requires url, hash, and version";
        }
        {
          assertion =
            !cfg.enable
            || cfg.install.source != "release"
            || (
              cfg.install.release.url != null
              && cfg.install.release.hash != null
              && cfg.install.release.version != null
            );
          message = "The release install source requires url, hash, and version";
        }
      ];
    }

    (lib.mkIf cfg.enable {
      services.yabai = {
        finalPackage = selectedPackage;
        service.finalExecutable = serviceExecutable;
      };
      home.packages = [ selectedPackage ];

      xdg.configFile."yabai/yabairc" = {
        source = legacyConfig;
        force = cfg.config.force;
      };

      xdg.configFile."yabai/yabai.jsonc" = lib.mkIf (validatedNativeConfig != null) {
        source = validatedNativeConfig;
        force = cfg.config.force;
      };
    })

    {
      home.activation.yabaiService =
        if cfg.enable && cfg.service.enable then enableService else disableService;
    }
  ];
}
