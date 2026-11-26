#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;

	p->addModel(modelAttenuverter);
	p->addModel(modelPianoRoll);
	// Add new modules here (and in plugin.json).
}
