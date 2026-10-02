#pragma once

#include "pipewire/PipeWireContext.h"

class QApplication;

class Application {
public:
    Application();
    ~Application();

    int runGui(QApplication& application);
    int runHeadless();

private:
    PipeWireContext pipeWireContext;
};