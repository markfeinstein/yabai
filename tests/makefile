.PHONY: clean build run all opaque-context

all: run

clean:
	@rm -f ./bin/tests ./bin/menu_bar_ui ./bin/loader_protocol
	@rmdir ./bin 2>/dev/null || true

build: clean opaque-context
	mkdir -p ./bin
	clang ./src/tests.m -o ./bin/tests -DTESTS -DPROFILE=1 -F/System/Library/PrivateFrameworks -framework Carbon -framework Cocoa -framework CoreServices -framework CoreVideo -framework SkyLight
	clang ./src/menu_bar_ui.m ../src/menu_bar_model.c -o ./bin/menu_bar_ui -framework Cocoa
	clang ./src/loader_protocol.c -x assembler-with-cpp ../src/osax/loader_shellcode.s -Wall -Wextra -Werror -o ./bin/loader_protocol

opaque-context:
	@clang -fsyntax-only ./src/rule_fold_context_opaque.c
	@if clang -DFORGE_RULE_FOLD_CONTEXT -fsyntax-only ./src/rule_fold_context_opaque.c >/dev/null 2>&1; then echo "opaque rule fold context was forgeable"; exit 1; fi

run: build
	./bin/tests
	./bin/menu_bar_ui
	./bin/loader_protocol
	/bin/sh ./install.sh
	python3 ./seticon.py
	@phrase='one active reservation token spanning the queued event and its retry/backoff lifecycle'; \
		grep -Fq "$$phrase" ../doc/yabai.asciidoc; \
		grep -Fq "$$phrase" ../doc/yabai.1; \
		! grep -Fq 'event handler clears the reservation on entry' ../doc/yabai.asciidoc; \
		! grep -Fq 'event handler clears the reservation on entry' ../doc/yabai.1; \
		echo 'documentation reconciliation lifecycle parity passed'
