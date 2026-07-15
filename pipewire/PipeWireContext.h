#pragma once

#include <vector>

#include "VirtualChannel.h"

class PipeWireContext {
public:
    PipeWireContext();
    ~PipeWireContext();

    bool createChannel(const std::string& name, const std::string& description);
private:
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;

    std::vector<VirtualChannel*> virtualChannels;

    bool doesChannelExist(const std::string& name) const;
};
