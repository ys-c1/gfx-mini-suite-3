CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t
build/t: src/shadow.c tests/test_shadow.c src/shadow.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/shadow.c tests/test_shadow.c -lm -o $@
test: build/t
	./build/t
clean:
	rm -rf build
.PHONY: all test clean
