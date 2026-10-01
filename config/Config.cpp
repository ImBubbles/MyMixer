//
// Created by Hayden Holmes on 10/1/26.
//

#include "Config.h"

#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "../logger/Log.h"

namespace {
bool isValidConfig(const Config& config) {
    std::map<std::string, std::string> channelNamesByNodeName;
    std::set<std::string> channelNames;

    for (const VirtualChannelConfig& channel : config.channels) {
        if (channel.name.empty()) {
            Log::error("Config contains a channel with an empty name");
            return false;
        }

        const std::string nodeName = UtilString::asLowercase(channel.name);
        if (!channelNames.insert(channel.name).second || channelNamesByNodeName.contains(nodeName)) {
            Log::error("Config contains duplicate channel names that map to PipeWire node \"" + nodeName + "\"");
            return false;
        }
        channelNamesByNodeName.emplace(nodeName, channel.name);
    }

    std::set<std::pair<std::string, std::string>> connections;
    for (const ChannelConnectionConfig& connection : config.connections) {
        if (channelNames.find(connection.from) == channelNames.end() ||
            channelNames.find(connection.to) == channelNames.end()) {
            Log::error("Config connection references a channel that is not defined: \"" + connection.from + "\" -> \"" + connection.to + "\"");
            return false;
        }
        if (!connections.emplace(connection.from, connection.to).second) {
            Log::error("Config contains duplicate connection: \"" + connection.from + "\" -> \"" + connection.to + "\"");
            return false;
        }
    }

    return true;
}
}

Config ConfigHandler::toConfig(PipeWireContext* pwc) {
    if (pwc == nullptr) {
        return {};
    }

    const std::vector<VirtualChannel*> channels = pwc->getChannels();
    std::vector<VirtualChannelConfig> configChannels;
    std::vector<ChannelConnectionConfig> configConnections;
    for (VirtualChannel* vc : channels) {
        if (vc == nullptr) {
            continue;
        }
        configChannels.push_back(toVirtualChannelConfig(vc));
        for (const auto& outputLink : vc->outputLinks) {
            if (outputLink == nullptr) {
                continue;
            }
            configConnections.push_back({
                .from = outputLink->from,
                .to = outputLink->to,
            });
        }
    }

    return {
        .channels = configChannels,
        .connections = configConnections
    };
}

std::string ConfigHandler::toJson(const Config& config) {
    nlohmann::json json;
    json["channels"] = nlohmann::json::array();
    json["connections"] = nlohmann::json::array();

    for (const VirtualChannelConfig& channel : config.channels) {
        json["channels"].push_back({
            {"name", channel.name},
            {"description", channel.description},
        });
    }

    for (const ChannelConnectionConfig& connection : config.connections) {
        json["connections"].push_back({
            {"from", connection.from},
            {"to", connection.to},
        });
    }

    return json.dump(2);
}

