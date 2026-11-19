CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t
build/t: src/oklab.c tests/test_oklab.c src/oklab.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/oklab.c tests/test_oklab.c -lm -o $@
test: build/t
	./build/t
clean:
	rm -rf build
.PHONY: all test clean
