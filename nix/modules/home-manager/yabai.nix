{
  config,
  lib,
  pkgs,
  baseVersion,
  yabaiHeadPackage,
  ...
}:
let
  cfg = config.programs.yabai;
  requiredSpaceCount = if cfg.spaces.ensureCount == null then 0 else cfg.spaces.ensureCount;
  nightlyPin = builtins.fromJSON (builtins.readFile ../../nightly.json);
  releasePin = builtins.fromJSON (builtins.readFile ../../release.json);

  pinnedNightly = {
    inherit (nightlyPin) version hash;
    url =
      if nightlyPin.tag == null then
        null
      else
        "https://github.com/markfeinstein/yabai/releases/download/${nightlyPin.tag}/Yabai-${nightlyPin.tag}.zip";
  };

  pinnedRelease = {
    inherit (releasePin) version hash;
    url = "https://github.com/markfeinstein/yabai/releases/download/v${releasePin.version}/Yabai-v${releasePin.version}.zip";
  };

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

  authoredSettings =
    if cfg.settings != null then
      pkgs.writeText "yabai-authored.jsonc" (builtins.toJSON cfg.settings)
    else
      cfg.settingsFile;

  validatedSettings =
    if authoredSettings == null then
      null
    else
      pkgs.runCommand "yabai.jsonc" { } ''
        cp ${lib.escapeShellArg (toString authoredSettings)} "$out"
        ${lib.escapeShellArg packageExecutable} --check-config "$out"
      '';

  legacyConfig = pkgs.writeText "yabairc" (cfg.extraConfig + "\n");

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
    ${if validatedSettings == null then "no-settings" else toString validatedSettings}
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
          errorEcho "Set programs.yabai.service.force = true to adopt it explicitly."
          exit 1
        fi

        if [ -f "$applied_plist" ] && [ -e "$destination" ] \
          && ! /usr/bin/cmp -s "$applied_plist" "$destination" \
          && ! /usr/bin/cmp -s "$desired_plist" "$destination" \
          && [ "$force_takeover" -ne 1 ]; then
          errorEcho "The Home Manager yabai LaunchAgent was modified outside Home Manager."
          errorEcho "Restore it or set programs.yabai.service.force = true to replace it."
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

  ensureSpaces = lib.hm.dag.entryAfter [ "yabaiService" ] ''
    service_executable=${lib.escapeShellArg serviceExecutable}
    required_space_count=${toString requiredSpaceCount}
    reload_settings=${if validatedSettings == null then "0" else "1"}

    if [ -n "''${DRY_RUN:-}" ]; then
      echo "Would ensure at least $required_space_count ordinary yabai Spaces globally."
    else
      space_count() {
        "$service_executable" -m query --spaces 2>/dev/null \
          | ${pkgs.jq}/bin/jq -er '[.[] | select(.["is-native-fullscreen"] != true)] | length'
      }

      current_space_count=""
      attempts=50
      while [ "$attempts" -gt 0 ]; do
        if current_space_count="$(space_count)"; then
          break
        fi
        /bin/sleep 0.1
        attempts=$((attempts - 1))
      done
      if [ -z "$current_space_count" ]; then
        errorEcho "Could not query yabai Spaces after starting the service."
        exit 1
      fi

      created_spaces=0
      while [ "$current_space_count" -lt "$required_space_count" ]; do
        previous_space_count=$current_space_count
        if ! "$service_executable" -m space --create; then
          errorEcho "Could not create the required yabai Space."
          errorEcho "Space creation requires partial SIP disablement and a working scripting addition."
          exit 1
        fi

        next_space_count=""
        attempts=50
        while [ "$attempts" -gt 0 ]; do
          candidate_space_count="$(space_count || true)"
          if [ -n "$candidate_space_count" ] \
            && [ "$candidate_space_count" -gt "$previous_space_count" ]; then
            next_space_count=$candidate_space_count
            break
          fi
          /bin/sleep 0.1
          attempts=$((attempts - 1))
        done
        if [ -z "$next_space_count" ]; then
          errorEcho "The yabai Space count did not increase after creation."
          exit 1
        fi

        current_space_count=$next_space_count
        created_spaces=1
      done

      if [ "$created_spaces" -eq 1 ] && [ "$reload_settings" -eq 1 ]; then
        if ! "$service_executable" -m config reload; then
          errorEcho "Created the required Spaces, but could not reload yabai settings."
          exit 1
        fi
      fi
    fi
  '';
in
{
  options.programs.yabai = {
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
        default = pinnedNightly;
        description = ''
          Pinned metadata for a CI-produced signed nightly application archive.
          Defaults to the latest nightly verified by this module revision when available.
        '';
      };

      release = lib.mkOption {
        type = lib.types.submodule artifactOptions;
        default = pinnedRelease;
        description = ''
          Pinned metadata for a CI-produced signed release application archive.
          Defaults to the latest release verified by this module revision.
        '';
      };
    };

    spaces.ensureCount = lib.mkOption {
      type = with lib.types; nullOr ints.positive;
      default = null;
      example = 9;
      description = ''
        Minimum global count of ordinary (non-native-fullscreen) Spaces.
        Missing Spaces are created on the active display after the service
        starts; extra Spaces are never removed. Creation requires partial SIP
        disablement and a working scripting addition, and activation fails if
        the requested count cannot be reached.
      '';
    };

    settings = lib.mkOption {
      type = with lib.types; nullOr attrs;
      default = null;
      description = "Declarative yabai settings serialized as JSON and validated before activation.";
    };

    settingsFile = lib.mkOption {
      type = with lib.types; nullOr path;
      default = null;
      description = "Existing JSONC settings file to validate and install.";
    };

    extraConfig = lib.mkOption {
      type = lib.types.lines;
      default = "";
      description = "Legacy yabairc commands executed after declarative settings are loaded.";
    };

    force = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = "Whether Home Manager may replace existing yabai configuration paths.";
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
      description = "Resolved application package selected by programs.yabai.install.";
    };
  };

  config = lib.mkMerge [
    {
      assertions = [
        {
          assertion = !cfg.enable || pkgs.stdenv.hostPlatform.isDarwin;
          message = "programs.yabai is only supported on Darwin";
        }
        {
          assertion = cfg.settings == null || cfg.settingsFile == null;
          message = "Set only one of programs.yabai.settings and settingsFile";
        }
        {
          assertion = cfg.service.executable == null || lib.hasPrefix "/" cfg.service.executable;
          message = "programs.yabai.service.executable must be an absolute path";
        }
        {
          assertion = cfg.spaces.ensureCount == null || cfg.service.enable;
          message = "programs.yabai.spaces.ensureCount requires programs.yabai.service.enable";
        }
        {
          assertion =
            !cfg.enable
            || cfg.install.source != "commit"
            || (cfg.install.commit.rev != null && cfg.install.commit.hash != null);
          message = "The commit install source requires programs.yabai.install.commit.rev and hash";
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
      programs.yabai = {
        finalPackage = selectedPackage;
        service.finalExecutable = serviceExecutable;
      };
      home.packages = [ selectedPackage ];

      xdg.configFile."yabai/yabairc" = {
        source = legacyConfig;
        inherit (cfg) force;
      };

      xdg.configFile."yabai/yabai.jsonc" = lib.mkIf (validatedSettings != null) {
        source = validatedSettings;
        inherit (cfg) force;
      };
    })

    {
      home.activation = {
        yabaiService = if cfg.enable && cfg.service.enable then enableService else disableService;
        yabaiEnsureSpaces = lib.mkIf (cfg.enable && cfg.spaces.ensureCount != null) ensureSpaces;
      };
    }
  ];
}
