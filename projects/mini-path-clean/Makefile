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
report: build/report
build/report: src/path.cpp tools/round_report.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@
bitcheck: build/bitcheck
build/bitcheck: tools/bitcheck.cpp tools/ref/path_baseline.h src/path.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tools/bitcheck.cpp src/path.cpp -o $@
demo: tool
	./build/app demo.cfg
bench: report
	./build/report

clean:
	rm -rf build
.PHONY: all test tool report bitcheck demo bench clean
