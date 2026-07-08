#pragma once
#include <string>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format.h>
#include <spa/pod/builder.h>

struct VirtualChannel {
    explicit VirtualChannel(pw_core* core, const std::string& name, const std::string& description);
    ~VirtualChannel();
    std::string name;
    std::string description;
private:
    static constexpr uint32_t sampleRate = 48000;
    static constexpr uint32_t channels = 2;

    pw_core* core;
    pw_stream* input;
    pw_stream* output;
    static const pw_stream_events inputStreamEvents;
    static const pw_stream_events outputStreamEvents;
    spa_hook inputListener;
    spa_hook outputListener;

    static const spa_pod* buildFormatParams(uint8_t* buffer, size_t size);
    static void onProcess(void* userdata);
    static void onStateChanged(
        void* userdata,
        enum pw_stream_state old,
        enum pw_stream_state state,
        const char* error
    );
};

namespace StreamFactory {
    static pw_stream* createInputNode(pw_core* core, const std::string& name, const std::string& description) {
        pw_properties* props = pw_properties_new(
            PW_KEY_NODE_NAME, (name + "_input").c_str(),
            PW_KEY_NODE_DESCRIPTION, (description + " Input").c_str(),
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_NODE_VIRTUAL, "true",
            nullptr
        );

        return pw_stream_new(core, (name + "_input").c_str(), props);
    }

    static pw_stream* createOutputNode(pw_core* core, const std::string& name, const std::string& description) {
        pw_properties* props = pw_properties_new(
            PW_KEY_NODE_NAME, (name + "_output").c_str(),
            PW_KEY_NODE_DESCRIPTION, (description + " Output").c_str(),
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_NODE_VIRTUAL, "true",
            nullptr
        );

        return pw_stream_new(core, (name + "_output").c_str(), props);
    }
}