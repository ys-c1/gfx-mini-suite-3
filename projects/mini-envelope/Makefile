PYTHON ?= python3
all:
	$(PYTHON) -m py_compile src/envelope.py src/config.py src/state.py src/app.py
test: all
	$(PYTHON) tests/test_envelope.py
tool: all
	@mkdir -p out
demo: tool
	cd src && $(PYTHON) app.py ../demo.cfg
clean:
	rm -rf src/__pycache__ tests/__pycache__
.PHONY: all test tool demo clean
