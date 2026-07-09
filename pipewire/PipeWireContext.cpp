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

    pw_thread_loop_unlock(loop);
    Log::info("Created PipeWireContext");
}

PipeWireContext::~PipeWireContext() {
    Log::info("Destroying PipeWireContext");

    pw_thread_loop_lock(loop);
    for (const VirtualChannel* vc : virtualChannels) {
        if (vc == nullptr) {
            continue;
        }
        delete vc;
    }
    virtualChannels.clear();

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

bool PipeWireContext::createChannel(const std::string& name, const std::string& description) {
    pw_thread_loop_lock(loop);

    if (doesChannelExist(name)) {
        pw_thread_loop_unlock(loop);
        return false;
    }

    auto* channel = new VirtualChannel(core, name, description);
    virtualChannels.push_back(channel);
    pw_thread_loop_unlock(loop);

    if (!channel->waitForNodeIds()) {
        pw_thread_loop_lock(loop);
        auto it = std::find(virtualChannels.begin(), virtualChannels.end(), channel);
        if (it != virtualChannels.end()) {
            virtualChannels.erase(it);
        }
        pw_thread_loop_unlock(loop);
        delete channel;
        return false;
    }

    return true;
}

VirtualChannel* PipeWireContext::findChannel(const std::string& name) const {
    for (VirtualChannel* vc : virtualChannels) {
        if (vc != nullptr && vc->name == name) {
            return vc;
        }
    }
    return nullptr;
}

bool PipeWireContext::linkChannels(const std::string& from, const std::string& to) {
    pw_thread_loop_lock(loop);
    VirtualChannel* source = findChannel(from);
    const VirtualChannel* target = findChannel(to);
    if (source == nullptr || target == nullptr) {
        pw_thread_loop_unlock(loop);
        Log::error("Cannot link channels \"" + from + "\" and \"" + to + "\": channel not found");
        return false;
    }

    const bool result = VirtualChannel::connect(source, target);
    pw_thread_loop_unlock(loop);
    return result;
}

bool PipeWireContext::unlinkChannels(const std::string& from, const std::string& to) {
    pw_thread_loop_lock(loop);
    VirtualChannel* source = findChannel(from);
    const VirtualChannel* target = findChannel(to);
    if (source == nullptr || target == nullptr) {
        pw_thread_loop_unlock(loop);
        Log::error("Cannot unlink channels \"" + from + "\" and \"" + to + "\": channel not found");
        return false;
    }

    const bool result = VirtualChannel::disconnect(source, target);
    pw_thread_loop_unlock(loop);
    return result;
}
