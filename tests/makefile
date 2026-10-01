.PHONY: clean build run all

all: run

clean:
	@rm -f ./bin/tests ./bin/menu_bar_ui ./bin/loader_protocol
	@rmdir ./bin 2>/dev/null || true

build: clean
	mkdir -p ./bin
	clang ./src/tests.m -o ./bin/tests -DTESTS -DPROFILE=1 -F/System/Library/PrivateFrameworks -framework Carbon -framework Cocoa -framework CoreServices -framework CoreVideo -framework SkyLight
	clang ./src/menu_bar_ui.m ../src/menu_bar_model.c -o ./bin/menu_bar_ui -framework Cocoa
	clang ./src/loader_protocol.c -x assembler-with-cpp ../src/osax/loader_shellcode.s -Wall -Wextra -Werror -o ./bin/loader_protocol

run: build
	./bin/tests
	./bin/menu_bar_ui
	./bin/loader_protocol
	/bin/sh ./install.sh
