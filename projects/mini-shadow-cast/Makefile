CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t
build/t: src/shadow.c tests/test_shadow.c src/shadow.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/shadow.c tests/test_shadow.c -lm -o $@
test: build/t
	./build/t
bench: build/bench
	./build/bench
converge: build/converge
	./build/converge
tool: build/app
build/converge: src/shadow.c tests/converge_shadow.c src/shadow.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/shadow.c tests/converge_shadow.c -lm -o $@
build/bench: src/shadow.c tests/bench_shadow.c src/shadow.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/shadow.c tests/bench_shadow.c -lm -o $@
build/app: src/shadow.c src/config.c src/state.c src/app_main.c
	@mkdir -p build out
	$(CC) $(CFLAGS) $^ -lm -o $@
demo: tool
	./build/app demo.cfg

clean:
	rm -rf build
.PHONY: all test bench converge clean tool demo
