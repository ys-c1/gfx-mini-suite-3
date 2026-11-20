CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t build/t2
build/t: src/tile.c tests/test_tile.c src/tile.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/tile.c tests/test_tile.c -o $@
build/t2: src/step.c src/tile.c tests/test_step.c src/step.h src/tile.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/step.c src/tile.c tests/test_step.c -o $@
test: build/t build/t2
	./build/t
	./build/t2
tool: build/app
build/app: src/tile.c src/step.c src/config.c src/state.c src/app_main.c
	@mkdir -p build out
	$(CC) $(CFLAGS) $^ -o $@
demo: tool
	./build/app demo.cfg

clean:
	rm -rf build
.PHONY: all test clean
