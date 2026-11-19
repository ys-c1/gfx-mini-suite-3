PYTHON ?= python3
all:
	$(PYTHON) -m py_compile src/envelope.py
test: all
	$(PYTHON) tests/test_envelope.py
clean:
	rm -rf src/__pycache__ tests/__pycache__
.PHONY: all test clean
