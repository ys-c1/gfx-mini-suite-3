CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/t
build/t: src/tile.c tests/test_tile.c src/tile.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/tile.c tests/test_tile.c -o $@
test: build/t build/tslope build/tfric
	./build/t
	./build/tslope
	./build/tfric

# ---- slope layer, unchanged baseline --------------------------------------
slope-test: build/tslope
	./build/tslope
build/tslope: src/tile.c src/slope.c tests/test_slope.c src/tile.h src/slope.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/tile.c src/slope.c tests/test_slope.c -o $@ -lm
slope-demo: build/sdemo
	./build/sdemo
build/sdemo: src/tile.c src/slope.c src/slope_demo.c src/tile.h src/slope.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/tile.c src/slope.c src/slope_demo.c -o $@ -lm

# ---- friction layer -------------------------------------------------------
friction-test: build/tfric
	./build/tfric
build/tfric: src/tile.c src/slope.c src/slope_friction.c tests/test_friction.c \
             src/tile.h src/slope.h src/slope_friction.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/tile.c src/slope.c src/slope_friction.c \
	      tests/test_friction.c -o $@ -lm

build/freport: src/tile.c src/slope.c src/slope_friction.c tools/friction_report.c \
               src/tile.h src/slope.h src/slope_friction.h
	@mkdir -p build out
	$(CC) $(CFLAGS) src/tile.c src/slope.c src/slope_friction.c \
	      tools/friction_report.c -o $@ -lm

friction-report: build/freport
	./build/freport friction.cfg out/friction_trace.csv | tee out/friction_summary.csv

friction-plots: friction-report
	python3 tools/friction_plot.py out/friction_trace.csv out/friction_summary.csv out/friction_report.svg

report: friction-plots

tool: build/app
build/app: src/tile.c src/config.c src/state.c src/app_main.c
	@mkdir -p build out
	$(CC) $(CFLAGS) $^ -o $@
demo: tool
	./build/app demo.cfg

clean:
	rm -rf build
	rm -f out/friction_trace.csv out/friction_summary.csv out/friction_report.svg
.PHONY: all test slope-test slope-demo friction-test friction-report \
        friction-plots report clean
