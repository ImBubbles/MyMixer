#include "PipeWireContext.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <optional>
#include <pipewire/pipewire.h>

#include "StreamFactory.h"
#include "../logger/Log.h"

static void killOldProcesses() {
    pid_t selfPid = getpid();
    char selfExePath[PATH_MAX];
    ssize_t selfLen = readlink("/proc/self/exe", selfExePath, sizeof(selfExePath) - 1);
    if (selfLen <= 0) {
        return;
    }
    selfExePath[selfLen] = '\0';

    DIR* procDir = opendir("/proc");
    if (procDir == nullptr) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(procDir)) != nullptr) {
        if (entry->d_type != DT_DIR) {
            continue;
        }

        char* endPtr = nullptr;
        long pid = strtol(entry->d_name, &endPtr, 10);
        if (pid <= 0 || *endPtr != '\0' || static_cast<pid_t>(pid) == selfPid) {
            continue;
        }

        char otherExePath[PATH_MAX];
        char procExeLink[PATH_MAX];
        snprintf(procExeLink, sizeof(procExeLink), "/proc/%ld/exe", pid);
        ssize_t otherLen = readlink(procExeLink, otherExePath, sizeof(otherExePath) - 1);
        if (otherLen <= 0) {
            continue;
        }
        otherExePath[otherLen] = '\0';

        if (strcmp(selfExePath, otherExePath) == 0) {
            if (kill(static_cast<pid_t>(pid), SIGTERM) == 0) {
                Log::info("Killed old MyMixer process " + std::to_string(pid));
            } else {
                Log::warning("Failed to kill old MyMixer process " + std::to_string(pid) + ": " + std::string(strerror(errno)));
            }
        }
    }

    closedir(procDir);
}

static void registry_global(
    void *data,
    uint32_t id,
    uint32_t permissions,
    const char *type,
    uint32_t version,
    const struct spa_dict *props)
{
    auto *ctx = static_cast<PipeWireContext *>(data);
    if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        ctx->addHardwareNode(id, props);
        return;
    }

    if (strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
        const char* nodeIdStr = spa_dict_lookup(props, PW_KEY_NODE_ID);
        if (nodeIdStr == nullptr) {
            return;
        }

        ctx->addHardwarePort(id, props);

        uint32_t nodeId = std::stoul(nodeIdStr);
        StreamContext* stream = ctx->findStreamContext(nodeId);
        if (stream == nullptr)
            return;

        const char* channel = spa_dict_lookup(props, "audio.channel");
        if (channel == nullptr)
            return;
        const char* portName = spa_dict_lookup(props, PW_KEY_PORT_NAME);
        const char* portDirection = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
        const char* isMonitor = spa_dict_lookup(props, PW_KEY_PORT_MONITOR);
        const char* expectedDirection = stream->direction == PW_DIRECTION_INPUT ? "in" : "out";
        if (portDirection == nullptr || strcmp(portDirection, expectedDirection) != 0 ||
            (isMonitor != nullptr && strcmp(isMonitor, "true") == 0)) {
            return;
        }

        if (strcmp(channel, "FL") == 0) {
            stream->leftPort = id;
            if (portName != nullptr) {
                stream->leftPortName = portName;
            }
        } else if (strcmp(channel, "FR") == 0) {
            stream->rightPort = id;
            if (portName != nullptr) {
                stream->rightPortName = portName;
            }
        }
        Log::logAsync(LogLevel::DEBUG, "Set port " + std::to_string(id) + " for node " + std::to_string(nodeId) + " channel " + std::string(channel));
        pw_thread_loop_signal(ctx->loop, false);
        return;
    }

    if (strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        const char* outNodeStr = spa_dict_lookup(props, "link.output.node");
        const char* outPortStr = spa_dict_lookup(props, "link.output.port");
        const char* inNodeStr = spa_dict_lookup(props, "link.input.node");
        const char* inPortStr = spa_dict_lookup(props, "link.input.port");

        if (outNodeStr && outPortStr && inNodeStr && inPortStr) {
            Log::logAsync(LogLevel::INFO, "Registry: link " + std::to_string(id) + " -> " + std::string(outNodeStr) + ":" + outPortStr +
                      " -> " + inNodeStr + ":" + inPortStr);
        } else {
            Log::logAsync(LogLevel::DEBUG, "Registry: link object " + std::to_string(id) + " published (partial props)");
        }
        return;
    }
}

