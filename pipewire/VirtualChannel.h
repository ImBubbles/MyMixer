#pragma once
#include <string>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include "../util/UtilString.h"
#include "PipeWireContext.h"

class PipeWireContext;
struct VirtualChannel;
struct StreamContext {
    StreamContext(VirtualChannel* channel, pw_stream* stream, const pw_direction direction, const bool process);
    VirtualChannel* channel;
    struct pw_stream* stream;

    uint32_t nodeId = PW_ID_ANY;
    uint32_t leftPort = PW_ID_ANY;
    uint32_t rightPort = PW_ID_ANY;

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

    static bool waitForNodeIds(pw_thread_loop* loop, pw_stream* source, pw_stream* sink, const std::string& name);
    bool waitForNodeIds() const;
    bool waitForPorts() const;

    const StreamContext* getSource() const;
    const StreamContext* getSink() const;
    bool connect(VirtualChannel* from, const VirtualChannel* to);
    std::vector<struct pw_link*> outputLinks;
private:
    StreamContext* source;
    StreamContext* sink;
};