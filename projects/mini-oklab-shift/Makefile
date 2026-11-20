CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t
build/t: src/oklab.c src/state.c tests/test_oklab.c src/oklab.h src/state.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/oklab.c src/state.c tests/test_oklab.c -lm -o $@
test: build/t
	./build/t
tool: build/app
build/app: src/oklab.c src/config.c src/state.c src/app_main.c
	@mkdir -p build out
	$(CC) $(CFLAGS) $^ -lm -o $@
demo: tool
	./build/app demo.cfg

clean:
	rm -rf build
.PHONY: all test clean