static void registry_global_remove(void *data, const uint32_t id)
{
    static_cast<PipeWireContext*>(data)->removeRegistryGlobal(id);
}

struct LinkProxyState {
    pw_thread_loop* loop = nullptr;
    bool bound = false;
    bool failed = false;
    std::string error;
};

static void link_proxy_bound(void* data, const uint32_t)
{
    auto* state = static_cast<LinkProxyState*>(data);
    state->bound = true;
    pw_thread_loop_signal(state->loop, false);
}

static void link_proxy_error(void* data, const int, const int, const char* message)
{
    auto* state = static_cast<LinkProxyState*>(data);
    state->failed = true;
    state->error = message != nullptr ? message : "unknown PipeWire proxy error";
    pw_thread_loop_signal(state->loop, false);
}

static const pw_proxy_events link_proxy_events = {
    .version = PW_VERSION_PROXY_EVENTS,
    .bound = link_proxy_bound,
    .error = link_proxy_error,
};



PipeWireContext::PipeWireContext() {
    killOldProcesses();
    Log::info("Creating PipeWireContext");

    int argc = 1;
    char name[] = "MyMixer";
    char* args[] = { name, nullptr };
    char** argv = args;
    pw_init(&argc, &argv);

    loop = pw_thread_loop_new("MyMixer", nullptr);
    pw_thread_loop_start(loop);
    pw_thread_loop_lock(loop);
    context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    core = pw_context_connect(context, nullptr, 0);
    registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
    registry_events = {
        .version = PW_VERSION_REGISTRY_EVENTS,
        .global = registry_global,
        .global_remove = registry_global_remove,
    };
    pw_registry_add_listener(
        registry,
        &registry_listener,
        &registry_events,
        this
        );
    pw_thread_loop_unlock(loop);

    Log::info("Created PipeWireContext");
}

PipeWireContext::~PipeWireContext() {
    Log::info("Destroying PipeWireContext");

    spa_hook_remove(&registry_listener);
    for (const VirtualChannel* vc : virtualChannels) {
        if (vc == nullptr) {
            continue;
        }
        delete vc;
    }
    virtualChannels.clear();
    pw_thread_loop_lock(loop);
    for (const ManagedHardwareConnection& connection : hardwareConnections) {
        for (pw_link* link : connection.links) {
            if (link != nullptr) {
                pw_core_destroy(core, link);
            }
        }
    }
    hardwareConnections.clear();
    hardwareDevices.clear();
    // (no debug proxy retention to clean up)
    pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry));
    registry = nullptr;

    if (core != nullptr) {
        pw_core_disconnect(core);
        core = nullptr;
    }
    if (context != nullptr) {
        pw_context_destroy(context);
        context = nullptr;
    }
    pw_thread_loop_unlock(loop);
    pw_thread_loop_stop(loop);
    pw_thread_loop_destroy(loop);
    loop = nullptr;

    pw_deinit();
    Log::info("Destroyed PipeWireContext");
}

bool PipeWireContext::doesChannelExist(const std::string& name) const {
    for (const VirtualChannel* vc : virtualChannels) {
        if (vc == nullptr) {
            continue;
        }
        if (vc->name == name) {
            return true;
        }
    }
    return false;
}

VirtualChannel* PipeWireContext::registerChannel(VirtualChannel* channel) {
    pw_thread_loop_lock(loop);
    if (doesChannelExist(channel->name)) {
        pw_thread_loop_unlock(loop);
        delete channel;
        return nullptr;
    }
    /* Add early so registry events can find the StreamContext while ports are published. */
    virtualChannels.push_back(channel);
    pw_thread_loop_unlock(loop);

    if (!channel->waitForNodeIds() || !channel->waitForPorts()) {
        pw_thread_loop_lock(loop);
        auto it = std::find(virtualChannels.begin(), virtualChannels.end(), channel);
        if (it != virtualChannels.end()) {
            virtualChannels.erase(it);
        }
        pw_thread_loop_unlock(loop);
        delete channel;
        Log::error("Either nodes or ports failed to resolve.");
        return nullptr;
    }

    return channel;
}

