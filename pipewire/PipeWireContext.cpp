#include "PipeWireContext.h"

#include <pipewire/pipewire.h>
#include "../logger/Log.h"

PipeWireContext::PipeWireContext() {
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

    virtualChannels.push_back(new VirtualChannel(core, name, description));
    pw_thread_loop_unlock(loop);
    return true;
}
