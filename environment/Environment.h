//
// Created by bubbles on 8/4/25.
//

#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include "../logger/Log.h"

struct Settings {
    std::string config;
    int logFilter;
};

namespace Environment {
    static bool isEnvironmentSetup = false;
    extern Settings settings;
    static const char* userConfigDir = std::getenv("XDG_CONFIG_HOME");

    void setupFileEnvironment();
    std::string readFile(const std::string& filePath);
    bool isAutostartEnabled();
    bool setAutostart(bool enabled, const std::string& executablePath);
    bool setDefaultConfigPath(const std::string& path);
};