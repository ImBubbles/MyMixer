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

    pw_buffer *inBuffer =
        pw_stream_dequeue_buffer(channel->getSource()->stream);

    if (inBuffer == nullptr) {
        return;
    }

    pw_buffer *outBuffer =
        pw_stream_dequeue_buffer(channel->getSink()->stream);

    if (outBuffer == nullptr) {
        pw_stream_queue_buffer(channel->getSource()->stream, inBuffer);
        return;
    }


    spa_data *inData =
        &inBuffer->buffer->datas[0];

    spa_data *outData =
        &outBuffer->buffer->datas[0];


    // assuming float audio
    float *input =
        static_cast<float *>(inData->data);

    float *output =
        static_cast<float *>(outData->data);


    uint32_t samples =
        inData->chunk->size / sizeof(float);


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
}

StreamContext::StreamContext(VirtualChannel* channel, pw_stream* stream, const bool process)
    :
    channel(channel), stream(stream) {
    events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = stream_state_changed,
    };
    if (process) {
        events = {
            .process = stream_process,
        };
    }
    pw_thread_loop_lock(channel->context->loop);
    pw_stream_add_listener(
        stream,
        &listener,
        &events,
        channel
        );
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
    source(new StreamContext(this, source, true)),
    sink(new StreamContext(this, sink, false))
{

    //pw_stream_add_listener(source, &inputListener, &inputStreamEvents, this);
    //pw_stream_add_listener(sink, &outputListener, &outputStreamEvents, this);

    Log::info(
        "Channel \"" + name + "\" created with input node \"" + name + "_input\" and output node \"" +
        name + "_output\""
    );
}

VirtualChannel::~VirtualChannel() {
    Log::info("Destroying virtual channel \"" + name + "\"");
    delete source;
    source = nullptr;
    delete sink;
    sink = nullptr;
    Log::info("Destroyed virtual channel \"" + name + "\"");
}

bool VirtualChannel::waitForNodeIds(pw_stream* source, pw_stream* sink, const std::string& name) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while ((pw_stream_get_node_id(source) == PW_ID_ANY || pw_stream_get_node_id(sink) == PW_ID_ANY) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
           }

    const bool ready = pw_stream_get_node_id(source) != PW_ID_ANY && pw_stream_get_node_id(sink) != PW_ID_ANY;
    if (!ready) {
        Log::error("Channel \"" + name + "\" node ids are not available yet");
    }
    return ready;
}

bool VirtualChannel::waitForNodeIds() const {
    return waitForNodeIds(source->stream, sink->stream, name);
}

const StreamContext* VirtualChannel::getSource() const {
    return source;
}
const StreamContext* VirtualChannel::getSink() const {
    return sink;
}
