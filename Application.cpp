#include "Application.h"
#include "logger/Log.h"
#include <thread>
#include <chrono>

// remove after testing
#include <iostream>

Application::Application(int argc, char *argv[]) : pipewireContext(PipeWireContext()) {
    Log::info("Application started");
    VirtualChannel* master = pipewireContext.createChannel("master", "Master");
    VirtualChannel* discord =pipewireContext.createChannel("discord", "Discord");
    pipewireContext.linkChannels(discord, master);
    //pipewireContext.linkChannels("discord", "master");
    //pipewireContext.unlinkChannels("discord", "master");
    std::this_thread::sleep_for(std::chrono::seconds(10));
    //std::string s;
    //std::cin >> s;
}
