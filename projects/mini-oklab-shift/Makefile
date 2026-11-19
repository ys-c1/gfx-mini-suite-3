CC ?= gcc
PYTHON ?= python3
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t
build/t: src/oklab.c tests/test_oklab.c src/oklab.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/oklab.c tests/test_oklab.c -lm -o $@
test: build/t
	./build/t

demo: build/libok.so
	$(PYTHON) tools/demo.py

build/libok.so: src/oklab.c src/oklab.h
	@mkdir -p build
	$(CC) $(CFLAGS) -shared -fPIC src/oklab.c -lm -o $@

clean:
	rm -rf build
.PHONY: all test demo clean
