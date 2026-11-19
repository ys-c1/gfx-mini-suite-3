CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t
build/t: src/tile.c tests/test_tile.c src/tile.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/tile.c tests/test_tile.c -o $@
test: build/t
	./build/t
clean:
	rm -rf build
.PHONY: all test clean
