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
    if (strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
        const char* nodeIdStr = spa_dict_lookup(props, PW_KEY_NODE_ID);
        if (nodeIdStr == nullptr)
            return;

        uint32_t nodeId = std::stoul(nodeIdStr);
        StreamContext* stream = ctx->findStreamContext(nodeId);
        if (stream == nullptr)
            return;

        const char* channel = spa_dict_lookup(props, "audio.channel");
        if (channel == nullptr)
            return;

        if (strcmp(channel, "FL") == 0) {
            stream->leftPort = id;
        } else if (strcmp(channel, "FR") == 0) {
            stream->rightPort = id;
        }
        Log::debug("Set port " + std::to_string(id) + " for node " + std::to_string(nodeId) + " channel " + std::string(channel));
        pw_thread_loop_signal(ctx->loop, false);
        return;
    }

    if (strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        const char* outNodeStr = spa_dict_lookup(props, "link.output.node");
        const char* outPortStr = spa_dict_lookup(props, "link.output.port");
        const char* inNodeStr = spa_dict_lookup(props, "link.input.node");
        const char* inPortStr = spa_dict_lookup(props, "link.input.port");

        if (outNodeStr && outPortStr && inNodeStr && inPortStr) {
            uint32_t outNode = std::stoul(outNodeStr);
            uint32_t outPort = std::stoul(outPortStr);
            uint32_t inNode = std::stoul(inNodeStr);
            uint32_t inPort = std::stoul(inPortStr);
            Log::info("Registry: link " + std::to_string(id) + " -> " + std::to_string(outNode) + ":" + std::to_string(outPort) + " -> " + std::to_string(inNode) + ":" + std::to_string(inPort));
        } else {
            Log::debug("Registry: link object " + std::to_string(id) + " published (partial props)");
        }
        return;
    }
}

static void registry_global_remove(void *data, const uint32_t id)
{
    //printf("Removed object %u\n", id);
}



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
        return nullptr;
    }
    /* Add early so registry events can find the StreamContext while ports are published. */
    virtualChannels.push_back(channel);
    pw_thread_loop_unlock(loop);

    if (!channel->waitForNodeIds() || !channel->waitForPorts()) {
        pw_thread_loop_lock(loop);
        /* Remove from list and cleanup */
        auto it = std::find(virtualChannels.begin(), virtualChannels.end(), channel);
        if (it != virtualChannels.end())
            virtualChannels.erase(it);
        delete channel;
        pw_thread_loop_unlock(loop);
        Log::error("Either nodes or ports failed to resolve.");
        return nullptr;
    }

    return channel;
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
    // Prefer using the stream's node id and named ports instead of raw port ids.
    // This attempts to create links that reference exported port names from streams.
    uint32_t outputNodeId = pw_stream_get_node_id(source->stream);
    uint32_t inputNodeId = pw_stream_get_node_id(sink->stream);

    Log::debug("linkStreams using node ids from streams: out=" + std::to_string(outputNodeId) + " in=" + std::to_string(inputNodeId));

    const char* portNames[2][2] = {
        { "output_FL", "input_FL" },
        { "output_FR", "input_FR" }
    };

    bool ok = true;
    for (int channel = 0; channel < 2; ++channel) {
        pw_thread_loop_lock(loop);
        pw_properties* props = pw_properties_new(
            PW_KEY_LINK_OUTPUT_NODE, std::to_string(outputNodeId).c_str(),
            PW_KEY_LINK_INPUT_NODE, std::to_string(inputNodeId).c_str(),
            PW_KEY_LINK_OUTPUT_PORT, portNames[channel][0],
            PW_KEY_LINK_INPUT_PORT, portNames[channel][1],
            nullptr
        );

        if (props == nullptr) {
            pw_thread_loop_unlock(loop);
            Log::error("Failed to create link properties for channel " + std::to_string(channel));
            ok = false;
            continue;
        }

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
            Log::error("Failed to create PipeWire link for channel " + std::to_string(channel));
            ok = false;
            continue;
        }

        Log::info("Link created for channel " + std::to_string(channel));
    }

    return ok;
}

bool PipeWireContext::linkChannels(const VirtualChannel* source, const VirtualChannel* sink) const {
    Log::debug("linkChannels: source='" + source->name + "' sink='" + sink->name + "'");
    // Use VirtualChannel::connect which implements the link-factory usage
    // with named port properties and manages pw_link lifetimes.
    auto* nonConstSource = const_cast<VirtualChannel*>(source);
    return nonConstSource->connect(nonConstSource, sink);
}

StreamContext* PipeWireContext::findStreamContext(uint32_t nodeId) const {
    for (VirtualChannel* channel : virtualChannels) {
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