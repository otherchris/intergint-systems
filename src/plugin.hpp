#pragma once
#include <rack.hpp>

using namespace rack;

// Declared in plugin.cpp, set by init().
extern Plugin* pluginInstance;

// One extern per module; defined at the bottom of each module's .cpp.
extern Model* modelAttenuverter;
extern Model* modelPianoRoll;
