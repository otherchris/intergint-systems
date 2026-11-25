# Path to the Rack SDK. Defaults to the copy fetched by scripts/fetch-sdk.sh.
# Override with: make RACK_DIR=/path/to/Rack-SDK
RACK_DIR ?= $(CURDIR)/dep/Rack-SDK

FLAGS +=
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

ifeq ($(wildcard $(RACK_DIR)/plugin.mk),)
$(error Rack SDK not found at $(RACK_DIR). Run ./scripts/fetch-sdk.sh or set RACK_DIR)
endif

include $(RACK_DIR)/plugin.mk
