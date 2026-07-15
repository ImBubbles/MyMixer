#include "VirtualChannel.h"
#include "../logger/Log.h"

#include <pipewire/keys.h>
#include <pipewire/link.h>
#include <pipewire/properties.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

VirtualChannel::VirtualChannel(pw_core* core, const std::string& name, const std::string& description) :
    name(name),
    description(description),
    core(core),
    input(StreamFactory::createInputNode(core, name, description)),
    output(StreamFactory::createOutputNode(core, name, description))
{

    pw_stream_add_listener(input, &inputListener, &inputStreamEvents, this);
    pw_stream_add_listener(output, &outputListener, &outputStreamEvents, this);

    Log::info(
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

bool VirtualChannel::waitForNodeIds(pw_stream* input, pw_stream* output, const std::string& name) {
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

bool VirtualChannel::waitForNodeIds() const {
    return waitForNodeIds(input, output, name);
}