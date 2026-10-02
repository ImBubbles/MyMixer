#include "VirtualChannel.h"
#include "../logger/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <utility>
#include "StreamFactory.h"

static void stream_process(void *data)
{
    auto *streamContext = static_cast<StreamContext *>(data);
    auto *channel = streamContext->channel;

    pw_buffer *inBuffer = pw_stream_dequeue_buffer(streamContext->stream);

    if (inBuffer == nullptr) {
        return;
    }

    float leftPeak = 0.0f;
    float rightPeak = 0.0f;
    const uint32_t format = streamContext->negotiatedFormat.load(std::memory_order_relaxed);
    const uint32_t channelCount = streamContext->negotiatedChannels.load(std::memory_order_relaxed);
    const uint32_t leftChannel = streamContext->leftChannel.load(std::memory_order_relaxed);
    const uint32_t rightChannel = streamContext->rightChannel.load(std::memory_order_relaxed);
    const bool planar = format == SPA_AUDIO_FORMAT_F32P;
    const bool interleaved = format == SPA_AUDIO_FORMAT_F32;
    const auto validRange = [](const spa_data& audioData) {
        return audioData.data != nullptr && audioData.chunk != nullptr &&
               (audioData.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) == 0 &&
               audioData.chunk->offset <= audioData.maxsize &&
               audioData.chunk->size <= audioData.maxsize - audioData.chunk->offset;
    };
    if (streamContext->hasNegotiatedFormat.load(std::memory_order_relaxed) && channelCount >= 2 &&
        leftChannel < channelCount && rightChannel < channelCount && inBuffer->buffer != nullptr) {
        const auto samplePeak = [](float sample, float& peak) {
            const float magnitude = std::fabs(sample);
            if (std::isfinite(magnitude)) {
                peak = std::max(peak, magnitude);
            }
        };
        if (interleaved && inBuffer->buffer->n_datas >= 1) {
            const spa_data& audioData = inBuffer->buffer->datas[0];
            const uint32_t frameStride = audioData.chunk != nullptr && audioData.chunk->stride > 0
                ? static_cast<uint32_t>(audioData.chunk->stride)
                : channelCount * sizeof(float);
            if (validRange(audioData) && frameStride >= channelCount * sizeof(float)) {
                const uint32_t frameCount = audioData.chunk->size / frameStride;
                const auto* bytes = static_cast<const uint8_t*>(audioData.data) + audioData.chunk->offset;
                for (uint32_t frame = 0; frame < frameCount; ++frame) {
                    float leftSample = 0.0f;
                    float rightSample = 0.0f;
                    std::memcpy(&leftSample, bytes + frame * frameStride + leftChannel * sizeof(float), sizeof(float));
                    std::memcpy(&rightSample, bytes + frame * frameStride + rightChannel * sizeof(float), sizeof(float));
                    samplePeak(leftSample, leftPeak);
                    samplePeak(rightSample, rightPeak);
                }
            }
        } else if (planar && inBuffer->buffer->n_datas >= channelCount) {
            for (const auto [audioChannel, peak] : {std::pair{leftChannel, &leftPeak}, std::pair{rightChannel, &rightPeak}}) {
                const spa_data& audioData = inBuffer->buffer->datas[audioChannel];
                const uint32_t sampleStride = audioData.chunk != nullptr && audioData.chunk->stride > 0
                    ? static_cast<uint32_t>(audioData.chunk->stride)
                    : sizeof(float);
                if (!validRange(audioData) || sampleStride < sizeof(float)) {
                    continue;
                }
                const uint32_t frameCount = audioData.chunk->size / sampleStride;
                const auto* bytes = static_cast<const uint8_t*>(audioData.data) + audioData.chunk->offset;
                for (uint32_t frame = 0; frame < frameCount; ++frame) {
                    float sample = 0.0f;
                    std::memcpy(&sample, bytes + frame * sampleStride, sizeof(float));
                    samplePeak(sample, *peak);
                }
            }
        }
    }
    channel->updateInputPeaks(leftPeak, rightPeak);

    const StreamContext* outputContext = channel->getSink();
    pw_buffer *outBuffer = pw_stream_dequeue_buffer(outputContext->stream);

    if (outBuffer == nullptr) {
        pw_stream_queue_buffer(streamContext->stream, inBuffer);
        return;
    }

    const bool sameFormat = streamContext->hasNegotiatedFormat.load(std::memory_order_relaxed) &&
        outputContext->hasNegotiatedFormat.load(std::memory_order_relaxed) &&
        streamContext->negotiatedFormat.load(std::memory_order_relaxed) == outputContext->negotiatedFormat.load(std::memory_order_relaxed) &&
        streamContext->negotiatedChannels.load(std::memory_order_relaxed) == outputContext->negotiatedChannels.load(std::memory_order_relaxed);
    const bool isPlanar = format == SPA_AUDIO_FORMAT_F32P;
    const uint32_t dataCount = isPlanar ? channelCount : 1;
    bool canCopy = sameFormat && inBuffer->buffer != nullptr && outBuffer->buffer != nullptr &&
                   (format == SPA_AUDIO_FORMAT_F32 || isPlanar) &&
                   inBuffer->buffer->n_datas >= dataCount && outBuffer->buffer->n_datas >= dataCount;
    if (canCopy) {
        for (uint32_t index = 0; index < dataCount; ++index) {
            const spa_data& sourceData = inBuffer->buffer->datas[index];
            const spa_data& destinationData = outBuffer->buffer->datas[index];
            if (!validRange(sourceData) || destinationData.data == nullptr || destinationData.chunk == nullptr ||
                sourceData.chunk->size > destinationData.maxsize) {
                canCopy = false;
                break;
            }
        }
    }

    if (canCopy) {
        for (uint32_t index = 0; index < dataCount; ++index) {
            const spa_data& sourceData = inBuffer->buffer->datas[index];
            spa_data& destinationData = outBuffer->buffer->datas[index];
            const auto* sourceBytes = static_cast<const uint8_t*>(sourceData.data) + sourceData.chunk->offset;
            std::memcpy(destinationData.data, sourceBytes, sourceData.chunk->size);
            destinationData.chunk->offset = 0;
            destinationData.chunk->size = sourceData.chunk->size;
            destinationData.chunk->stride = sourceData.chunk->stride;
            destinationData.chunk->flags = sourceData.chunk->flags;
        }
    } else if (outBuffer->buffer != nullptr) {
        for (uint32_t index = 0; index < outBuffer->buffer->n_datas; ++index) {
            if (outBuffer->buffer->datas[index].chunk != nullptr) {
                outBuffer->buffer->datas[index].chunk->size = 0;
            }
        }
    }

    pw_stream_queue_buffer(streamContext->stream, inBuffer);
    pw_stream_queue_buffer(outputContext->stream, outBuffer);
}

