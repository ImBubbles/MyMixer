#pragma once
#include <atomic>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>
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
    pw_direction direction;

    uint32_t nodeId = PW_ID_ANY;
    uint32_t leftPort = PW_ID_ANY;
    uint32_t rightPort = PW_ID_ANY;
    std::string leftPortName;
    std::string rightPortName;
    std::atomic<uint32_t> negotiatedFormat{SPA_AUDIO_FORMAT_UNKNOWN};
    std::atomic<uint32_t> negotiatedChannels{0};
    std::atomic<uint32_t> leftChannel{0};
    std::atomic<uint32_t> rightChannel{1};
    std::atomic<bool> hasNegotiatedFormat{false};

    struct spa_hook listener;
    struct pw_stream_events events;
    ~StreamContext();
};

struct OutputLink;

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
    std::pair<float, float> consumeInputPeaks() noexcept;
    void updateInputPeaks(float left, float right) noexcept;
    bool connect(VirtualChannel* from, const VirtualChannel* to) const;
    void clearConnections();
    bool clearConnectionsTo(const std::string& destinationName);
    std::vector<std::unique_ptr<OutputLink>> outputLinks;
private:
    StreamContext* source;
    StreamContext* sink;
    std::atomic<float> leftInputPeak{0.0f};
    std::atomic<float> rightInputPeak{0.0f};
};

struct OutputLink {
    std::string from;
    std::string to;
    std::array<pw_link*, 2> links{};
};