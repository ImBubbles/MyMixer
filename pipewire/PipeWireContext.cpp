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

}

static void registry_global_remove(void *data, const uint32_t id)
{
    printf("Removed object %u\n", id);
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

bool PipeWireContext::registerChannel(VirtualChannel* channel) {
    if (doesChannelExist(channel->name)) {
        return false;
    }
    if (!channel->waitForNodeIds()) {
        pw_thread_loop_lock(loop);
        delete channel;
        pw_thread_loop_unlock(loop);
        return false;
    }
    pw_thread_loop_lock(loop);
    virtualChannels.push_back(channel);
    return true;
}

bool PipeWireContext::createChannel(const std::string& name, const std::string& description) {
    // Also handles registering using PipeWireContext#registerChannel()
    if (doesChannelExist(name)) {
        // though register method also handles this, I'd rather do a name check before making a new object
        return false;
    }
    pw_thread_loop_lock(loop);
    auto* channel = StreamFactory::createVirtualChannel(this, name, description);
    pw_thread_loop_unlock(loop);

    return registerChannel(channel);
}