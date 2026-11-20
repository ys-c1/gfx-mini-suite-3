CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
all: build/t
build/t: src/path.cpp tests/test_path.cpp src/path.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/path.cpp tests/test_path.cpp -o $@
test: build/t
	./build/t
tool: build/app
build/app: src/path.cpp src/config.c src/state.c src/app_main.cpp
	@mkdir -p build out
	$(CXX) $(CXXFLAGS) $^ -o $@
demo: tool
	./build/app demo.cfg

report: build/report
build/report: tools/resample_report.cpp src/path.cpp src/config.c demo.cfg
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc tools/resample_report.cpp src/path.cpp src/config.c -o $@
	./build/report demo.cfg

clean:
	rm -rf build
.PHONY: all test clean report
