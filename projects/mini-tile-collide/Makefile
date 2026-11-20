CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t build/td
build/t: src/tile.c tests/test_tile.c src/tile.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/tile.c tests/test_tile.c -o $@
build/td: src/tile.c src/door.c tests/test_door.c src/tile.h src/door.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/tile.c src/door.c tests/test_door.c -o $@
build/bench: src/tile.c src/door.c tests/bench_door.c src/tile.h src/door.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/tile.c src/door.c tests/bench_door.c -o $@
test: build/t build/td
	./build/t
	./build/td
bench: build/bench
	./build/bench
tool: build/app
build/app: src/tile.c src/config.c src/state.c src/app_main.c
	@mkdir -p build out
	$(CC) $(CFLAGS) $^ -o $@
demo: tool
	./build/app demo.cfg

clean:
	rm -rf build
.PHONY: all test bench clean
