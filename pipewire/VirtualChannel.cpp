#include "VirtualChannel.h"
#include "../logger/Log.h"

#include <algorithm>
#include <cstring>

const pw_stream_events VirtualChannel::inputStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = VirtualChannel::onStateChanged,
    .process = VirtualChannel::onProcess,
};

const pw_stream_events VirtualChannel::outputStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = VirtualChannel::onStateChanged,
};

const spa_pod* VirtualChannel::buildFormatParams(uint8_t* buffer, const size_t size) {
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, static_cast<uint32_t>(size));

    return static_cast<const spa_pod*>(spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_Format,
        SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType,
        SPA_POD_Id(SPA_MEDIA_TYPE_audio),
        SPA_FORMAT_mediaSubtype,
        SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_AUDIO_format,
        SPA_POD_Id(SPA_AUDIO_FORMAT_F32_LE),
        SPA_FORMAT_AUDIO_channels,
        SPA_POD_Int(channels),
        SPA_FORMAT_AUDIO_rate,
        SPA_POD_Int(sampleRate)
    ));
}

VirtualChannel::VirtualChannel(pw_core* core, const std::string& name, const std::string& description) :
    name(name),
    description(description),
    core(core),
    input(StreamFactory::createInputNode(core, name, description)),
    output(StreamFactory::createOutputNode(core, name, description))
{
    uint8_t buffer[1024];
    const spa_pod* params[] = { buildFormatParams(buffer, sizeof(buffer)) };
    const enum pw_stream_flags flags = static_cast<enum pw_stream_flags>(
        PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS
    );

    pw_stream_add_listener(input, &inputListener, &inputStreamEvents, this);
    pw_stream_add_listener(output, &outputListener, &outputStreamEvents, this);

    if (pw_stream_connect(input, PW_DIRECTION_INPUT, PW_ID_ANY, flags, params, 1) < 0) {
        Log::error("Failed to connect input node for channel \"" + name + "\"");
    }

    if (pw_stream_connect(output, PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, params, 1) < 0) {
        Log::error("Failed to connect output node for channel \"" + name + "\"");
    }

    Log::debug(
        "Channel \"" + name + "\" created with input node \"" + name + "_input\" and output node \"" +
        name + "_output\""
    );
}

VirtualChannel::~VirtualChannel() {
    Log::info("Destroying virtual channel \"" + name + "\"");
    if (input != nullptr) {
        pw_stream_destroy(input);
        input = nullptr;
    }
    if (output != nullptr) {
        pw_stream_destroy(output);
        output = nullptr;
    }
    Log::info("Destroyed virtual channel \"" + name + "\"");
}

void VirtualChannel::onProcess(void* userdata) {
    auto* channel = static_cast<VirtualChannel*>(userdata);

    pw_buffer* inBuffer = pw_stream_dequeue_buffer(channel->input);
    if (inBuffer == nullptr) {
        return;
    }

    pw_buffer* outBuffer = pw_stream_dequeue_buffer(channel->output);
    if (outBuffer == nullptr) {
        pw_stream_queue_buffer(channel->input, inBuffer);
        return;
    }

    spa_buffer* inSpa = inBuffer->buffer;
    spa_buffer* outSpa = outBuffer->buffer;

    spa_data* inData = &inSpa->datas[0];
    spa_data* outData = &outSpa->datas[0];

    if (inData->data != nullptr && outData->data != nullptr && inData->chunk != nullptr && outData->chunk != nullptr) {
        const uint32_t size = std::min(inData->chunk->size, outData->maxsize);
        std::memcpy(outData->data, inData->data, size);
        outData->chunk->offset = 0;
        outData->chunk->size = size;
        outData->chunk->stride = inData->chunk->stride;
    }

    pw_stream_queue_buffer(channel->input, inBuffer);
    pw_stream_queue_buffer(channel->output, outBuffer);
}

void VirtualChannel::onStateChanged(
    void* userdata,
    enum pw_stream_state old,
    enum pw_stream_state state,
    const char* error
) {
    (void)old;

    auto* channel = static_cast<VirtualChannel*>(userdata);

    switch (state) {
        case PW_STREAM_STATE_ERROR:
            Log::error(
                channel->name + " stream error: " +
                std::string(error != nullptr ? error : "unknown")
            );
            break;

        case PW_STREAM_STATE_CONNECTING:
            Log::debug(channel->name + " connecting");
            break;

        case PW_STREAM_STATE_PAUSED:
            Log::debug(channel->name + " paused");
            break;

        case PW_STREAM_STATE_STREAMING:
            Log::debug(channel->name + " streaming");
            break;

        default:
            break;
    }
}