Config ConfigHandler::fromJson(const std::string& jsonText) {
    using Json = nlohmann::json;

    std::vector<std::set<std::string>> objectKeys;
    const auto duplicateKeyCallback = [&objectKeys](int, Json::parse_event_t event, Json& parsed) {
        if (event == Json::parse_event_t::object_start) {
            objectKeys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            if (objectKeys.empty() || !objectKeys.back().insert(parsed.get<std::string>()).second) {
                throw std::invalid_argument("Config JSON contains a duplicate object key");
            }
        } else if (event == Json::parse_event_t::object_end) {
            if (!objectKeys.empty()) {
                objectKeys.pop_back();
            }
        }
        return true;
    };

    Json json;
    try {
        json = Json::parse(jsonText, duplicateKeyCallback, true, false);
    } catch (const Json::exception& error) {
        throw std::invalid_argument("Invalid config JSON: " + std::string(error.what()));
    }

    const auto requireObjectKeys = [](const Json& value, const std::set<std::string>& expected, const std::string& path) {
        if (!value.is_object()) {
            throw std::invalid_argument(path + " must be a JSON object");
        }

        std::set<std::string> actual;
        for (auto field = value.begin(); field != value.end(); ++field) {
            actual.insert(field.key());
        }
        if (actual != expected) {
            throw std::invalid_argument(path + " has missing or unknown fields");
        }
    };
    const auto requireString = [](const Json& value, const std::string& path) -> const std::string& {
        if (!value.is_string()) {
            throw std::invalid_argument(path + " must be a string");
        }
        return value.get_ref<const std::string&>();
    };

    requireObjectKeys(json, {"channels", "connections"}, "Config root");
    if (!json["channels"].is_array() || !json["connections"].is_array()) {
        throw std::invalid_argument("Config channels and connections must be arrays");
    }

    std::vector<VirtualChannelConfig> channels;
    for (std::size_t index = 0; index < json["channels"].size(); ++index) {
        const Json& channel = json["channels"][index];
        const std::string path = "channels[" + std::to_string(index) + "]";
        requireObjectKeys(channel, {"name", "description"}, path);
        channels.push_back({
            .name = requireString(channel["name"], path + ".name"),
            .description = requireString(channel["description"], path + ".description"),
        });
    }

    std::vector<ChannelConnectionConfig> connections;
    for (std::size_t index = 0; index < json["connections"].size(); ++index) {
        const Json& connection = json["connections"][index];
        const std::string path = "connections[" + std::to_string(index) + "]";
        requireObjectKeys(connection, {"from", "to"}, path);
        connections.push_back({
            .from = requireString(connection["from"], path + ".from"),
            .to = requireString(connection["to"], path + ".to"),
        });
    }

    Config config{
        .channels = std::move(channels),
        .connections = std::move(connections),
    };
    if (!isValidConfig(config)) {
        throw std::invalid_argument("Config JSON contains invalid channel or connection data");
    }
    return config;
}

VirtualChannelConfig ConfigHandler::toVirtualChannelConfig(VirtualChannel* vc) {
    return {
        .name = vc->name,
        .description = vc->description,
    };
}

bool ConfigHandler::applyConfig(PipeWireContext* pwc, const Config& config) {
    if (pwc == nullptr || !isValidConfig(config)) {
        return false;
    }

    std::map<std::string, const VirtualChannelConfig*> desiredChannels;
    for (const VirtualChannelConfig& channel : config.channels) {
        desiredChannels.emplace(channel.name, &channel);
    }

    const std::vector<VirtualChannel*> currentChannels = pwc->getChannels();
    for (VirtualChannel* channel : currentChannels) {
        if (channel != nullptr) {
            channel->clearConnections();
        }
    }

    for (VirtualChannel* channel : currentChannels) {
        if (channel == nullptr) {
            continue;
        }
        const auto desired = desiredChannels.find(channel->name);
        if (desired == desiredChannels.end() || desired->second->description != channel->description) {
            if (!pwc->removeChannel(channel->name)) {
                Log::error("Failed to remove channel \"" + channel->name + "\" during config reconciliation");
                return false;
            }
        }
    }

    std::map<std::string, VirtualChannel*> channelsByName;
    for (VirtualChannel* channel : pwc->getChannels()) {
        if (channel != nullptr) {
            channelsByName.emplace(channel->name, channel);
        }
    }

    for (const VirtualChannelConfig& channelConfig : config.channels) {
        if (channelsByName.find(channelConfig.name) != channelsByName.end()) {
            continue;
        }

        VirtualChannel* channel = pwc->createChannel(channelConfig.name, channelConfig.description);
        if (channel == nullptr) {
            Log::error("Failed to create configured channel \"" + channelConfig.name + "\"");
            return false;
        }
        channelsByName.emplace(channelConfig.name, channel);
    }

    for (const ChannelConnectionConfig& connection : config.connections) {
        const auto from = channelsByName.find(connection.from);
        const auto to = channelsByName.find(connection.to);
        if (from == channelsByName.end() || to == channelsByName.end() ||
            !pwc->linkChannels(from->second, to->second)) {
            Log::error("Failed to create configured connection \"" + connection.from + "\" -> \"" + connection.to + "\"");
            return false;
        }
    }

    return true;
}

bool ConfigHandler::loadConfig(PipeWireContext* pwc, Config config) {
    if (pwc == nullptr || !isValidConfig(config)) {
        return false;
    }

    const Config previousConfig = toConfig(pwc);
    if (applyConfig(pwc, config)) {
        return true;
    }

    Log::warning("Failed to apply config; attempting to restore the previous config");
    if (!applyConfig(pwc, previousConfig)) {
        Log::error("Failed to restore the previous config after config load failure");
    }
    return false;
}