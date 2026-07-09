#pragma once

#include <vector>

#include "VirtualChannel.h"

class PipeWireContext {
public:
    PipeWireContext();
    ~PipeWireContext();

    bool createChannel(const std::string& name, const std::string& description);
    bool linkChannels(const std::string& from, const std::string& to);
    bool unlinkChannels(const std::string& from, const std::string& to);
private:
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;

    std::vector<VirtualChannel*> virtualChannels;

    bool doesChannelExist(const std::string& name) const;
    VirtualChannel* findChannel(const std::string& name) const;
};
