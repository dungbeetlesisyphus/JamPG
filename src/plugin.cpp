#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
    pluginInstance = p;

    // IMAGE collection (pixel-driven)
    p->addModel(modelJamPG);
}