static void stream_state_changed(
    void *data,
    enum pw_stream_state old_state,
    enum pw_stream_state state,
    const char *error)
{
    auto *streamContext = static_cast<StreamContext *>(data);
    auto *channel = streamContext->channel;

    if (error != nullptr) {
        Log::error("Stream error: " + std::string(error));
    }
    Log::debug(
        "Channel \"" + channel->name +
        "\" stream state: " +
        std::string(pw_stream_state_as_string(state))
    );

    pw_thread_loop_signal(channel->context->loop, false);
}

static void stream_param_changed(void* data, const uint32_t id, const spa_pod* parameter)
{
    auto* streamContext = static_cast<StreamContext*>(data);
    if (id != SPA_PARAM_Format || parameter == nullptr) {
        return;
    }

    spa_audio_info_raw format{};
    if (spa_format_audio_raw_parse(parameter, &format) < 0) {
        streamContext->hasNegotiatedFormat.store(false, std::memory_order_relaxed);
        Log::warning("Stream negotiated a non-raw audio format");
        return;
    }

    uint32_t leftChannel = 0;
    uint32_t rightChannel = 1;
    if (!SPA_FLAG_IS_SET(format.flags, SPA_AUDIO_FLAG_UNPOSITIONED)) {
        bool foundLeft = false;
        bool foundRight = false;
        for (uint32_t index = 0; index < format.channels; ++index) {
            if (format.position[index] == SPA_AUDIO_CHANNEL_FL) {
                leftChannel = index;
                foundLeft = true;
            } else if (format.position[index] == SPA_AUDIO_CHANNEL_FR) {
                rightChannel = index;
                foundRight = true;
            }
        }
        if (!foundLeft || !foundRight) {
            streamContext->hasNegotiatedFormat.store(false, std::memory_order_relaxed);
            Log::warning("Stream format does not provide front-left and front-right channels");
            return;
        }
    }

    streamContext->negotiatedFormat.store(format.format, std::memory_order_relaxed);
    streamContext->negotiatedChannels.store(format.channels, std::memory_order_relaxed);
    streamContext->leftChannel.store(leftChannel, std::memory_order_relaxed);
    streamContext->rightChannel.store(rightChannel, std::memory_order_relaxed);
    const bool supportedFormat = format.channels >= 2 &&
        (format.format == SPA_AUDIO_FORMAT_F32 || format.format == SPA_AUDIO_FORMAT_F32P);
    streamContext->hasNegotiatedFormat.store(supportedFormat, std::memory_order_relaxed);
    Log::info("Stream negotiated audio format " + std::to_string(format.format) + " with " +
              std::to_string(format.channels) + " channels; meter " + (supportedFormat ? "enabled" : "unsupported"));
}

