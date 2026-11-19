CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t build/tsoft
build/t: src/shadow.c tests/test_shadow.c src/shadow.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/shadow.c tests/test_shadow.c -lm -o $@
build/tsoft: src/shadow.c tests/test_shadow_soft.c src/shadow.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/shadow.c tests/test_shadow_soft.c -lm -o $@
build/bench: src/shadow.c tests/bench_soft.c src/shadow.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/shadow.c tests/bench_soft.c -lm -o $@
test: build/t build/tsoft
	./build/t
	./build/tsoft
bench: build/bench
	./build/bench
equiv:
	sh tests/hard_equivalence.sh
tool: build/app
build/app: src/shadow.c src/config.c src/state.c src/app_main.c
	@mkdir -p build out
	$(CC) $(CFLAGS) $^ -lm -o $@
demo: tool
	./build/app demo.cfg

clean:
	rm -rf build
.PHONY: all test bench equiv clean
