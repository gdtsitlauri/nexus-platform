BUILD_DIR ?= build

.PHONY: configure build test hdl flow clean

configure:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build $(BUILD_DIR) --parallel 2

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure -j1

hdl:
	python3 scripts/test_hdl.py all

flow: build
	python3 scripts/nexus_flow.py $(BUILD_DIR)/bin

clean:
	rm -rf $(BUILD_DIR)
