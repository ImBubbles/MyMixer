#pragma once

#include <vector>

#include "VirtualChannel.h"

class PipeWireContext {
public:
    PipeWireContext();
    ~PipeWireContext();
private:
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;

    std::vector<const VirtualChannel*> virtualChannels;
public:
    bool createChannel(const std::string& name, const std::string& description);
private:
    bool doesChannelExist(const std::string& name) const;
};
