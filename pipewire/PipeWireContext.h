#pragma once

#include <vector>

#include "VirtualChannel.h"

struct VirtualChannel;
struct StreamContext;
struct pw_proxy;
struct pw_core;
struct pw_context;
struct pw_registry;
struct pw_thread_loop;
class PipeWireContext {
public:
    PipeWireContext();
    ~PipeWireContext();

    VirtualChannel* registerChannel(VirtualChannel* channel);
    VirtualChannel* createChannel(const std::string& name, const std::string& description);
    bool linkPorts(uint32_t outputNode, uint32_t outputPort, uint32_t inputNode, uint32_t inputPort) const;
    bool linkPortsLR(uint32_t outputNode, uint32_t outputL, uint32_t outputR, uint32_t inputNode, uint32_t inputL, uint32_t inputR) const;
    bool linkStreams(const StreamContext* source, const StreamContext* sink) const;
    bool linkChannels(const VirtualChannel* source, const VirtualChannel* sink) const;

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

    std::vector<VirtualChannel*> virtualChannels;
    // (removed debug proxy retention) 

    bool doesChannelExist(const std::string& name) const;
    bool removeChannel(const std::string& name);
};