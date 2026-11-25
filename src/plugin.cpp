#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;

	p->addModel(modelAttenuverter);
	// Add new modules here (and in plugin.json).
}
