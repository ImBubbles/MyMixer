#include <pipewire/pipewire.h>
#include <string>

#include "VirtualChannel.h"
#include "../util/UtilString.h"

#pragma once

namespace StreamFactory {
    // TODO remove monitor channels from source node
    static pw_properties* createSourceProperties(const std::string& name, const std::string& description) {
        const std::string nodeName = UtilString::asLowercase("mymixer_" + name + "_source");
        pw_properties* props = pw_properties_new(
            PW_KEY_NODE_NAME, nodeName.c_str(),
            PW_KEY_NODE_DESCRIPTION, (description + " Input").c_str(),
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CLASS, "Audio/Sink",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_NODE_VIRTUAL, "true",
            nullptr
        );

        return props;
    }

    static pw_properties* createSinkProperties(const std::string& name, const std::string& description) {
        const std::string nodeName = UtilString::asLowercase("mymixer_" + name + "_sink");
        pw_properties* props = pw_properties_new(
            PW_KEY_NODE_NAME, nodeName.c_str(),
            PW_KEY_NODE_DESCRIPTION, (description + " Output").c_str(),
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CLASS, "Audio/Source",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_NODE_VIRTUAL, "true",
            nullptr
        );

        return props;
    }
    static pw_stream* createSourceStream(pw_core* core, const std::string& name, const std::string& description) {
        return pw_stream_new(core, description.c_str(), createSourceProperties(name, description));
    }
    static pw_stream* createSinkStream(pw_core* core, const std::string& name, const std::string& description) {
        return pw_stream_new(core, description.c_str(), createSinkProperties(name, description));
    }
    static const spa_audio_info_raw* createAudioInfoRaw(const uint32_t rate) {
        const auto* result = new spa_audio_info_raw {
            .format = SPA_AUDIO_FORMAT_F32,
            .rate = rate,
            .channels = 2,
            .position = {
                SPA_AUDIO_CHANNEL_FL,
                SPA_AUDIO_CHANNEL_FR
            }
        };
        return result;
    }
    static const spa_audio_info_raw* createAudioInfoRawDefault() {
        return createAudioInfoRaw(48000);
    }
    static VirtualChannel* createVirtualChannel(PipeWireContext* context, const std::string& name, const std::string& description) {
        pw_thread_loop_lock(context->loop);
        const auto vc = new VirtualChannel(
            context,
            name,
            description,
            createSourceStream(context->core, name, description),
            createSinkStream(context->core, name, description)
            );
        pw_thread_loop_unlock(context->loop);
        return vc;
    }
}