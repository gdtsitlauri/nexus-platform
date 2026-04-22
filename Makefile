BUILD_DIR ?= build

.PHONY: configure build test docs audit hdl clean

configure:
	cmake -S . -B $(BUILD_DIR) -G Ninja

build: configure
	cmake --build $(BUILD_DIR) --parallel 2

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure -j1

hdl:
	./scripts/test_hdl.sh all

docs: configure
	cmake --build $(BUILD_DIR) --target docs

audit:
	python3 scripts/audit_phase1.py .

clean:
	rm -rf $(BUILD_DIR)
