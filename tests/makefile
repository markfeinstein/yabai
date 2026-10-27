.PHONY: all clean build core-build run run-asan run-tsan benchmark-ax benchmark-ws-snapshot benchmark-space-inventory benchmark-window-query test-all opaque-context repository-build runtime-paths macho-parser syntax arch-arm64 arch-x86_64 shell-fixtures integration-hide-on-close

all: run

CONFIG ?= ordinary
ARCH ?=
CC ?= clang
ARCH_FLAG = $(if $(ARCH),-arch $(ARCH),)
BASE_FLAGS = $(ARCH_FLAG) -DTESTS -DPROFILE=1 -std=c11 -Wall -Wextra
FRAMEWORKS = -F/System/Library/PrivateFrameworks -framework Carbon -framework Cocoa -framework ColorSync -framework CoreServices -framework CoreVideo -framework SkyLight
OUT = ./bin/$(CONFIG)$(if $(ARCH),-$(ARCH),)
CORE = $(OUT)/tests
MODEL = $(OUT)/menu_bar_model.o
CORE_DEPS = $(wildcard ./src/*.c ./src/*.m ../src/*.c ../src/*.h ../src/*.m ../src/misc/*.c ../src/misc/*.h ../src/misc/*.m ../src/osax/*.h)
SANITIZERS_ordinary =
SANITIZERS_asan = -fsanitize=address,undefined
SANITIZERS_tsan = -fsanitize=thread,undefined
SANITIZERS = $(SANITIZERS_$(CONFIG))

clean:
	@if [ -d ./bin ] && [ ! -L ./bin ]; then find ./bin -mindepth 1 -delete; rmdir ./bin; elif [ -e ./bin ] || [ -L ./bin ]; then echo "refusing to clean unsafe tests/bin" >&2; exit 1; fi

$(OUT):
	@mkdir -p '$@'

$(MODEL): ../src/menu_bar_model.c ../src/menu_bar_model.h | $(OUT)
	$(CC) -c ../src/menu_bar_model.c -o '$@' $(ARCH_FLAG) $(SANITIZERS) -std=c11 -Wall -Wextra -Werror -MMD -MP

$(CORE): ./src/tests.m $(CORE_DEPS) $(MODEL) | $(OUT)
	$(CC) ./src/tests.m $(MODEL) -o '$@' $(BASE_FLAGS) $(SANITIZERS) $(FRAMEWORKS)

core-build: $(CORE)

opaque-context:
	@$(CC) -fsyntax-only ./src/rule_fold_context_opaque.c
	@if $(CC) -DFORGE_RULE_FOLD_CONTEXT -fsyntax-only ./src/rule_fold_context_opaque.c >/dev/null 2>&1; then echo "opaque rule fold context was forgeable"; exit 1; fi

build: core-build opaque-context
	$(CC) ./src/runtime_paths.c -o $(OUT)/runtime_paths $(ARCH_FLAG) -DTESTS -std=c11 -Wall -Wextra -Werror
	$(CC) ./src/menu_bar_ui.m ../src/menu_bar_model.c -o $(OUT)/menu_bar_ui $(ARCH_FLAG) -framework Cocoa
	$(CC) ./src/loader_protocol.c -x assembler-with-cpp ../src/osax/loader_shellcode.s $(ARCH_FLAG) -Wall -Wextra -Werror -o $(OUT)/loader_protocol
	$(CC) ./src/osax_payload.m -o $(OUT)/osax_payload $(ARCH_FLAG) -DPROFILE=1 -Wl,-undefined,dynamic_lookup -F/System/Library/PrivateFrameworks -framework Carbon -framework ColorSync -framework Foundation -framework SkyLight
	$(CC) ./src/macho_parser.c ../scripts/macho_parser.c ../scripts/scan_sa_core.c -o $(OUT)/macho_parser $(ARCH_FLAG) -DMACHO_PARSER_TESTING -std=c11 -Wall -Wextra -Werror
	$(CC) -fobjc-arc ../scripts/scan-sa-patterns.m ../scripts/macho_parser.c ../scripts/scan_sa_core.c $(ARCH_FLAG) -framework Foundation -o $(OUT)/scan_sa_patterns -Wall -Wextra -Werror -Wno-unused-function
	$(CC) -c ../scripts/macho_parser.c -o $(OUT)/macho_parser_production.o $(ARCH_FLAG) -std=c11 -Wall -Wextra -Werror -MMD -MP
	@if nm -g $(OUT)/macho_parser_production.o | grep -q 'macho_test_'; then echo "production parser exports test seam"; exit 1; fi
	@if $(CC) ./src/runtime_paths_no_nofollow.c -o $(OUT)/runtime_paths_no_nofollow $(ARCH_FLAG) -std=c11 >/dev/null 2>&1; then echo "runtime paths compiled without O_NOFOLLOW"; rm -f $(OUT)/runtime_paths_no_nofollow; exit 1; fi

repository-build: ./repository_build_fixture.sh ./repository_build.sh
	/bin/sh ./repository_build_fixture.sh

shell-fixtures:
	./integration_hide_on_close_fixture.sh
	./man_check_fixture.sh

run: build repository-build shell-fixtures
	$(CORE)
	$(OUT)/runtime_paths
	$(OUT)/menu_bar_ui
	$(OUT)/loader_protocol
	$(OUT)/osax_payload
	$(OUT)/macho_parser
	$(OUT)/macho_parser --write-scanner-fixture $(OUT)/scanner_fixture
	@if $(OUT)/scan_sa_patterns $(OUT)/scanner_fixture >/dev/null 2>&1; then echo "scanner accepted missing required checks"; exit 1; fi
	$(OUT)/macho_parser --write-success-scanner-fixture $(OUT)/scanner_success_fixture
	$(OUT)/scan_sa_patterns $(OUT)/scanner_success_fixture >/dev/null
	/bin/sh ./install.sh
	python3 ./seticon.py

benchmark-ax: core-build
	$(CORE) --benchmark-window-ax-frame

benchmark-ws-snapshot: core-build
	$(CORE) --benchmark-window-server-snapshot

benchmark-space-inventory: core-build
	$(CORE) --benchmark-space-window-inventory

benchmark-window-query: core-build
	$(CORE) --benchmark-window-query-snapshot

run-asan:
	$(MAKE) CONFIG=asan sanitizer-run asan-focused-run

run-tsan:
	$(MAKE) CONFIG=tsan sanitizer-run

sanitizer-run: core-build
	$(CC) ./src/runtime_paths.c -o $(OUT)/runtime_paths $(ARCH_FLAG) -DTESTS -std=c11 -Wall -Wextra -Werror $(SANITIZERS)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 TSAN_OPTIONS=halt_on_error=1 $(CORE)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 TSAN_OPTIONS=halt_on_error=1 $(OUT)/runtime_paths

asan-focused-run: | $(OUT)
	$(CC) ./src/osax_payload.m -o $(OUT)/osax_payload $(ARCH_FLAG) -DPROFILE=1 -fsanitize=address,undefined -Wl,-undefined,dynamic_lookup -F/System/Library/PrivateFrameworks -framework Carbon -framework ColorSync -framework Foundation -framework SkyLight
	$(CC) ./src/macho_parser.c ../scripts/macho_parser.c ../scripts/scan_sa_core.c -o $(OUT)/macho_parser $(ARCH_FLAG) -DMACHO_PARSER_TESTING -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(OUT)/osax_payload
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(OUT)/macho_parser

runtime-paths: build
	$(OUT)/runtime_paths

macho-parser: build
	$(OUT)/macho_parser

syntax:
	$(CC) -fsyntax-only ../src/menu_bar_model.c -std=c11 -Wall -Wextra -Werror
	$(CC) -fsyntax-only ../scripts/macho_parser.c -std=c11 -Wall -Wextra -Werror
	$(CC) -fsyntax-only ../scripts/scan_sa_core.c -std=c11 -Wall -Wextra -Werror
	$(CC) -fsyntax-only ./src/public_header_compile.c -std=c11 -Wall -Wextra -Werror

arch-arm64:
	$(MAKE) CONFIG=arch ARCH=arm64 core-build
	$(CC) ./src/osax_payload.m -o ./bin/arch-arm64/osax_payload -arch arm64 -DPROFILE=1 -Wl,-undefined,dynamic_lookup -F/System/Library/PrivateFrameworks -framework Carbon -framework ColorSync -framework Foundation -framework SkyLight
	@lipo ./bin/arch-arm64/tests -verify_arch arm64
	@lipo ./bin/arch-arm64/osax_payload -verify_arch arm64
	@if [ "$$(uname -m)" = arm64 ]; then ./bin/arch-arm64/tests && ./bin/arch-arm64/osax_payload; else echo "SKIP arm64 execution: host is not arm64"; fi

arch-x86_64:
	$(MAKE) CONFIG=arch ARCH=x86_64 core-build
	$(CC) ./src/osax_payload.m -o ./bin/arch-x86_64/osax_payload -arch x86_64 -DPROFILE=1 -Wl,-undefined,dynamic_lookup -F/System/Library/PrivateFrameworks -framework Carbon -framework ColorSync -framework Foundation -framework SkyLight
	@lipo ./bin/arch-x86_64/tests -verify_arch x86_64
	@lipo ./bin/arch-x86_64/osax_payload -verify_arch x86_64
	@if [ "$$(uname -m)" = x86_64 ]; then ./bin/arch-x86_64/tests && ./bin/arch-x86_64/osax_payload; elif /usr/bin/arch -x86_64 /usr/bin/true >/dev/null 2>&1; then /usr/bin/arch -x86_64 ./bin/arch-x86_64/tests && /usr/bin/arch -x86_64 ./bin/arch-x86_64/osax_payload; else echo "SKIP x86_64 execution: Rosetta is unavailable"; fi

integration-hide-on-close:
	./integration/hide_on_close.sh

test-all: run run-asan run-tsan arch-arm64 arch-x86_64 syntax

-include $(MODEL:.o=.d) $(OUT)/macho_parser_production.d
