CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
all: build/t
build/t: src/path.cpp tests/test_path.cpp src/path.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/path.cpp tests/test_path.cpp -o $@
test: build/t
	./build/t
clean:
	rm -rf build
.PHONY: all test clean
