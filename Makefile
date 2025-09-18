# Bird-Box Makefile
# Wrapper around ESP-IDF CMake/Ninja build system with Git versioning

# Paths
PROJECT_DIR := $(CURDIR)
BUILD_DIR := $(PROJECT_DIR)/build
VERSION_FILE := $(PROJECT_DIR)/version.h

# Toolchain setup
export PATH := $(IDF_PATH)/tools:$(PATH)

# Default serial port (override with `make flash PORT=/dev/ttyUSB1`)
PORT ?= /dev/ttyUSB0
BAUD ?= 115200

# ESP-IDF environment
export BATCH_BUILD=1

.PHONY: all menuconfig build flash monitor clean fullclean reconfigure version

all: build

# --- Versioning ---
$(VERSION_FILE):
	@echo "#define GIT_VERSION \"$(shell git describe --tags --always --dirty)\"" > $(VERSION_FILE)
	@echo "Generated version.h with GIT_VERSION=$(shell git describe --tags --always --dirty)"

version: $(VERSION_FILE)

# --- Build rules ---
menuconfig:
	idf.py menuconfig

build: $(VERSION_FILE)
	idf.py build

flash: build
	idf.py -p $(PORT) -b $(BAUD) flash

monitor:
	idf.py -p $(PORT) monitor

flash-monitor: build
	idf.py -p $(PORT) -b $(BAUD) flash monitor

clean:
	rm -rf $(BUILD_DIR)

fullclean:
	idf.py fullclean
	rm -f $(VERSION_FILE)

reconfigure:
	idf.py reconfigure
