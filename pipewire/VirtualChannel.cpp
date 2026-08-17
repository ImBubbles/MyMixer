#include "VirtualChannel.h"
#include "../logger/Log.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include "StreamFactory.h"

static void stream_process(void *data)
{
    auto *channel = static_cast<VirtualChannel *>(data);

    pw_buffer *inBuffer = pw_stream_dequeue_buffer(channel->getSource()->stream);

    if (inBuffer == nullptr) {
        return;
    }

    pw_buffer *outBuffer = pw_stream_dequeue_buffer(channel->getSink()->stream);

    if (outBuffer == nullptr) {
        pw_stream_queue_buffer(channel->getSource()->stream, inBuffer);
        return;
    }


    spa_data *inData = &inBuffer->buffer->datas[0];

    spa_data *outData = &outBuffer->buffer->datas[0];


    // assuming float audio
    float *input = static_cast<float *>(inData->data);

    float *output = static_cast<float *>(outData->data);


    uint32_t samples = inData->chunk->size / sizeof(float);


    memcpy(
        output,
        input,
        samples * sizeof(float)
    );


    outData->chunk->size =
        inData->chunk->size;


    pw_stream_queue_buffer(
        channel->getSource()->stream,
        inBuffer
    );

    pw_stream_queue_buffer(
        channel->getSink()->stream,
        outBuffer
    );
}

static void stream_state_changed(
    void *data,
    enum pw_stream_state old_state,
    enum pw_stream_state state,
    const char *error)
{
    auto *channel = static_cast<VirtualChannel *>(data);

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

StreamContext::StreamContext(VirtualChannel* channel, pw_stream* stream, const pw_direction direction, const bool process)
    :
    channel(channel), stream(stream) {
    events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = stream_state_changed,
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
        channel
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
    // Destroy any created links
    if (!outputLinks.empty()) {
        pw_thread_loop_lock(context->loop);
        for (pw_link* link : outputLinks) {
            if (link != nullptr) {
                pw_core_destroy(context->core, link);
            }
        }
        outputLinks.clear();
        pw_thread_loop_unlock(context->loop);
    }

    delete source;
    source = nullptr;
    delete sink;
    sink = nullptr;
    Log::info("Destroyed virtual channel \"" + name + "\"");
}

bool VirtualChannel::connect(VirtualChannel* from, const VirtualChannel* to) {
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

    if (!from->outputLinks.empty()) {
        Log::error("Cannot link channel \"" + from->name + "\": output is already linked");
        return false;
    }

    if (!waitForNodeIds(context->loop, fromSink->stream, toSource->stream, from->name)) {
        return false;
    }

    const uint32_t outputNodeId = pw_stream_get_node_id(fromSink->stream);
    const uint32_t inputNodeId = pw_stream_get_node_id(toSource->stream);
    const char* portNames[2][2] = {
        { "output_FL", "input_FL" },
        { "output_FR", "input_FR" }
    };

    for (int channel = 0; channel < 2; ++channel) {
        pw_properties* props = pw_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, std::to_string(outputNodeId).c_str(),
            PW_KEY_LINK_INPUT_NODE, std::to_string(inputNodeId).c_str(),
            PW_KEY_LINK_OUTPUT_PORT, portNames[channel][0],
            PW_KEY_LINK_INPUT_PORT, portNames[channel][1],
            nullptr
        );

        if (props == nullptr) {
            Log::error("Failed to create link properties for stereo channel " + std::to_string(channel) + " between \"" + from->name + "\" and \"" + to->name + "\"");
            break;
        }

        pw_thread_loop_lock(context->loop);
        auto* link = static_cast<pw_link*>(pw_core_create_object(
            context->core,
            "link-factory",
            PW_TYPE_INTERFACE_Link,
            PW_VERSION_LINK,
            &props->dict,
            0
        ));
        pw_thread_loop_unlock(context->loop);
        pw_properties_free(props);

        if (link == nullptr) {
            Log::error("Failed to create PipeWire link for stereo channel " + std::to_string(channel) + " from \"" + from->name + "\" to \"" + to->name + "\"");
            break;
        }

        from->outputLinks.push_back(link);
    }

    if (from->outputLinks.size() != 2) {
        // cleanup
        pw_thread_loop_lock(context->loop);
        for (pw_link* link : from->outputLinks) {
            if (link != nullptr) {
                pw_core_destroy(context->core, link);
            }
        }
        from->outputLinks.clear();
        pw_thread_loop_unlock(context->loop);
        return false;
    }

    Log::info("Linked channel \"" + from->name + "\" -> \"" + to->name + "\" via PipeWire stereo links");
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
         sink->rightPort == PW_ID_ANY) &&
        std::chrono::steady_clock::now() < deadline
    ) {
        pw_thread_loop_wait(context->loop);
    }

    pw_thread_loop_unlock(context->loop);

    const bool ready =
        source->leftPort != PW_ID_ANY &&
        source->rightPort != PW_ID_ANY &&
        sink->leftPort != PW_ID_ANY &&
        sink->rightPort != PW_ID_ANY;

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
