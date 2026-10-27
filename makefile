FRAMEWORK_PATH = -F/System/Library/PrivateFrameworks
FRAMEWORK      = -framework Carbon -framework Cocoa -framework ColorSync -framework CoreServices -framework CoreVideo -framework ScreenCaptureKit -framework SkyLight
CLI_FLAGS      =
MACOS_MIN_VERSION = 14.0
COMMON_FLAGS   = -std=c11 -Wall -Wextra -g -fvisibility=hidden -mmacosx-version-min=$(MACOS_MIN_VERSION) -fno-objc-arc -arch x86_64 -arch arm64 -sectcreate __TEXT __info_plist $(INFO_PLIST)
DEBUG_FLAGS    = $(COMMON_FLAGS) -O0
RELEASE_FLAGS  = $(COMMON_FLAGS) -DNDEBUG -O3
ASAN_FLAGS     = $(COMMON_FLAGS) -O0 -fsanitize=address,undefined
TSAN_FLAGS     = $(COMMON_FLAGS) -O0 -fsanitize=thread,undefined
BUILD_PATH     = ./bin
RELEASE_PATH   = ./release
REPO_ROOT      := $(CURDIR)
BUILD_PATH_ORIGIN := $(origin BUILD_PATH)
RELEASE_PATH_ORIGIN := $(origin RELEASE_PATH)
DOC_PATH       = ./doc
SCRIPT_PATH    = ./scripts
ASSET_PATH     = ./assets
SMP_PATH       = ./examples
OSAX_PATH      = ./src/osax
INFO_PLIST     = $(ASSET_PATH)/Info.plist
DEBUG_BIN      = $(BUILD_PATH)/debug/yabai
RELEASE_BIN    = $(BUILD_PATH)/release/yabai
ASAN_BIN       = $(BUILD_PATH)/asan/yabai
TSAN_BIN       = $(BUILD_PATH)/tsan/yabai
TAG_CORPUS_BIN = $(BUILD_PATH)/debug/window-server-tag-corpus
GEOMETRY_PROBE_BIN = $(BUILD_PATH)/debug/window-server-geometry-probe
NATIVE_TAB_RECORDER_BIN = $(BUILD_PATH)/debug/native-tab-event-recorder
CORNER_GEOMETRY_PROBE_BIN = $(BUILD_PATH)/debug/window-corner-geometry-probe
YABAI_COMMON   = ./src/manifest.m
YABAI_DEPS     = $(filter-out ./src/menu_bar_model.c ./src/menu_bar_model.h,$(wildcard ./src/*.c ./src/*.h ./src/*.m ./src/misc/*.c ./src/misc/*.h ./src/misc/*.m)) $(INFO_PLIST) makefile
DEBUG_MODEL    = $(BUILD_PATH)/debug/menu_bar_model.o
RELEASE_MODEL  = $(BUILD_PATH)/release/menu_bar_model.o
ASAN_MODEL     = $(BUILD_PATH)/asan/menu_bar_model.o
TSAN_MODEL     = $(BUILD_PATH)/tsan/menu_bar_model.o
OSAX_DEPS      = makefile $(OSAX_PATH)/loader.m $(OSAX_PATH)/loader_shellcode.s $(OSAX_PATH)/loader_protocol.h $(OSAX_PATH)/payload.m $(OSAX_PATH)/common.h $(OSAX_PATH)/message_validation.h ./src/misc/hashtable.h ./src/misc/socket_io.h ./src/runtime_paths.h $(OSAX_PATH)/arm64_payload.m $(OSAX_PATH)/x64_payload.m
TAG_CORPUS_DEPS = scripts/window_server_tag_corpus.m scripts/window_server_tag_corpus_core.c scripts/window_server_tag_corpus_core.h src/window_server_snapshot.c src/window_server_snapshot.h src/window_corner_geometry.c src/window_corner_geometry.h src/window_server_tags.h src/window_server_classifier.c src/window_server_classifier.h src/misc/extern.h makefile
GEOMETRY_PROBE_DEPS = scripts/window_server_geometry_probe.m src/window_server_hit_test.c src/window_server_hit_test.h src/misc/extern.h makefile
NATIVE_TAB_RECORDER_DEPS = scripts/native_tab_event_recorder.m scripts/native_tab_event_recorder_core.c scripts/native_tab_event_recorder_core.h src/misc/extern.h makefile
CORNER_GEOMETRY_PROBE_DEPS = scripts/window_corner_geometry_probe.m src/window_corner_geometry.c src/window_corner_geometry.h src/misc/extern.h makefile
OSAX_FRAMEWORK = $(FRAMEWORK_PATH) -framework SkyLight -framework Foundation -framework Carbon -framework ColorSync
XCRUN          ?= xcrun
XXD            ?= xxd
CODESIGN       ?= /usr/bin/codesign
SIGN_IDENTITY  ?= yabai-cert
SHASUM         ?= /usr/bin/shasum
STAT           ?= /usr/bin/stat
ACL_LIST       ?= /bin/ls
TAR            ?= /usr/bin/tar
ASCIIDOCTOR    ?= asciidoctor
MAN_DATE_SETUP = date=$$(awk '$$1 == ":revdate:" { count++; if (NF == 2) value = $$2; else invalid = 1 } END { if (count != 1 || invalid) exit 1; print value }' '$(DOC_PATH)/yabai.asciidoc') || { echo "doc/yabai.asciidoc must contain exactly one YYYY-MM-DD revdate" >&2; exit 1; }; case "$$date" in [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]) ;; *) echo "doc/yabai.asciidoc must contain exactly one YYYY-MM-DD revdate" >&2; exit 1 ;; esac
PYTHON         ?= python3
SUDO           ?= /usr/bin/sudo
ROOT_SHELL     ?= /bin/sh
DEPLOY_DIR     ?= /usr/local/bin
DEPLOY_TRUST_ROOT ?= /
DEPLOY_OWNER_UID ?= 0
# Must be supplied by trusted configuration and pin a cryptographic identity
# (for example, an exact requirement that pins the leaf certificate hash).
DEPLOY_REQUIREMENT ?=

.PHONY: all debug release asan tsan window-server-tag-corpus window-server-geometry-probe window-corner-geometry-probe native-tab-event-recorder install man man-check man-contract icon archive publish sign deploy \
        test test-asan test-tsan test-benchmark-ax test-benchmark-ws-snapshot test-benchmark-space-inventory test-benchmark-window-query test-benchmark-snapshot-reuse test-benchmark-animation-clock test-benchmark-window-server-tag-corpus test-benchmark-window-server-classifier test-benchmark-window-constraints test-benchmark-window-server-hit-test test-arm64 test-x86_64 test-architectures test-all syntax \
        integration-hide-on-close integration \
        clean-debug clean-release clean-asan clean-tsan clean distclean
.NOTPARALLEL:

all: debug

debug: $(DEBUG_BIN)
release: $(RELEASE_BIN)
asan: $(ASAN_BIN)
tsan: $(TSAN_BIN)
window-server-tag-corpus: $(TAG_CORPUS_BIN)
window-server-geometry-probe: $(GEOMETRY_PROBE_BIN)
window-corner-geometry-probe: $(CORNER_GEOMETRY_PROBE_BIN)
native-tab-event-recorder: $(NATIVE_TAB_RECORDER_BIN)
# Historical "install" builds the optimized artifact; it does not copy files.
install: release

test:
	$(MAKE) -C tests run

test-asan:
	$(MAKE) -C tests run-asan

test-tsan:
	$(MAKE) -C tests run-tsan

test-benchmark-ax:
	$(MAKE) -C tests benchmark-ax

test-benchmark-ws-snapshot:
	$(MAKE) -C tests benchmark-ws-snapshot

test-benchmark-space-inventory:
	$(MAKE) -C tests benchmark-space-inventory

test-benchmark-window-query:
	$(MAKE) -C tests benchmark-window-query

test-benchmark-snapshot-reuse:
	$(MAKE) -C tests benchmark-snapshot-reuse

test-benchmark-animation-clock:
	$(MAKE) -C tests benchmark-animation-clock

test-benchmark-window-server-tag-corpus:
	$(MAKE) -C tests benchmark-window-server-tag-corpus

test-benchmark-window-server-classifier:
	$(MAKE) -C tests benchmark-window-server-classifier

test-benchmark-window-constraints:
	$(MAKE) -C tests benchmark-window-constraints

test-benchmark-window-server-hit-test:
	$(MAKE) -C tests benchmark-window-server-hit-test

test-arm64:
	$(MAKE) -C tests arch-arm64

test-x86_64:
	$(MAKE) -C tests arch-x86_64

test-architectures: test-arm64 test-x86_64

test-all: test test-asan test-tsan test-architectures syntax man-contract

syntax:
	$(MAKE) -C tests syntax

# Explicit opt-in only: this opens windows and drives Accessibility UI against
# a running daemon on the current BSP Space. Never a dependency of CI.
integration-hide-on-close: debug
	$(MAKE) -C tests integration-hide-on-close

integration: integration-hide-on-close

$(TAG_CORPUS_BIN): $(TAG_CORPUS_DEPS)
	@mkdir -p '$(dir $@)'
	$(XCRUN) clang scripts/window_server_tag_corpus.m src/window_corner_geometry.c -o '$@' $(COMMON_FLAGS) -O0 $(FRAMEWORK_PATH) -framework Cocoa -framework SkyLight

$(GEOMETRY_PROBE_BIN): $(GEOMETRY_PROBE_DEPS)
	@mkdir -p '$(dir $@)'
	$(XCRUN) clang scripts/window_server_geometry_probe.m -o '$@' $(COMMON_FLAGS) -O0 $(FRAMEWORK_PATH) -framework Cocoa -framework SkyLight

$(CORNER_GEOMETRY_PROBE_BIN): $(CORNER_GEOMETRY_PROBE_DEPS)
	@mkdir -p '$(dir $@)'
	$(XCRUN) clang scripts/window_corner_geometry_probe.m -o '$@' $(COMMON_FLAGS) -O0 $(FRAMEWORK_PATH) -framework Cocoa -framework SkyLight

$(NATIVE_TAB_RECORDER_BIN): $(NATIVE_TAB_RECORDER_DEPS)
	@mkdir -p '$(dir $@)'
	$(XCRUN) clang scripts/native_tab_event_recorder.m scripts/native_tab_event_recorder_core.c -o '$@' $(COMMON_FLAGS) -O0 $(FRAMEWORK_PATH) -framework Cocoa -framework SkyLight

# Generate both native scripting-addition artifacts as private temporaries,
# then publish only one combined compiler input. Explicit source and makefile
# dependencies keep unchanged builds incremental; a dependency change advances
# the generated source mtime so the embedding binary rebuilds exactly once.
define OSAX_RULE
$(BUILD_PATH)/$(1)/osax/generated.c: $(OSAX_DEPS)
	@set -eu; \
	dir='$(BUILD_PATH)/$(1)/osax'; mkdir -p "$$$$dir"; \
	payload=$$$$(mktemp "$$$$dir/.payload.XXXXXX"); loader=$$$$(mktemp "$$$$dir/.loader.XXXXXX"); generated=$$$$(mktemp "$$$$dir/.generated.c.XXXXXX"); \
	trap 'rm -f "$$$$payload" "$$$$loader" "$$$$generated"' 0 HUP INT TERM; \
	$(XCRUN) clang '$(OSAX_PATH)/payload.m' -shared -fPIC -O3 -mmacosx-version-min=$(MACOS_MIN_VERSION) -arch x86_64 -arch arm64e -o "$$$$payload" $(OSAX_FRAMEWORK); \
	$(XCRUN) clang '$(OSAX_PATH)/loader.m' -x assembler-with-cpp '$(OSAX_PATH)/loader_shellcode.s' -O3 -mmacosx-version-min=$(MACOS_MIN_VERSION) -arch x86_64 -arch arm64e -o "$$$$loader" -framework Cocoa; \
	$(XXD) -i -n __src_osax_payload "$$$$payload" > "$$$$generated"; \
	$(XXD) -i -n __src_osax_loader "$$$$loader" >> "$$$$generated"; \
	if [ -f "$$$$dir/generated.c" ] && cmp -s "$$$$generated" "$$$$dir/generated.c"; then touch "$$$$dir/generated.c"; rm -f "$$$$generated"; else mv -f "$$$$generated" "$$$$dir/generated.c"; fi; \
	rm -f "$$$$payload" "$$$$loader"; \
	trap - 0 HUP INT TERM
endef
$(eval $(call OSAX_RULE,debug))
$(eval $(call OSAX_RULE,release))
$(eval $(call OSAX_RULE,asan))
$(eval $(call OSAX_RULE,tsan))

define MODEL_RULE
$(BUILD_PATH)/$(1)/menu_bar_model.o: ./src/menu_bar_model.c ./src/menu_bar_model.h makefile
	@mkdir -p '$(BUILD_PATH)/$(1)'
	$(XCRUN) clang -c ./src/menu_bar_model.c -o '$$@' $($(2)_FLAGS) -MMD -MP
endef
$(eval $(call MODEL_RULE,debug,DEBUG))
$(eval $(call MODEL_RULE,release,RELEASE))
$(eval $(call MODEL_RULE,asan,ASAN))
$(eval $(call MODEL_RULE,tsan,TSAN))

$(DEBUG_BIN): $(YABAI_DEPS) $(BUILD_PATH)/debug/osax/generated.c $(DEBUG_MODEL)
	mkdir -p '$(BUILD_PATH)/debug'
	$(XCRUN) clang $(YABAI_COMMON) '$(BUILD_PATH)/debug/osax/generated.c' $(DEBUG_MODEL) $(DEBUG_FLAGS) $(CLI_FLAGS) $(FRAMEWORK_PATH) $(FRAMEWORK) -o '$@'

$(RELEASE_BIN): $(YABAI_DEPS) $(BUILD_PATH)/release/osax/generated.c $(RELEASE_MODEL)
	mkdir -p '$(BUILD_PATH)/release'
	$(XCRUN) clang $(YABAI_COMMON) '$(BUILD_PATH)/release/osax/generated.c' $(RELEASE_MODEL) $(RELEASE_FLAGS) $(CLI_FLAGS) $(FRAMEWORK_PATH) $(FRAMEWORK) -o '$@'

$(ASAN_BIN): $(YABAI_DEPS) $(BUILD_PATH)/asan/osax/generated.c $(ASAN_MODEL)
	mkdir -p '$(BUILD_PATH)/asan'
	$(XCRUN) clang $(YABAI_COMMON) '$(BUILD_PATH)/asan/osax/generated.c' $(ASAN_MODEL) $(ASAN_FLAGS) $(CLI_FLAGS) $(FRAMEWORK_PATH) $(FRAMEWORK) -o '$@'

$(TSAN_BIN): $(YABAI_DEPS) $(BUILD_PATH)/tsan/osax/generated.c $(TSAN_MODEL)
	mkdir -p '$(BUILD_PATH)/tsan'
	$(XCRUN) clang $(YABAI_COMMON) '$(BUILD_PATH)/tsan/osax/generated.c' $(TSAN_MODEL) $(TSAN_FLAGS) $(CLI_FLAGS) $(FRAMEWORK_PATH) $(FRAMEWORK) -o '$@'

man:
	@set -eu; $(MAN_DATE_SETUP); '$(ASCIIDOCTOR)' -a "docdate=$$date" -b manpage '$(DOC_PATH)/yabai.asciidoc' -o '$(DOC_PATH)/yabai.1'

man-contract:
	./tests/doc_contract.sh --self-test

man-check: man-contract
	@set -eu; command -v '$(ASCIIDOCTOR)' >/dev/null 2>&1 || { echo "asciidoctor is required for man-check" >&2; exit 127; }; $(MAN_DATE_SETUP); tmp=$$(mktemp "$${TMPDIR:-/tmp}/yabai-man.XXXXXX"); trap 'rm -f "$$tmp"' 0 HUP INT TERM; '$(ASCIIDOCTOR)' -a "docdate=$$date" -b manpage '$(DOC_PATH)/yabai.asciidoc' -o "$$tmp"; cmp "$$tmp" '$(DOC_PATH)/yabai.1'

icon: release
	$(PYTHON) '$(SCRIPT_PATH)/seticon.py' '$(ASSET_PATH)/icon/2x/icon-512px@2x.png' '$(RELEASE_BIN)'

sign: release
	$(CODESIGN) -fs "$(SIGN_IDENTITY)" '$(RELEASE_BIN)'

archive: man-check release sign icon
	@set -eu; \
	mkdir -p '$(RELEASE_PATH)'; \
	stage=$$(mktemp -d '$(RELEASE_PATH)/.archive-stage.XXXXXX'); tmp=; \
	cleanup() { [ -z "$$tmp" ] || rm -f "$$tmp"; if [ -d "$$stage" ]; then find "$$stage" -depth -delete; fi; }; \
	trap cleanup 0 HUP INT TERM; \
	tmp=$$(mktemp '$(RELEASE_PATH)/.archive.XXXXXX'); \
	mkdir -p "$$stage/archive/bin" "$$stage/archive/doc" "$$stage/archive/examples"; \
	cp '$(RELEASE_BIN)' "$$stage/archive/bin/yabai"; \
	cp '$(DOC_PATH)/yabai.1' "$$stage/archive/doc/yabai.1"; \
	cp -R '$(SMP_PATH)/.' "$$stage/archive/examples/"; \
	release=$$('$(RELEASE_BIN)' --version); final='$(RELEASE_PATH)/'"$$release"'.tar.gz'; \
	'$(TAR)' -czf "$$tmp" -C "$$stage" archive; \
	members=$$('$(TAR)' -tzf "$$tmp"); \
	printf '%s\n' "$$members" | grep -qx 'archive/bin/yabai'; \
	printf '%s\n' "$$members" | grep -qx 'archive/doc/yabai.1'; \
	if printf '%s\n' "$$members" | grep -qv '^archive/'; then exit 1; fi; \
	mv -f "$$tmp" "$$final"; \
	find "$$stage" -depth -delete; trap - 0 HUP INT TERM

publish:
	@set -eu; \
	installer='$(SCRIPT_PATH)/install.sh'; binary='$(RELEASE_BIN)'; \
	[ -f "$$binary" ] && [ -x "$$binary" ]; \
	release=$$("$$binary" --version); case "$$release" in yabai-v*) ;; *) exit 1 ;; esac; \
	version=$${release#yabai-v}; [ -n "$$version" ]; case "$$version" in *[!0-9A-Za-z._-]*) exit 1 ;; esac; \
	archive='$(RELEASE_PATH)/'"$$release"'.tar.gz'; [ -f "$$archive" ]; \
	hash=$$($(SHASUM) -a 256 "$$archive" | cut -d ' ' -f 1); \
	[ "$${#hash}" -eq 64 ]; case "$$hash" in *[!0-9a-f]*) exit 1 ;; esac; \
	[ "$$(grep -c '^VERSION=' "$$installer")" -eq 1 ]; [ "$$(grep -c '^EXPECTED_HASH=' "$$installer")" -eq 1 ]; \
	tmp=$$(mktemp '$(SCRIPT_PATH)/.install.sh.XXXXXX'); trap 'rm -f "$$tmp"' 0 HUP INT TERM; \
	sed -e "s/^VERSION=.*/VERSION=\"$$version\"/" -e "s/^EXPECTED_HASH=.*/EXPECTED_HASH=\"$$hash\"/" "$$installer" > "$$tmp"; \
	chmod 0755 "$$tmp"; mv -f "$$tmp" "$$installer"; trap - 0 HUP INT TERM

# DEPLOY_REQUIREMENT must come from trusted configuration and pin a
# cryptographic identity. Run 'make sign' explicitly before deploy; deploy only
# verifies the existing release artifact and never invokes the signer. The privileged shell validates every path component,
# verifies before and after replacement, and rolls back post-rename verification
# failures. Once final verification commits, SA/restart failure is reported but
# intentionally leaves the verified binary installed.
deploy:
	@set -eu; \
	requirement='$(DEPLOY_REQUIREMENT)'; [ -n "$$requirement" ]; source='$(RELEASE_BIN)'; \
	[ -f "$$source" ] && [ -x "$$source" ]; \
	stage=$$(mktemp "$${TMPDIR:-/tmp}/yabai-deploy-source.XXXXXX"); trap 'rm -f "$$stage"' 0 HUP INT TERM; \
	/bin/cp "$$source" "$$stage"; /bin/chmod 0500 "$$stage"; '$(CODESIGN)' --verify --strict -R "$$requirement" "$$stage"; \
	expected=$$('$(SHASUM)' -a 256 "$$stage" | /usr/bin/cut -d ' ' -f 1); expected_size=$$('$(STAT)' -f %z "$$stage"); \
	[ "$${#expected}" -eq 64 ]; case "$$expected" in *[!0-9a-fA-F]*) exit 1 ;; esac; case "$$expected_size" in ''|*[!0-9]*) exit 1 ;; esac; [ "$$expected_size" -gt 0 ]; \
	exec 3<"$$stage"; rm -f "$$stage"; trap - 0 HUP INT TERM; \
	/bin/cat <&3 | '$(SUDO)' '$(ROOT_SHELL)' -c 'PATH=/usr/bin:/bin:/usr/sbin:/sbin; export PATH; set -efu; dir=$$1; expected=$$2; expected_size=$$3; requirement=$$4; owner=$$5; codesign=$$6; shasum=$$7; stat=$$8; trust_root=$$9; acl_list=$${10}; no_acl() { acl_output=$$("$$acl_list" -lde "$$1") || return 1; ! printf "%s\n" "$$acl_output" | /usr/bin/grep -Eq "^[[:space:]]*[0-9]+:"; }; case "$$dir:$$trust_root" in /*:/*) ;; *) exit 1 ;; esac; [ -d "$$trust_root" ] && [ ! -L "$$trust_root" ] && no_acl "$$trust_root"; [ "$$(cd "$$trust_root" && pwd -P)" = "$$trust_root" ]; [ "$$("$$stat" -f %u "$$trust_root")" = "$$owner" ]; root_mode=$$("$$stat" -f %OLp "$$trust_root"); [ $$((0$$root_mode & 022)) -eq 0 ]; case "$$trust_root" in /) rel=$${dir#/} ;; *) case "$$dir" in "$$trust_root"/*) rel=$${dir#"$$trust_root"/} ;; *) exit 1 ;; esac ;; esac; [ -n "$$rel" ]; [ "$$(cd "$$dir" 2>/dev/null && pwd -P)" = "$$dir" ]; current=$$trust_root; old_ifs=$$IFS; IFS=/; set -- $$rel; IFS=$$old_ifs; for component do [ -n "$$component" ] && [ "$$component" != . ] && [ "$$component" != .. ]; if [ "$$current" = / ]; then current=/$$component; else current=$$current/$$component; fi; [ -d "$$current" ] && [ ! -L "$$current" ] && no_acl "$$current"; [ "$$("$$stat" -f %u "$$current")" = "$$owner" ]; mode=$$("$$stat" -f %OLp "$$current"); [ $$((0$$mode & 022)) -eq 0 ]; done; destination=$$dir/yabai; [ ! -L "$$destination" ]; { [ ! -e "$$destination" ] || { [ -f "$$destination" ] && no_acl "$$destination" && [ "$$("$$stat" -f %u "$$destination")" = "$$owner" ]; }; }; tmp=$$(/usr/bin/mktemp "$$dir/.yabai.deploy.XXXXXX"); backup=; installed=0; committed=0; reader=; cleanup() { [ -z "$$reader" ] || { /bin/kill "$$reader" 2>/dev/null || :; wait "$$reader" 2>/dev/null || :; }; if [ "$$committed" -eq 0 ]; then if [ -n "$$backup" ] && [ -f "$$backup" ]; then /bin/mv -f "$$backup" "$$destination"; backup=; elif [ "$$installed" -eq 1 ]; then /bin/rm -f "$$destination"; fi; fi; /bin/rm -f "$$tmp"; [ -z "$$backup" ] || /bin/rm -f "$$backup"; }; trap cleanup 0 HUP INT TERM; [ "$$("$$stat" -f %u "$$tmp")" = "$$owner" ] && no_acl "$$tmp"; exec 3<&0; read_limit=$$((expected_size+1)); [ "$$read_limit" -gt "$$expected_size" ]; /usr/bin/head -c "$$read_limit" <&3 >"$$tmp" & reader=$$!; ticks=0; while /bin/kill -0 "$$reader" 2>/dev/null; do [ "$$ticks" -lt 100 ] || exit 1; /bin/sleep 0.1; ticks=$$((ticks+1)); done; wait "$$reader"; reader=; [ "$$("$$stat" -f %z "$$tmp")" = "$$expected_size" ]; /bin/chmod 0755 "$$tmp"; "$$codesign" --verify --strict -R "$$requirement" "$$tmp"; actual=$$("$$shasum" -a 256 "$$tmp" | /usr/bin/cut -d " " -f 1); [ "$$actual" = "$$expected" ]; if [ -e "$$destination" ]; then backup=$$(/usr/bin/mktemp "$$dir/.yabai.backup.XXXXXX"); /bin/rm -f "$$backup"; /bin/mv "$$destination" "$$backup"; fi; /bin/mv "$$tmp" "$$destination"; installed=1; [ ! -L "$$destination" ] && no_acl "$$destination"; "$$codesign" --verify --strict -R "$$requirement" "$$destination"; actual=$$("$$shasum" -a 256 "$$destination" | /usr/bin/cut -d " " -f 1); [ "$$actual" = "$$expected" ]; committed=1; [ -z "$$backup" ] || /bin/rm -f "$$backup"; backup=; "$$destination" --uninstall-sa; "$$destination" --load-sa' sh '$(DEPLOY_DIR)' "$$expected" "$$expected_size" "$$requirement" '$(DEPLOY_OWNER_UID)' '$(CODESIGN)' '$(SHASUM)' '$(STAT)' '$(DEPLOY_TRUST_ROOT)' '$(ACL_LIST)'
	'$(DEPLOY_DIR)/yabai' --restart-service

