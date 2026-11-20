PYTHON ?= python3
all:
	$(PYTHON) -m py_compile src/envelope.py src/config.py src/state.py src/reconstruction.py src/app.py
test: all
	$(PYTHON) tests/test_envelope.py
	$(PYTHON) tests/test_reconstruction.py
bench: all
	@mkdir -p out
	$(PYTHON) tests/bench_reconstruction.py
tool: all
	@mkdir -p out
demo: tool
	cd src && $(PYTHON) app.py ../demo.cfg
clean:
	rm -rf src/__pycache__ tests/__pycache__
.PHONY: all test bench tool demo clean
