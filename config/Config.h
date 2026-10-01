//
// Created by Hayden Holmes on 10/1/26.
//

#pragma once

#include "../pipewire/PipeWireContext.h"
#include "../pipewire/VirtualChannel.h"

struct VirtualChannelConfig;
struct ChannelConnectionConfig;

struct Config {
    std::vector<VirtualChannelConfig> channels; // Config channels, not hot
    std::vector<ChannelConnectionConfig> connections;
};

struct VirtualChannelConfig {
    const std::string name;
    const std::string description;
};
struct ChannelConnectionConfig {
    const std::string from;
    const std::string to;
};

struct ConfigHandler {
    static Config toConfig(PipeWireContext*);
    static std::string toJson(const Config&);
    static Config fromJson(const std::string&);
    static bool loadConfig(PipeWireContext*, Config);
private:
    static VirtualChannelConfig toVirtualChannelConfig(VirtualChannel* vc);
    static bool applyConfig(PipeWireContext*, const Config&);
};