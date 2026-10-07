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

    static std::filesystem::path autostartFilePath() {
        const char* home = std::getenv("HOME");
        std::filesystem::path base = userConfigDir ? std::filesystem::path(userConfigDir)
            : std::filesystem::path(home ? home : "") / ".config";
        return base / "autostart" / "mymixer.desktop";
    }

    bool isAutostartEnabled() {
        std::error_code error;
        return std::filesystem::exists(autostartFilePath(), error) && !error;
    }

    bool setAutostart(const bool enabled, const std::string& executablePath) {
        const std::filesystem::path path = autostartFilePath();
        std::error_code error;
        if (!enabled) {
            std::filesystem::remove(path, error);
            return !error;
        }
        if (executablePath.empty() || ensureFilePathExistence(path.parent_path().string()) < 0) {
            return false;
        }
        std::ofstream output(path, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }
        // Exec values with spaces must be double-quoted per the desktop entry spec.
        output << "[Desktop Entry]\nType=Application\nName=MyMixer\nExec=\"" << executablePath << "\"\n";
        return static_cast<bool>(output);
    }

    bool setDefaultConfigPath(const std::string& path) {
        const char* home = std::getenv("HOME");
        const std::filesystem::path configDir = (userConfigDir ? std::filesystem::path(userConfigDir)
            : std::filesystem::path(home ? home : "") / ".config") / "MyMixer";
        const std::filesystem::path settingsPath = configDir / "settings.json";
        try {
            nlohmann::json settingsJson;
            {
                std::ifstream input(settingsPath);
                if (!input.is_open()) {
                    return false;
                }
                settingsJson = nlohmann::json::parse(input);
            }
            const std::string absolute = std::filesystem::absolute(path).lexically_normal().string();
            settingsJson["config"] = absolute;
            std::ofstream output(settingsPath, std::ios::trunc);
            output << settingsJson.dump(2) << '\n';
            if (!output) {
                return false;
            }
            settings.config = absolute;
            return true;
        } catch (const std::exception& error) {
            Log::error("Failed to update default config: " + std::string(error.what()));
            return false;
        }
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

        if (settingsCreate < 0) {
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

            nlohmann::json settingsJson = nlohmann::json::parse(settingsInput);
            if (!settingsJson.is_object()) {
                throw std::runtime_error("Settings JSON root must be an object");
            }

            std::string configValue;
            if (settingsJson.contains("config") && settingsJson["config"].is_string()) {
                configValue = settingsJson["config"].get<std::string>();
            }
            if (configValue.empty() || configValue == "changeme") {
                if (settingsJson.contains("defaultConfig") && settingsJson["defaultConfig"].is_string()) {
                    configValue = settingsJson["defaultConfig"].get<std::string>();
                }
                if (configValue.empty() || configValue == "changeme") {
                    configValue = configPath.string();
                }
                settingsJson["config"] = configValue;

                std::ofstream settingsOutput(settingsPath, std::ios::trunc);
                if (!settingsOutput.is_open()) {
                    throw std::runtime_error("Could not open settings file for migration");
                }
                settingsOutput << settingsJson.dump(2) << '\n';
                if (!settingsOutput) {
                    throw std::runtime_error("Failed to write migrated settings file");
                }
            }

            std::filesystem::path resolvedConfigPath(configValue);
            if (resolvedConfigPath.is_relative()) {
                resolvedConfigPath = configDir / resolvedConfigPath;
            }
            Settings loadedSettings{
                .config = resolvedConfigPath.lexically_normal().string(),
                .logFilter = settingsJson.value("logFilter", 0),
            };
            settings = std::move(loadedSettings);
        } catch (const std::exception& error) {
            Log::error("Failed to load settings file: " + std::string(error.what()));
        }

        Log::info("Environment setup complete");
    }
}