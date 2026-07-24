#include "Application.h"
#include "logger/Log.h"
#include <thread>
#include <chrono>

Application::Application(int argc, char *argv[]) : pipewireContext(PipeWireContext()) {
    Log::info("Application started");
    pipewireContext.createChannel("master", "Master");
    pipewireContext.createChannel("discord", "Discord");
    //pipewireContext.linkChannels("discord", "master");
    std::this_thread::sleep_for(std::chrono::seconds(5));
    //pipewireContext.unlinkChannels("discord", "master");
    std::this_thread::sleep_for(std::chrono::seconds(5));
}
