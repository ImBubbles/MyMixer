#include "Application.h"
#include "logger/Log.h"
#include <thread>
#include <chrono>

#include "environment/Environment.h"
#include "config/Config.h"

// remove after testing
#include <iostream>

Application::Application(int argc, char *argv[]) : pipewireContext(PipeWireContext()) {
    Log::info("Application started");
    // Load file environment
    Environment::setupFileEnvironment();
    // Load config
    const std::string configPath = Environment::settings.config;
    const std::string configJson = Environment::readFile(configPath);
    const Config config = ConfigHandler::fromJson(configJson);
    if (ConfigHandler::loadConfig(&pipewireContext, config)) {
        Log::info("Configuration loaded");
    } else {
        Log::error("Failed to load configuration");
    }

    //VirtualChannel* master = pipewireContext.createChannel("master", "Master");
    //VirtualChannel* discord = pipewireContext.createChannel("discord", "Discord");
    //pipewireContext.linkChannels(discord, master);
    //pipewireContext.linkChannels("discord", "master");
    //pipewireContext.unlinkChannels("discord", "master");
    std::this_thread::sleep_for(std::chrono::seconds(10));
    //std::string s;
    //std::cin >> s;
}
