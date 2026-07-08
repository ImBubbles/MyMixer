#pragma once
#include "pipewire/PipeWireContext.h"

class Application {
public:
    Application(int argc, char *argv[]);
private:
    PipeWireContext pipewireContext;
};
