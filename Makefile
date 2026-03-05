# Corpus — Image-driven sequencer for VCV Rack Pro 2
#
# Build:
#   make              → builds plugin
#   make install      → installs to VCV Rack plugins directory

# Point this at your Rack SDK
RACK_DIR ?= ../Rack-SDK

# Plugin source files
SOURCES += src/plugin.cpp
SOURCES += src/JamPG.cpp

# C++17 for structured bindings, std::optional, etc.
FLAGS += -std=c++17

# ─── DISTRIBUTABLES ───────────────────────────────────────────
DISTRIBUTABLES += res
DISTRIBUTABLES += LICENSE

# Include the Rack build system
include $(RACK_DIR)/plugin.mk