bool PipeWireContext::removeChannel(const std::string& name) {
    VirtualChannel* channel = nullptr;
    pw_thread_loop_lock(loop);
    const auto it = std::find_if(virtualChannels.begin(), virtualChannels.end(), [&name](const VirtualChannel* candidate) {
        return candidate != nullptr && candidate->name == name;
    });
    if (it != virtualChannels.end()) {
        channel = *it;
    }
    pw_thread_loop_unlock(loop);

    if (channel == nullptr) {
        return false;
    }

    for (VirtualChannel* candidate : getChannels()) {
        if (candidate != nullptr && candidate != channel) {
            candidate->clearConnectionsTo(name);
        }
    }

    pw_thread_loop_lock(loop);
    auto connection = hardwareConnections.begin();
    while (connection != hardwareConnections.end()) {
        if ((!connection->source.isHardware && connection->source.name == name) ||
            (!connection->destination.isHardware && connection->destination.name == name)) {
            for (pw_link* link : connection->links) {
                if (link != nullptr) {
                    pw_core_destroy(core, link);
                }
            }
            connection = hardwareConnections.erase(connection);
        } else {
            ++connection;
        }
    }
    pw_thread_loop_unlock(loop);

    pw_thread_loop_lock(loop);
    const auto current = std::find(virtualChannels.begin(), virtualChannels.end(), channel);
    if (current != virtualChannels.end()) {
        virtualChannels.erase(current);
    }
    pw_thread_loop_unlock(loop);

    delete channel;
    return true;
}

VirtualChannel* PipeWireContext::createChannel(const std::string& name, const std::string& description) {
    // Also handles registering using PipeWireContext#registerChannel()
    if (doesChannelExist(name)) {
        // though register method also handles this, I'd rather do a name check before making a new object
        return nullptr;
    }
    auto* channel = StreamFactory::createVirtualChannel(this, name, description);

    return registerChannel(channel);
}

// TODO Unsure if this method works
bool PipeWireContext::linkPorts(const uint32_t outputNode, const uint32_t outputPort,
    const uint32_t inputNode, const uint32_t inputPort) const {

    pw_thread_loop_lock(loop);

    std::string linkOutputNode = std::to_string(outputNode);
    std::string linkOutputPort = std::to_string(outputPort);
    std::string linkInputNode = std::to_string(inputNode);
    std::string linkInputPort = std::to_string(inputPort);

    Log::debug("Creating link with properties: output=" + linkOutputNode + ":" + linkOutputPort + " input=" + linkInputNode + ":" + linkInputPort);

    // Try creating link properties using the standard PW_KEY_LINK_* keys
    pw_properties* props = pw_properties_new(
        PW_KEY_LINK_OUTPUT_NODE, linkOutputNode.c_str(),
        PW_KEY_LINK_OUTPUT_PORT, linkOutputPort.c_str(),
        PW_KEY_LINK_INPUT_NODE, linkInputNode.c_str(),
        PW_KEY_LINK_INPUT_PORT, linkInputPort.c_str(),
        nullptr
    );

    if (props == nullptr) {
        pw_thread_loop_unlock(loop);
        Log::error("Failed to create link properties");
        return false;
    }

    // Attempt to create the link via link-factory and interpret as a pw_link*
    void* raw_link = pw_core_create_object(
        core,
        "link-factory",
        PW_TYPE_INTERFACE_Link,
        PW_VERSION_LINK,
        &props->dict,
        0
    );

    pw_properties_free(props);

    pw_thread_loop_unlock(loop);

    if (raw_link == nullptr) {
        Log::error("Failed to create PipeWire link using PW_KEY_LINK_* keys");
        return false;
    }

    Log::info("Link created");

    return true;
}

// TODO Unsure if this method works
bool PipeWireContext::linkPortsLR(const uint32_t outputNode, const uint32_t outputL, const uint32_t outputR,
    const uint32_t inputNode, const uint32_t inputL, const uint32_t inputR) const {
    return linkPorts(outputNode, outputL, inputNode, inputL) && linkPorts(outputNode, outputR, inputNode, inputR);
}

