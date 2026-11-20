CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t
build/t: src/shadow.c src/state.c tests/test_shadow.c src/shadow.h src/state.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/shadow.c src/state.c tests/test_shadow.c -lm -o $@
test: build/t build/app
	./build/t
	bash tests/run_integration.sh
tool: build/app
build/app: src/shadow.c src/config.c src/state.c src/app_main.c
	@mkdir -p build out
	$(CC) $(CFLAGS) $^ -lm -o $@
demo: tool
	./build/app demo.cfg
bench: build/app
	bash tests/bench.sh
clean:
	rm -rf build
.PHONY: all test clean tool demo bench
