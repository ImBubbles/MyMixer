#pragma once

#include <vector>

#include "VirtualChannel.h"

struct VirtualChannel;
class PipeWireContext {
public:
    PipeWireContext();
    ~PipeWireContext();

    bool registerChannel(VirtualChannel* channel);
    bool createChannel(const std::string& name, const std::string& description);

    struct pw_thread_loop* loop = nullptr;
    struct pw_context* context = nullptr;
    struct pw_core* core = nullptr;
    struct pw_registry* registry = nullptr;
    struct spa_hook registry_listener = {nullptr};
    struct pw_registry_events registry_events;
private:

    std::vector<VirtualChannel*> virtualChannels;

    bool doesChannelExist(const std::string& name) const;
};