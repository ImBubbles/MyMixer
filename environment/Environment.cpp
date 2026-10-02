//
// Created by bubbles on 8/4/25.
//

#include "Environment.h"
#include "generated/defaults/settings.h"
#include "generated/defaults/config.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

int createDir(const std::string& filePath, const std::string& dirName) {
    const std::filesystem::path childPath(dirName);
    if (dirName.empty() || childPath.has_parent_path() || childPath.filename() != childPath ||
        dirName == "." || dirName == "..") {
        Log::error("Invalid directory name: " + dirName);
        return -1;
    }

    std::error_code error;
    const std::filesystem::path parentPath = filePath.empty()
        ? std::filesystem::current_path(error)
        : std::filesystem::path(filePath);
    if (error) {
        Log::error("Failed to resolve current directory");
        return -1;
    }
    if (!std::filesystem::is_directory(parentPath, error) || error) {
        Log::error("Parent path is not an existing directory: " + parentPath.string());
        return -1;
    }

    const std::filesystem::path targetPath = parentPath / childPath;
    const bool targetExists = std::filesystem::exists(targetPath, error);
    if (error) {
        Log::error("Failed to inspect path: " + targetPath.string());
        return -1;
    }
    if (targetExists) {
        const bool targetIsDirectory = std::filesystem::is_directory(targetPath, error);
        if (error || !targetIsDirectory) {
            Log::error("Path exists but is not a directory: " + targetPath.string());
            return -1;
        }
        return 1;
    }

    if (std::filesystem::create_directory(targetPath, error)) {
        Log::debug("Created directory " + targetPath.string());
        return 0;
    }

    if (const bool targetIsDirectory = std::filesystem::is_directory(targetPath, error); !error && targetIsDirectory) {
        return 1;
    }

    Log::error("Failed to create directory " + targetPath.string());
    return -1;
}

int ensureExistence(const std::string& filePath) {
    Log::debug("Ensuring file "+filePath);
    if (std::filesystem::exists(filePath)) {
        return 1;
    }
    std::ofstream outfile(filePath);
    if (outfile.is_open()) {
        outfile.close();
        return 0;
    }
    outfile.close();
    return -1;
}

int ensureExistence(const std::string& filePath, const std::string& defaultValue) {
    const int result = ensureExistence(filePath);
    if (result!=0) {
        return result;
    }
    std::fstream file(filePath);
    file << defaultValue;
    file.close();
    return result;
}

int ensureFilePathExistence(const std::string& filePath) {
    if (filePath.empty()) {
        Log::error("Cannot ensure an empty directory path");
        return -1;
    }

    std::error_code error;
    const std::filesystem::path directoryPath =
        std::filesystem::absolute(std::filesystem::path(filePath), error).lexically_normal();
    if (error) {
        Log::error("Failed to resolve directory path: " + filePath);
        return -1;
    }

    std::filesystem::path currentPath = directoryPath.root_path();
    bool createdAny = false;
    for (const auto& component : directoryPath.relative_path()) {
        const int result = createDir(currentPath.string(), component.string());
        if (result < 0) {
            return -1;
        }
        createdAny = createdAny || result == 0;
        currentPath /= component;
    }

    return createdAny ? 0 : 1;
}

namespace Environment {
    Settings settings{};

    std::string readFile(const std::string& filePath) {
        Log::debug("Reading file "+filePath);
        std::ifstream file(filePath);
        if (!file.is_open()) {
            Log::debug("Could not find file: " + filePath);
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    void setupFileEnvironment() {

        Log::info("Setting up environment ...");

        std::filesystem::path configDir;
        if (userConfigDir) {
            configDir = std::filesystem::path(userConfigDir) / "MyMixer";
        } else {
            configDir = std::filesystem::path(std::getenv("HOME")) / ".config" / "MyMixer";
        }

        const std::filesystem::path settingsPath = configDir / "settings.json";
        const std::filesystem::path configPath = configDir / "config.json";
        if (ensureFilePathExistence(configDir.string()) < 0) {
            Log::error("Failed to create/find config directory: " + configDir.string());
            return;
        }
        if (ensureFilePathExistence(configDir.string()) == 0) {
            Log::info("Created config directory: " + configDir.string());
        }

        const std::string defaultSettings(
            reinterpret_cast<const char*>(resource_default_settings),
            resource_default_settings_len
        );
        const std::string defaultConfig(
            reinterpret_cast<const char*>(resource_default_config),
            resource_default_config_len
        );

        const int settingsCreate = ensureExistence(settingsPath.string(), defaultSettings);
        const int configCreate = ensureExistence(configPath.string(), defaultConfig);

        if (settingsCreate == 0) {
            try {
                std::ifstream settingsInput(settingsPath);
                nlohmann::json settings = nlohmann::json::parse(settingsInput);
                if (!settings.is_object()) {
                    throw std::runtime_error("Settings JSON root must be an object");
                }

                settings["defaultConfig"] = configPath.string();
                std::ofstream settingsOutput(settingsPath, std::ios::trunc);
                if (!settingsOutput.is_open()) {
                    throw std::runtime_error("Could not open settings file for update");
                }
                settingsOutput << settings.dump(2) << '\n';
                if (!settingsOutput) {
                    throw std::runtime_error("Failed to write updated settings file");
                }
            } catch (const std::exception& error) {
                Log::error("Failed to set default config path in settings: " + std::string(error.what()));
            }
        } else if (settingsCreate < 0) {
            Log::error("Failed to create settings file: " + settingsPath.string());
        }

        if (configCreate < 0) {
            Log::error("Failed to create config file: " + configPath.string());
        }

        try {
            std::ifstream settingsInput(settingsPath);
            if (!settingsInput.is_open()) {
                throw std::runtime_error("Could not open settings file for loading");
            }

            const nlohmann::json settingsJson = nlohmann::json::parse(settingsInput);
            Settings loadedSettings{
                .config = settingsJson.at("config").get<std::string>(),
                .logFilter = settingsJson.at("logFilter").get<int>(),
            };
            settings = std::move(loadedSettings);
        } catch (const std::exception& error) {
            Log::error("Failed to load settings file: " + std::string(error.what()));
        }

        Log::info("Environment setup complete");
    }
}