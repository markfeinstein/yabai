FRAMEWORK_PATH = -F/System/Library/PrivateFrameworks
FRAMEWORK      = -framework Carbon -framework Cocoa -framework CoreServices -framework CoreVideo -framework SkyLight
CLI_FLAGS      =
BUILD_FLAGS    = -std=c11 -Wall -Wextra -g -O0 -fvisibility=hidden -mmacosx-version-min=11.0 -fno-objc-arc -arch x86_64 -arch arm64 -sectcreate __TEXT __info_plist $(INFO_PLIST)
BUILD_PATH     = ./bin
DOC_PATH       = ./doc
SCRIPT_PATH    = ./scripts
ASSET_PATH     = ./assets
SMP_PATH       = ./examples
ARCH_PATH      = ./archive
OSAX_SRC       = ./src/osax/payload_bin.c ./src/osax/loader_bin.c
YABAI_SRC      = ./src/manifest.m $(OSAX_SRC)
YABAI_DEPS     = $(wildcard ./src/*.c ./src/*.h ./src/*.m ./src/misc/*.c ./src/misc/*.h ./src/misc/*.m) $(OSAX_SRC) $(INFO_PLIST) makefile
OSAX_PATH      = ./src/osax
INFO_PLIST     = $(ASSET_PATH)/Info.plist
BINS           = $(BUILD_PATH)/yabai

.PHONY: all asan tsan install man icon archive publish sign clean-build clean
.NOTPARALLEL:

all: clean-build $(BINS)

asan: BUILD_FLAGS=-std=c11 -Wall -Wextra -g -O0 -fvisibility=hidden -fsanitize=address,undefined -mmacosx-version-min=11.0 -fno-objc-arc -arch x86_64 -arch arm64 -sectcreate __TEXT __info_plist $(INFO_PLIST)
asan: clean-build $(BINS)

tsan: BUILD_FLAGS=-std=c11 -Wall -Wextra -g -O0 -fvisibility=hidden -fsanitize=thread,undefined -mmacosx-version-min=11.0 -fno-objc-arc -arch x86_64 -arch arm64 -sectcreate __TEXT __info_plist $(INFO_PLIST)
tsan: clean-build $(BINS)

install: BUILD_FLAGS=-std=c11 -Wall -Wextra -DNDEBUG -O3 -fvisibility=hidden -mmacosx-version-min=11.0 -fno-objc-arc -arch x86_64 -arch arm64 -sectcreate __TEXT __info_plist $(INFO_PLIST)
install: clean-build $(BINS)

$(OSAX_SRC): makefile $(OSAX_PATH)/loader.m $(OSAX_PATH)/loader_shellcode.s $(OSAX_PATH)/loader_protocol.h $(OSAX_PATH)/payload.m $(OSAX_PATH)/common.h $(OSAX_PATH)/message_validation.h ./src/misc/hashtable.h ./src/misc/socket_io.h $(OSAX_PATH)/arm64_payload.m $(OSAX_PATH)/x64_payload.m
	xcrun clang $(OSAX_PATH)/payload.m -shared -fPIC -O3 -mmacosx-version-min=11.0 -arch x86_64 -arch arm64e -o $(OSAX_PATH)/payload $(FRAMEWORK_PATH) -framework SkyLight -framework Foundation -framework Carbon
	xcrun clang $(OSAX_PATH)/loader.m -x assembler-with-cpp $(OSAX_PATH)/loader_shellcode.s -O3 -mmacosx-version-min=11.0 -arch x86_64 -arch arm64e -o $(OSAX_PATH)/loader -framework Cocoa
	xxd -i -a $(OSAX_PATH)/payload $(OSAX_PATH)/payload_bin.c
	xxd -i -a $(OSAX_PATH)/loader $(OSAX_PATH)/loader_bin.c
	rm -f $(OSAX_PATH)/payload
	rm -f $(OSAX_PATH)/loader

man:
	asciidoctor -b manpage $(DOC_PATH)/yabai.asciidoc -o $(DOC_PATH)/yabai.1

icon:
	python3 $(SCRIPT_PATH)/seticon.py $(ASSET_PATH)/icon/2x/icon-512px@2x.png $(BUILD_PATH)/yabai

publish:
	@set -eu; \
	installer='$(SCRIPT_PATH)/install.sh'; \
	binary='$(BUILD_PATH)/yabai'; \
	[ -f "$$binary" ] && [ -x "$$binary" ]; \
	release=$$("$$binary" --version); \
	case "$$release" in yabai-v*) ;; *) exit 1 ;; esac; \
	version=$${release#yabai-v}; \
	[ -n "$$version" ]; \
	case "$$version" in *[!0-9A-Za-z._-]*) exit 1 ;; esac; \
	archive='$(BUILD_PATH)/'"$$release"'.tar.gz'; \
	[ -f "$$archive" ]; \
	hash=$$(shasum -a 256 "$$archive" | cut -d ' ' -f 1); \
	[ "$${#hash}" -eq 64 ]; \
	case "$$hash" in *[!0-9a-f]*) exit 1 ;; esac; \
	[ "$$(grep -c '^VERSION=' "$$installer")" -eq 1 ]; \
	[ "$$(grep -c '^EXPECTED_HASH=' "$$installer")" -eq 1 ]; \
	tmp=$$(mktemp '$(SCRIPT_PATH)/.install.sh.XXXXXX'); \
	trap 'rm -f "$$tmp"' 0 HUP INT TERM; \
	sed -e "s/^VERSION=.*/VERSION=\"$$version\"/" \
	    -e "s/^EXPECTED_HASH=.*/EXPECTED_HASH=\"$$hash\"/" \
	    "$$installer" > "$$tmp"; \
	chmod 0755 "$$tmp"; \
	mv -f "$$tmp" "$$installer"; \
	trap - 0 HUP INT TERM

archive: man install sign icon
	rm -rf $(ARCH_PATH)
	mkdir -p $(ARCH_PATH)
	cp -r $(BUILD_PATH) $(ARCH_PATH)/
	cp -r $(DOC_PATH) $(ARCH_PATH)/
	cp -r $(SMP_PATH) $(ARCH_PATH)/
	tar -cvzf $(BUILD_PATH)/$(shell $(BUILD_PATH)/yabai --version).tar.gz $(ARCH_PATH)
	rm -rf $(ARCH_PATH)

sign:
	codesign -fs "yabai-cert" $(BUILD_PATH)/yabai

clean-build:
	rm -rf $(BUILD_PATH)

clean: clean-build
	rm -f $(OSAX_SRC)

$(BUILD_PATH)/yabai: $(YABAI_DEPS)
	mkdir -p $(BUILD_PATH)
	xcrun clang $(YABAI_SRC) $(BUILD_FLAGS) $(CLI_FLAGS) $(FRAMEWORK_PATH) $(FRAMEWORK) -o $@

# Build an optimized, yabai-cert signed binary and install it to the
# root-owned /usr/local/bin so the sudoers rule can stay digest-free.
deploy: install sign
	sudo cp $(BUILD_PATH)/yabai /usr/local/bin/yabai
	sudo /usr/local/bin/yabai --uninstall-sa
	sudo /usr/local/bin/yabai --load-sa
	/usr/local/bin/yabai --restart-service
