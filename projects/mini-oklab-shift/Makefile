CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
OBJS = build/oklab.o
all: build/t build/t_path
build/oklab.o: src/oklab.c src/oklab.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/oklab.c -o $@
build/t: src/oklab.c tests/test_oklab.c src/oklab.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/oklab.c tests/test_oklab.c -lm -o $@
build/t_path: $(OBJS) tests/test_oklab_path.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(OBJS) tests/test_oklab_path.c -lm -o $@
test: build/t build/t_path
	./build/t
	./build/t_path
clean:
	rm -rf build
.PHONY: all test clean
