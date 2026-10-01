//
// Created by bubbles on 8/4/25.
//

#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include "../logger/Log.h"

static bool isEnvironmentSetup = false;

void setupFileEnvironment();
std::string readFile(std::string& filePath);
struct HTTPServerConfig* loadServerConfig();