StreamContext::StreamContext(VirtualChannel* channel, pw_stream* stream, const pw_direction direction, const bool process)
    :
    channel(channel), stream(stream), direction(direction) {
    events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = stream_state_changed,
        .param_changed = stream_param_changed,
    };
    if (process) {
        events.process = stream_process;
    }
    pw_thread_loop_lock(channel->context->loop);
    // Listener
    pw_stream_add_listener(
        stream,
        &listener,
        &events,
        this
        );
    // Parameters
    uint8_t buffer[1024];

    auto builder =
        SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const auto* info = StreamFactory::createAudioInfoRawDefault();
    const spa_pod *params[1];
    params[0] = spa_format_audio_raw_build(
        &builder,
        SPA_PARAM_EnumFormat,
        info
    );
    // Connect using params
    if (pw_stream_connect(
        stream,
        direction,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_MAP_BUFFERS
        ),
        params,
        1) < 0) {
        Log::error("Failed to connect source stream");
        }
    pw_thread_loop_unlock(channel->context->loop);
}
StreamContext::~StreamContext() {
    Log::info("Destroying stream");
    if (stream == nullptr) {
        return;
    }
    pw_thread_loop_lock(channel->context->loop);
    spa_hook_remove(&listener);
    pw_stream_disconnect(stream);
    pw_stream_destroy(stream);
    stream = nullptr;
    pw_thread_loop_unlock(channel->context->loop);
    Log::info("Destroyed stream");
}

VirtualChannel::VirtualChannel(PipeWireContext* context, const std::string& name, const std::string& description, pw_stream* source, pw_stream* sink) :
    name(name),
    description(description),
    context(context),
    source(new StreamContext(this, source, PW_DIRECTION_INPUT, true)),
    sink(new StreamContext(this, sink, PW_DIRECTION_OUTPUT, false))
{
    Log::info(
        "Channel \"" + name + "\" created with source node \"" + name + "_source\" and sink node \"" +
        name + "_sink\""
    );
}

VirtualChannel::~VirtualChannel() {
    Log::info("Destroying virtual channel \"" + name + "\"");
    clearConnections();

    delete source;
    source = nullptr;
    delete sink;
    sink = nullptr;
    Log::info("Destroyed virtual channel \"" + name + "\"");
}

void VirtualChannel::clearConnections() {
    if (outputLinks.empty()) {
        return;
    }

    pw_thread_loop_lock(context->loop);
    for (const auto& outputLink : outputLinks) {
        if (outputLink == nullptr) {
            continue;
        }
        for (pw_link* link : outputLink->links) {
            if (link != nullptr) {
                pw_core_destroy(context->core, link);
            }
        }
    }
    pw_thread_loop_unlock(context->loop);
    outputLinks.clear();
}

bool VirtualChannel::clearConnectionsTo(const std::string& destinationName) {
    bool removed = false;
    pw_thread_loop_lock(context->loop);
    auto link = outputLinks.begin();
    while (link != outputLinks.end()) {
        if (*link == nullptr || (*link)->to != destinationName) {
            ++link;
            continue;
        }

        for (pw_link* pipeWireLink : (*link)->links) {
            if (pipeWireLink != nullptr) {
                pw_core_destroy(context->core, pipeWireLink);
            }
        }
        removed = true;
        link = outputLinks.erase(link);
    }
    pw_thread_loop_unlock(context->loop);
    return removed;
}

std::pair<float, float> VirtualChannel::consumeInputPeaks() noexcept {
    return {
        leftInputPeak.exchange(0.0f, std::memory_order_relaxed),
        rightInputPeak.exchange(0.0f, std::memory_order_relaxed),
    };
}

void VirtualChannel::updateInputPeaks(const float left, const float right) noexcept {
    float previousLeft = leftInputPeak.load(std::memory_order_relaxed);
    while (left > previousLeft && !leftInputPeak.compare_exchange_weak(previousLeft, left, std::memory_order_relaxed)) {
    }
    float previousRight = rightInputPeak.load(std::memory_order_relaxed);
    while (right > previousRight && !rightInputPeak.compare_exchange_weak(previousRight, right, std::memory_order_relaxed)) {
    }
}