# Each configuration can be removed independently; archives are only removed
# by distclean.
define CLEAN_RULE
clean-$(1):
	@set -eu; repo='$(REPO_ROOT)'; [ "$$$$(pwd -P)" = "$$$$repo" ]; \
	[ '$(BUILD_PATH_ORIGIN)' != 'command line' ]; [ '$(BUILD_PATH)' = './bin' ]; \
	root="$$$$repo/bin"; [ ! -L "$$$$root" ]; \
	if [ -e "$$$$root" ]; then [ -d "$$$$root" ] && [ "$$$$(cd "$$$$root" && pwd -P)" = "$$$$root" ]; fi; \
	target="$$$$root/$(1)"; [ ! -L "$$$$target" ]; \
	if [ -e "$$$$target" ]; then [ -d "$$$$target" ]; find "$$$$target" -mindepth 1 -delete; rmdir "$$$$target"; fi
endef
$(eval $(call CLEAN_RULE,debug))
$(eval $(call CLEAN_RULE,release))
$(eval $(call CLEAN_RULE,asan))
$(eval $(call CLEAN_RULE,tsan))

clean: clean-debug clean-release clean-asan clean-tsan
	@set -eu; repo='$(REPO_ROOT)'; [ "$$(pwd -P)" = "$$repo" ]; \
	[ '$(BUILD_PATH_ORIGIN)' != 'command line' ]; [ '$(BUILD_PATH)' = './bin' ]; \
	root="$$repo/bin"; [ ! -L "$$root" ]; \
	if [ -e "$$root" ]; then [ -d "$$root" ] && [ "$$(cd "$$root" && pwd -P)" = "$$root" ]; rmdir "$$root" 2>/dev/null || true; fi

distclean: clean
	@set -eu; repo='$(REPO_ROOT)'; [ "$$(pwd -P)" = "$$repo" ]; \
	[ '$(RELEASE_PATH_ORIGIN)' != 'command line' ]; [ '$(RELEASE_PATH)' = './release' ]; \
	root="$$repo/release"; [ ! -L "$$root" ]; \
	if [ -e "$$root" ]; then [ -d "$$root" ] && [ "$$(cd "$$root" && pwd -P)" = "$$root" ]; find "$$root" -mindepth 1 -delete; rmdir "$$root"; fi

-include $(DEBUG_MODEL:.o=.d) $(RELEASE_MODEL:.o=.d) $(ASAN_MODEL:.o=.d) $(TSAN_MODEL:.o=.d)
