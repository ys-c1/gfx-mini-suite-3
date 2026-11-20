CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t
build/t: src/oklab.c src/adapt.c tests/test_oklab.c src/oklab.h src/adapt.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/oklab.c src/adapt.c tests/test_oklab.c -lm -o $@
build/tcfg: src/config.c tests/test_config.c src/config.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/config.c tests/test_config.c -o $@
test: build/t build/tcfg
	./build/t
	./build/tcfg
bench: build/bench
	@mkdir -p out
	./build/bench .
build/bench: src/oklab.c src/adapt.c src/config.c tests/bench_adapt.c src/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/oklab.c src/adapt.c src/config.c tests/bench_adapt.c -lm -o $@
tool: build/app
build/app: src/oklab.c src/adapt.c src/config.c src/state.c src/app_main.c src/*.h
	@mkdir -p build out
	$(CC) $(CFLAGS) src/oklab.c src/adapt.c src/config.c src/state.c src/app_main.c -lm -o $@
demo: tool
	./build/app demo.cfg

clean:
	rm -rf build
.PHONY: all test bench tool clean demo
