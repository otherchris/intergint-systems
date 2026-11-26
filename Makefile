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

# Panel sources in panels/ use editable <text>; this bakes text into paths in res/.
# Needs Python 3. Creates a local venv in dep/.venv on first run.
PANEL_VENV := dep/.venv
PANEL_SOURCES := $(wildcard panels/*.svg)

$(PANEL_VENV)/.installed: scripts/requirements.txt
	python3 -m venv $(PANEL_VENV)
	$(PANEL_VENV)/bin/pip install -q -r scripts/requirements.txt
	touch $@

.PHONY: panels
panels: $(PANEL_VENV)/.installed
	$(PANEL_VENV)/bin/python scripts/build-panels.py $(PANEL_SOURCES)
