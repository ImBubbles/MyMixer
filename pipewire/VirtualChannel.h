#pragma once
#include <string>
#include <pipewire/pipewire.h>

#include "../util/UtilString.h"
#include "PipeWireContext.h"

class PipeWireContext;
struct VirtualChannel;
struct StreamContext {
    StreamContext(VirtualChannel* channel, pw_stream* stream, const bool process);
    VirtualChannel* channel;
    struct pw_stream* stream;

    struct spa_hook listener;
    struct pw_stream_events events;
    ~StreamContext();
};

struct VirtualChannel {
    explicit VirtualChannel(PipeWireContext* context, const std::string& name, const std::string& description, pw_stream* input, pw_stream* output);
    ~VirtualChannel();
    std::string name;
    std::string description;
    PipeWireContext* context;

    static bool waitForNodeIds(pw_stream* source, pw_stream* sink, const std::string& name);
    bool waitForNodeIds() const;

    const StreamContext* getSource() const;
    const StreamContext* getSink() const;
private:
    StreamContext* source;
    StreamContext* sink;
};