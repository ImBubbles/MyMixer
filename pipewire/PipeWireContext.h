#pragma once

#include <array>
#include <string>
#include <vector>

#include "VirtualChannel.h"

struct VirtualChannel;
struct StreamContext;
struct pw_proxy;
struct pw_core;
struct pw_context;
struct pw_registry;
struct pw_thread_loop;

struct HardwareAudioDevice {
    std::string name;
    std::string description;
    bool isInput = false;
    uint32_t nodeId = PW_ID_ANY;
    uint32_t leftPort = PW_ID_ANY;
    uint32_t rightPort = PW_ID_ANY;
    std::string leftPortName;
    std::string rightPortName;
};

struct AudioEndpoint {
    std::string name;
    bool isHardware = false;
};

struct AudioConnection {
    AudioEndpoint source;
    AudioEndpoint destination;
};

class PipeWireContext {
public:
    PipeWireContext();
    ~PipeWireContext();

    VirtualChannel* registerChannel(VirtualChannel* channel);
    VirtualChannel* createChannel(const std::string& name, const std::string& description);
    bool removeChannel(const std::string& name);
    bool linkPorts(uint32_t outputNode, uint32_t outputPort, uint32_t inputNode, uint32_t inputPort) const;
    bool linkPortsLR(uint32_t outputNode, uint32_t outputL, uint32_t outputR, uint32_t inputNode, uint32_t inputL, uint32_t inputR) const;
    bool linkStreams(const StreamContext* source, const StreamContext* sink) const;
    bool linkChannels(const VirtualChannel* source, const VirtualChannel* sink) const;
    bool setChannelConnection(const VirtualChannel* source, const VirtualChannel* destination, bool connected) const;
    bool setAudioConnection(const AudioEndpoint& source, const AudioEndpoint& destination, bool connected);
    bool createStereoLinks(uint32_t outputNode, const std::array<std::string, 2>& outputPorts,
                           uint32_t inputNode, const std::array<std::string, 2>& inputPorts,
                           std::array<pw_link*, 2>& links);
    std::vector<HardwareAudioDevice> getHardwareDevices() const;
    std::vector<AudioConnection> getHardwareConnections() const;

    void addHardwareNode(uint32_t id, const spa_dict* props);
    void addHardwarePort(uint32_t id, const spa_dict* props);
    void removeRegistryGlobal(uint32_t id);

    struct pw_thread_loop* loop = nullptr;
    struct pw_context* context = nullptr;
    struct pw_core* core = nullptr;
    struct pw_registry* registry = nullptr;
    struct spa_hook registry_listener = {nullptr};
    struct pw_registry_events registry_events;

    StreamContext* findStreamContext(uint32_t nodeId) const;

    std::vector<VirtualChannel*> getChannels() const;
private:
    friend struct ConfigHandler;

    struct ManagedHardwareConnection {
        AudioEndpoint source;
        AudioEndpoint destination;
        std::array<pw_link*, 2> links{};
    };

    std::vector<VirtualChannel*> virtualChannels;
    std::vector<HardwareAudioDevice> hardwareDevices;
    std::vector<ManagedHardwareConnection> hardwareConnections;
    // (removed debug proxy retention) 

    bool doesChannelExist(const std::string& name) const;
};