#include "Application.h"
#include "logger/Log.h"
#include <thread>
#include <chrono>

Application::Application(int argc, char *argv[]) : pipewireContext(PipeWireContext()) {
    Log::info("Application started");
    pipewireContext.createChannel("goofy", "poop");
    std::this_thread::sleep_for(std::chrono::seconds(10));
}
