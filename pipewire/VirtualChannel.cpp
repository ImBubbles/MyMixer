#include "VirtualChannel.h"
#include "../logger/Log.h"

#include <pipewire/keys.h>
#include <pipewire/link.h>
#include <pipewire/properties.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

static bool waitForStreamNodeIds(pw_stream* input, pw_stream* output, const std::string& name) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while ((pw_stream_get_node_id(input) == PW_ID_ANY || pw_stream_get_node_id(output) == PW_ID_ANY) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const bool ready = pw_stream_get_node_id(input) != PW_ID_ANY && pw_stream_get_node_id(output) != PW_ID_ANY;
    if (!ready) {
        Log::error("Channel \"" + name + "\" node ids are not available yet");
    }
    return ready;
}

bool VirtualChannel::connect(VirtualChannel* from, const VirtualChannel* to) {
    if (from->core != to->core) {
        Log::error("Cannot link channels \"" + from->name + "\" and \"" + to->name + "\": different PipeWire cores");
        return false;
    }

    if (from->output == nullptr || to->input == nullptr) {
        Log::error("Cannot link channels \"" + from->name + "\" and \"" + to->name + "\": stream not initialized");
        return false;
    }

    if (!from->outputLinks.empty()) {
        Log::error("Cannot link channel \"" + from->name + "\": output is already linked");
        return false;
    }

    if (!waitForStreamNodeIds(from->output, to->input, from->name)) {
        return false;
    }

    const uint32_t outputNodeId = pw_stream_get_node_id(from->output);
    const uint32_t inputNodeId = pw_stream_get_node_id(to->input);
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

        auto* link = static_cast<pw_link*>(pw_core_create_object(
            from->core,
            "link-factory",
            PW_TYPE_INTERFACE_Link,
            PW_VERSION_LINK,
            &props->dict,
            0
        ));
        pw_properties_free(props);

        if (link == nullptr) {
            Log::error("Failed to create PipeWire link for stereo channel " + std::to_string(channel) + " from \"" + from->name + "\" to \"" + to->name + "\"");
            break;
        }

        from->outputLinks.push_back(link);
    }

    if (from->outputLinks.size() != 2) {
        for (pw_link* link : from->outputLinks) {
            pw_core_destroy(from->core, link);
        }
        from->outputLinks.clear();
        return false;
    }

    Log::info("Linked channel \"" + from->name + "\" -> \"" + to->name + "\" via PipeWire stereo links");
    return true;
}

bool VirtualChannel::disconnect(VirtualChannel* from, const VirtualChannel* to) {
    if (from->core != to->core) {
        Log::error("Cannot unlink channels \"" + from->name + "\" and \"" + to->name + "\": different PipeWire cores");
        return false;
    }

    if (from->output == nullptr || to->input == nullptr) {
        Log::error("Cannot unlink channels \"" + from->name + "\" and \"" + to->name + "\": stream not initialized");
        return false;
    }

    if (from->outputLinks.empty()) {
        Log::warning("No existing links to disconnect for channel \"" + from->name + "\"");
        return false;
    }

    for (pw_link* link : from->outputLinks) {
        if (link != nullptr) {
            pw_core_destroy(from->core, link);
        }
    }
    from->outputLinks.clear();

    Log::info("Unlinked channel \"" + from->name + "\" -> \"" + to->name + "\"");
    return true;
}

const pw_stream_events VirtualChannel::inputStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = VirtualChannel::onStateChanged,
    .process = VirtualChannel::onProcess,
};

const pw_stream_events VirtualChannel::outputStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = VirtualChannel::onStateChanged,
};

bool VirtualChannel::waitForNodeIds() const {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while ((pw_stream_get_node_id(input) == PW_ID_ANY || pw_stream_get_node_id(output) == PW_ID_ANY) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const bool ready = pw_stream_get_node_id(input) != PW_ID_ANY && pw_stream_get_node_id(output) != PW_ID_ANY;
    if (!ready) {
        Log::error("Channel \"" + name + "\" node ids are not available yet");
    }
    return ready;
}

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

    Log::info(
        "Channel \"" + name + "\" created with input node \"" + name + "_input\" and output node \"" +
        name + "_output\""
    );
}

VirtualChannel::~VirtualChannel() {
    Log::info("Destroying virtual channel \"" + name + "\"");
    for (pw_link* link : outputLinks) {
        if (link != nullptr) {
            pw_core_destroy(core, link);
        }
    }
    outputLinks.clear();

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
