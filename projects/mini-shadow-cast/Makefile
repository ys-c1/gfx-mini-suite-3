CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t
build/t: src/shadow.c tests/test_shadow.c src/shadow.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/shadow.c tests/test_shadow.c -lm -o $@
test: build/t
	./build/t
tool: build/app
build/app: src/shadow.c src/config.c src/state.c src/app_main.c
	@mkdir -p build out
	$(CC) $(CFLAGS) $^ -lm -o $@
demo: tool
	./build/app demo.cfg

clean:
	rm -rf build
.PHONY: all test clean
