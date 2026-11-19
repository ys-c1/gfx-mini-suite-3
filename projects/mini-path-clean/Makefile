CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
all: build/t build/report
build/t: src/path.cpp tests/test_path.cpp tests/ref_baseline.h src/path.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/path.cpp tests/test_path.cpp -o $@
build/report: src/path.cpp tests/report_isect.cpp tests/ref_baseline.h src/path.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/path.cpp tests/report_isect.cpp -o $@
test: build/t
	./build/t
report: build/report
	./build/report
check: build/t build/report
	./build/t
	python3 -I tests/compare_three.py build/report
clean:
	rm -rf build
.PHONY: all test report check clean