// This works
bool PipeWireContext::linkStreams(const StreamContext* source, const StreamContext* sink) const {
    if (source == nullptr || sink == nullptr || source->channel == nullptr || sink->channel == nullptr) {
        Log::error("Cannot link streams without owning channels");
        return false;
    }

    if (source->channel->getSink() != source || sink->channel->getSource() != sink) {
        Log::error("Cannot link streams that are not a channel sink-to-source pair");
        return false;
    }

    return linkChannels(source->channel, sink->channel);
}

bool PipeWireContext::linkChannels(const VirtualChannel* source, const VirtualChannel* sink) const {
    if (source == nullptr || sink == nullptr) {
        Log::error("Cannot link null channels");
        return false;
    }
    Log::debug("linkChannels: source='" + source->name + "' sink='" + sink->name + "'");
    // Use VirtualChannel::connect which implements the link-factory usage
    // with named port properties and manages pw_link lifetimes.
    auto* nonConstSource = const_cast<VirtualChannel*>(source);
    return nonConstSource->connect(nonConstSource, sink);
}

bool PipeWireContext::setChannelConnection(
    const VirtualChannel* source,
    const VirtualChannel* destination,
    const bool connected) const {
    if (source == nullptr || destination == nullptr || source->context != this || destination->context != this ||
        core == nullptr || source->context->core != core || destination->context->core != core) {
        Log::error("Cannot change connection for channels outside this PipeWire context");
        return false;
    }

    if (connected) {
        return linkChannels(source, destination);
    }

    auto* mutableSource = const_cast<VirtualChannel*>(source);
    return mutableSource->clearConnectionsTo(destination->name);
}