bool VirtualChannel::connect(VirtualChannel* from, const VirtualChannel* to) const {
    if (from->context->core != to->context->core) {
        Log::error("Cannot link channels \"" + from->name + "\" and \"" + to->name + "\": different PipeWire cores");
        return false;
    }

    const StreamContext* fromSink = from->getSink();
    const StreamContext* toSource = to->getSource();

    if (fromSink == nullptr || toSource == nullptr) {
        Log::error("Cannot link channels \"" + from->name + "\" and \"" + to->name + "\": stream not initialized");
        return false;
    }

    for (const auto& outputLink : from->outputLinks) {
        if (outputLink != nullptr && outputLink->to == to->name) {
            Log::error("Cannot link channel \"" + from->name + "\" to \"" + to->name + "\": route already exists");
            return false;
        }
    }

    if (!waitForNodeIds(from->context->loop, fromSink->stream, toSource->stream, from->name)) {
        return false;
    }

    const uint32_t outputNodeId = pw_stream_get_node_id(fromSink->stream);
    const uint32_t inputNodeId = pw_stream_get_node_id(toSource->stream);

    const std::string* sourcePortNames[2] = {
        &fromSink->leftPortName,
        &fromSink->rightPortName
    };
    const std::string* destinationPortNames[2] = {
        &toSource->leftPortName,
        &toSource->rightPortName
    };
    if (sourcePortNames[0]->empty() || sourcePortNames[1]->empty() ||
        destinationPortNames[0]->empty() || destinationPortNames[1]->empty()) {
        Log::error("Cannot link channels \"" + from->name + "\" and \"" + to->name + "\": port names are not available");
        return false;
    }
    from->outputLinks.reserve(from->outputLinks.size() + 1);
    auto outputLink = std::make_unique<OutputLink>();
    outputLink->from = from->name;
    outputLink->to = to->name;
    const std::array<std::string, 2> outputPortNames{*sourcePortNames[0], *sourcePortNames[1]};
    const std::array<std::string, 2> inputPortNames{*destinationPortNames[0], *destinationPortNames[1]};
    if (!from->context->createStereoLinks(outputNodeId, outputPortNames, inputNodeId, inputPortNames, outputLink->links)) {
        return false;
    }

    from->outputLinks.push_back(std::move(outputLink));
    Log::info("Linked channel \"" + from->name + "\" (" + outputPortNames[0] + ", " + outputPortNames[1] +
              ") -> \"" + to->name + "\" (" + inputPortNames[0] + ", " + inputPortNames[1] + ")");
    return true;
}

bool VirtualChannel::waitForNodeIds(pw_thread_loop* loop, pw_stream* source, pw_stream* sink, const std::string& name) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    pw_thread_loop_lock(loop);
    while ((pw_stream_get_node_id(source) == PW_ID_ANY || pw_stream_get_node_id(sink) == PW_ID_ANY) && std::chrono::steady_clock::now() < deadline) {
        pw_thread_loop_wait(loop);
    }
    pw_thread_loop_unlock(loop);

    const bool ready = pw_stream_get_node_id(source) != PW_ID_ANY && pw_stream_get_node_id(sink) != PW_ID_ANY;
    if (!ready) {
        Log::error("Channel \"" + name + "\" node ids are not available yet");
    }
    return ready;
}

bool VirtualChannel::waitForNodeIds() const {
    const bool result = waitForNodeIds(context->loop, source->stream, sink->stream, name);
    if (result) {
        source->nodeId = pw_stream_get_node_id(source->stream);
        Log::debug(name + " source has node id of " + std::to_string(source->nodeId));
        sink->nodeId = pw_stream_get_node_id(sink->stream);
        Log::debug(name + " sink has node id of " + std::to_string(sink->nodeId));
    }
    return result;
}

bool VirtualChannel::waitForPorts() const {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);

    pw_thread_loop_lock(context->loop);

    while (
        (source->leftPort == PW_ID_ANY ||
         source->rightPort == PW_ID_ANY ||
         sink->leftPort == PW_ID_ANY ||
         sink->rightPort == PW_ID_ANY ||
         source->leftPortName.empty() ||
         source->rightPortName.empty() ||
         sink->leftPortName.empty() ||
         sink->rightPortName.empty()) &&
        std::chrono::steady_clock::now() < deadline
    ) {
        pw_thread_loop_wait(context->loop);
    }

    pw_thread_loop_unlock(context->loop);

    const bool ready =
        source->leftPort != PW_ID_ANY &&
        source->rightPort != PW_ID_ANY &&
        sink->leftPort != PW_ID_ANY &&
        sink->rightPort != PW_ID_ANY &&
        !source->leftPortName.empty() &&
        !source->rightPortName.empty() &&
        !sink->leftPortName.empty() &&
        !sink->rightPortName.empty();

    if (!ready) {
        Log::error(
            "Channel \"" + name +
            "\" ports are not available yet"
        );
    }

    return ready;
}

const StreamContext* VirtualChannel::getSource() const {
    return source;
}
const StreamContext* VirtualChannel::getSink() const {
    return sink;
}