bool PipeWireContext::createStereoLinks(
    const uint32_t outputNode,
    const std::array<std::string, 2>& outputPorts,
    const uint32_t inputNode,
    const std::array<std::string, 2>& inputPorts,
    std::array<pw_link*, 2>& links) {
    if (outputNode == PW_ID_ANY || inputNode == PW_ID_ANY || outputPorts[0].empty() ||
        outputPorts[1].empty() || inputPorts[0].empty() || inputPorts[1].empty()) {
        return false;
    }

    std::array<pw_link*, 2> created{};
    std::array<spa_hook, 2> listeners{};
    std::array<bool, 2> listenerAdded{};
    std::array<LinkProxyState, 2> states{};
    bool requestsCreated = true;

    pw_thread_loop_lock(loop);
    for (std::size_t channel = 0; channel < created.size(); ++channel) {
        const std::string outputNodeId = std::to_string(outputNode);
        const std::string inputNodeId = std::to_string(inputNode);
        pw_properties* properties = pw_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, outputNodeId.c_str(),
            PW_KEY_LINK_OUTPUT_PORT, outputPorts[channel].c_str(),
            PW_KEY_LINK_INPUT_NODE, inputNodeId.c_str(),
            PW_KEY_LINK_INPUT_PORT, inputPorts[channel].c_str(),
            nullptr
        );
        if (properties == nullptr) {
            requestsCreated = false;
            break;
        }

        created[channel] = static_cast<pw_link*>(pw_core_create_object(
            core, "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &properties->dict, 0));
        pw_properties_free(properties);
        if (created[channel] == nullptr) {
            requestsCreated = false;
            break;
        }

        states[channel].loop = loop;
        pw_proxy_add_listener(reinterpret_cast<pw_proxy*>(created[channel]), &listeners[channel],
                               &link_proxy_events, &states[channel]);
        listenerAdded[channel] = true;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (requestsCreated && std::chrono::steady_clock::now() < deadline &&
           !states[0].failed && !states[1].failed && (!states[0].bound || !states[1].bound)) {
        pw_thread_loop_timed_wait(loop, 1);
    }

    const bool success = requestsCreated && states[0].bound && states[1].bound &&
                         !states[0].failed && !states[1].failed;
    for (std::size_t channel = 0; channel < listeners.size(); ++channel) {
        if (listenerAdded[channel]) {
            spa_hook_remove(&listeners[channel]);
        }
    }
    if (!success) {
        for (std::size_t channel = 0; channel < created.size(); ++channel) {
            if (!states[channel].error.empty()) {
                Log::error("PipeWire rejected stereo link " + std::to_string(channel) + ": " + states[channel].error);
            }
            if (created[channel] != nullptr) {
                pw_core_destroy(core, created[channel]);
            }
        }
        pw_thread_loop_unlock(loop);
        if (requestsCreated && !states[0].failed && !states[1].failed) {
            Log::error("Timed out waiting for PipeWire to bind both stereo links");
        }
        return false;
    }

    links = created;
    pw_thread_loop_unlock(loop);
    return true;
}

void PipeWireContext::addHardwareNode(const uint32_t id, const spa_dict* props) {
    const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char* virtualNode = spa_dict_lookup(props, PW_KEY_NODE_VIRTUAL);
    const char* factoryName = spa_dict_lookup(props, PW_KEY_FACTORY_NAME);
    const char* objectPath = spa_dict_lookup(props, PW_KEY_OBJECT_PATH);
    const char* deviceId = spa_dict_lookup(props, PW_KEY_DEVICE_ID);
    if (mediaClass == nullptr || name == nullptr ||
        (virtualNode != nullptr && strcmp(virtualNode, "true") == 0) ||
        (factoryName != nullptr && strcmp(factoryName, "support.null-audio-sink") == 0) ||
        (objectPath == nullptr && deviceId == nullptr)) {
        return;
    }

    const bool isInput = strcmp(mediaClass, "Audio/Source") == 0;
    if (!isInput && strcmp(mediaClass, "Audio/Sink") != 0) {
        return;
    }

    const char* description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    const char* nickname = spa_dict_lookup(props, PW_KEY_NODE_NICK);
    HardwareAudioDevice device{
        .name = name,
        .description = description != nullptr ? description : (nickname != nullptr ? nickname : name),
        .isInput = isInput,
        .nodeId = id,
    };

    pw_thread_loop_lock(loop);
    const auto existing = std::find_if(hardwareDevices.begin(), hardwareDevices.end(), [&device](const HardwareAudioDevice& candidate) {
        return candidate.name == device.name;
    });
    if (existing == hardwareDevices.end()) {
        Log::logAsync(LogLevel::INFO, "Discovered hardware audio " + std::string(isInput ? "input: " : "output: ") + device.name);
        hardwareDevices.push_back(std::move(device));
    } else {
        existing->description = std::move(device.description);
        existing->isInput = device.isInput;
        existing->nodeId = device.nodeId;
    }
    pw_thread_loop_signal(loop, false);
    pw_thread_loop_unlock(loop);
}

void PipeWireContext::addHardwarePort(const uint32_t id, const spa_dict* props) {
    const char* nodeIdValue = spa_dict_lookup(props, PW_KEY_NODE_ID);
    const char* channel = spa_dict_lookup(props, PW_KEY_AUDIO_CHANNEL);
    const char* portName = spa_dict_lookup(props, PW_KEY_PORT_NAME);
    const char* portDirection = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
    const char* isMonitor = spa_dict_lookup(props, PW_KEY_PORT_MONITOR);
    if (nodeIdValue == nullptr || channel == nullptr || portName == nullptr || portDirection == nullptr ||
        (isMonitor != nullptr && strcmp(isMonitor, "true") == 0)) {
        return;
    }

    uint32_t nodeId;
    try {
        nodeId = static_cast<uint32_t>(std::stoul(nodeIdValue));
    } catch (const std::exception&) {
        return;
    }

    pw_thread_loop_lock(loop);
    const auto device = std::find_if(hardwareDevices.begin(), hardwareDevices.end(), [nodeId](const HardwareAudioDevice& candidate) {
        return candidate.nodeId == nodeId;
    });
    const char* expectedDirection = device != hardwareDevices.end() && device->isInput ? "out" : "in";
    if (device != hardwareDevices.end() && strcmp(portDirection, expectedDirection) == 0) {
        if (strcmp(channel, "FL") == 0) {
            device->leftPort = id;
            device->leftPortName = portName;
        } else if (strcmp(channel, "FR") == 0) {
            device->rightPort = id;
            device->rightPortName = portName;
        }
        pw_thread_loop_signal(loop, false);
    }
    pw_thread_loop_unlock(loop);
}

void PipeWireContext::removeRegistryGlobal(const uint32_t id) {
    pw_thread_loop_lock(loop);
    for (VirtualChannel* channel : virtualChannels) {
        if (channel == nullptr) {
            continue;
        }
        for (StreamContext* stream : {const_cast<StreamContext*>(channel->getSource()), const_cast<StreamContext*>(channel->getSink())}) {
            if (stream->leftPort == id) {
                stream->leftPort = PW_ID_ANY;
                stream->leftPortName.clear();
            }
            if (stream->rightPort == id) {
                stream->rightPort = PW_ID_ANY;
                stream->rightPortName.clear();
            }
        }
    }
    for (HardwareAudioDevice& device : hardwareDevices) {
        if (device.leftPort == id) {
            device.leftPort = PW_ID_ANY;
            device.leftPortName.clear();
        }
        if (device.rightPort == id) {
            device.rightPort = PW_ID_ANY;
            device.rightPortName.clear();
        }
    }
    const auto removedDevice = std::find_if(hardwareDevices.begin(), hardwareDevices.end(), [id](const HardwareAudioDevice& device) {
        return device.nodeId == id;
    });
    if (removedDevice != hardwareDevices.end()) {
        const std::string removedName = removedDevice->name;
        hardwareDevices.erase(removedDevice);

        auto connection = hardwareConnections.begin();
        while (connection != hardwareConnections.end()) {
            if ((connection->source.isHardware && connection->source.name == removedName) ||
                (connection->destination.isHardware && connection->destination.name == removedName)) {
                for (pw_link* link : connection->links) {
                    if (link != nullptr) {
                        pw_core_destroy(core, link);
                    }
                }
                connection = hardwareConnections.erase(connection);
            } else {
                ++connection;
            }
        }
    }
    pw_thread_loop_unlock(loop);
}

std::vector<HardwareAudioDevice> PipeWireContext::getHardwareDevices() const {
    pw_thread_loop_lock(loop);
    const auto devices = hardwareDevices;
    pw_thread_loop_unlock(loop);
    return devices;
}

std::vector<AudioConnection> PipeWireContext::getHardwareConnections() const {
    pw_thread_loop_lock(loop);
    std::vector<AudioConnection> connections;
    connections.reserve(hardwareConnections.size());
    for (const ManagedHardwareConnection& connection : hardwareConnections) {
        connections.push_back({connection.source, connection.destination});
    }
    pw_thread_loop_unlock(loop);
    return connections;
}

bool PipeWireContext::setAudioConnection(
    const AudioEndpoint& source,
    const AudioEndpoint& destination,
    const bool connected) {
    if (source.name.empty() || destination.name.empty() || source.name == destination.name ||
        (!source.isHardware && !destination.isHardware)) {
        return false;
    }

    pw_thread_loop_lock(loop);
    auto existing = std::find_if(hardwareConnections.begin(), hardwareConnections.end(), [&source, &destination](const ManagedHardwareConnection& connection) {
        return connection.source.name == source.name && connection.source.isHardware == source.isHardware &&
               connection.destination.name == destination.name && connection.destination.isHardware == destination.isHardware;
    });
    if (!connected) {
        if (existing == hardwareConnections.end()) {
            pw_thread_loop_unlock(loop);
            return false;
        }
        for (pw_link* link : existing->links) {
            if (link != nullptr) {
                pw_core_destroy(core, link);
            }
        }
        hardwareConnections.erase(existing);
        pw_thread_loop_unlock(loop);
        return true;
    }
    if (existing != hardwareConnections.end()) {
        pw_thread_loop_unlock(loop);
        return true;
    }
    pw_thread_loop_unlock(loop);

    const auto virtualChannels = getChannels();
    const auto hardwareDevices = getHardwareDevices();
    const auto resolveNode = [&](const AudioEndpoint& endpoint, const bool output) -> std::optional<HardwareAudioDevice> {
        if (!endpoint.isHardware) {
            const auto channel = std::find_if(virtualChannels.begin(), virtualChannels.end(), [&endpoint](const VirtualChannel* candidate) {
                return candidate != nullptr && candidate->name == endpoint.name;
            });
            if (channel == virtualChannels.end()) {
                return std::nullopt;
            }
            const StreamContext* stream = output ? (*channel)->getSink() : (*channel)->getSource();
            if (stream == nullptr) {
                return std::nullopt;
            }
            return HardwareAudioDevice{
                .name = endpoint.name,
                .description = (*channel)->description,
                .isInput = !output,
                .nodeId = pw_stream_get_node_id(stream->stream),
                .leftPort = stream->leftPort,
                .rightPort = stream->rightPort,
                .leftPortName = stream->leftPortName,
                .rightPortName = stream->rightPortName,
            };
        }

        const auto device = std::find_if(hardwareDevices.begin(), hardwareDevices.end(), [&endpoint, output](const HardwareAudioDevice& candidate) {
            return candidate.name == endpoint.name && candidate.isInput == output;
        });
        return device == hardwareDevices.end() ? std::nullopt : std::optional<HardwareAudioDevice>(*device);
    };

    const auto sourceNode = resolveNode(source, true);
    const auto destinationNode = resolveNode(destination, false);
    if (!sourceNode || !destinationNode || sourceNode->nodeId == PW_ID_ANY ||
        destinationNode->nodeId == PW_ID_ANY || sourceNode->leftPort == PW_ID_ANY ||
        sourceNode->rightPort == PW_ID_ANY || destinationNode->leftPort == PW_ID_ANY ||
        destinationNode->rightPort == PW_ID_ANY || sourceNode->leftPortName.empty() ||
        sourceNode->rightPortName.empty() || destinationNode->leftPortName.empty() ||
        destinationNode->rightPortName.empty()) {
        Log::error("Audio route endpoints are not available as stereo source/sink ports");
        return false;
    }

    ManagedHardwareConnection connection{.source = source, .destination = destination};
    const std::array<std::string, 2> outputPorts{sourceNode->leftPortName, sourceNode->rightPortName};
    const std::array<std::string, 2> inputPorts{destinationNode->leftPortName, destinationNode->rightPortName};
    pw_thread_loop_lock(loop);
    existing = std::find_if(hardwareConnections.begin(), hardwareConnections.end(), [&source, &destination](const ManagedHardwareConnection& current) {
        return current.source.name == source.name && current.source.isHardware == source.isHardware &&
               current.destination.name == destination.name && current.destination.isHardware == destination.isHardware;
    });
    if (existing != hardwareConnections.end()) {
        pw_thread_loop_unlock(loop);
        return true;
    }
    pw_thread_loop_unlock(loop);

    const bool success = createStereoLinks(sourceNode->nodeId, outputPorts, destinationNode->nodeId,
                                           inputPorts, connection.links);
    pw_thread_loop_lock(loop);
    if (!success) {
        pw_thread_loop_unlock(loop);
        Log::error("Failed to establish PipeWire hardware route " + source.name + " -> " + destination.name);
        return false;
    } else {
        hardwareConnections.push_back(std::move(connection));
        Log::info("Created PipeWire route " + source.name + ":" + outputPorts[0] + "/" + outputPorts[1] +
                  " -> " + destination.name + ":" + inputPorts[0] + "/" + inputPorts[1]);
    }
    pw_thread_loop_unlock(loop);
    return success;
}

StreamContext* PipeWireContext::findStreamContext(uint32_t nodeId) const {
    for (const VirtualChannel* channel : virtualChannels) {
        if (channel == nullptr)
            continue;

        auto* source =
            const_cast<StreamContext*>(channel->getSource());

        auto* sink =
            const_cast<StreamContext*>(channel->getSink());

        if (source->nodeId == nodeId)
            return source;

        if (sink->nodeId == nodeId)
            return sink;
    }

    return nullptr;
}

std::vector<VirtualChannel*> PipeWireContext::getChannels() const {
    return virtualChannels;
}