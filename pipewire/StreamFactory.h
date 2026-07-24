#include <pipewire/pipewire.h>
#include <string>

#include "VirtualChannel.h"
#include "../util/UtilString.h"

#pragma once

namespace StreamFactory {
    static pw_properties* createSourceProperties(const std::string& name, const std::string& description) {
        const std::string nodeName = UtilString::asLowercase("mymixer_" + name + "_input");
        pw_properties* props = pw_properties_new(
            PW_KEY_NODE_NAME, nodeName.c_str(),
            PW_KEY_NODE_DESCRIPTION, (description + " Input").c_str(),
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_NODE_VIRTUAL, "true",
            nullptr
        );

        return props;
    }

    static pw_properties* createSinkProperties(const std::string& name, const std::string& description) {
        const std::string nodeName = UtilString::asLowercase("mymixer_" + name + "_output");
        pw_properties* props = pw_properties_new(
            PW_KEY_NODE_NAME, nodeName.c_str(),
            PW_KEY_NODE_DESCRIPTION, (description + " Output").c_str(),
            PW_KEY_MEDIA_TYPE, "Audio",
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
    static VirtualChannel* createVirtualChannel(PipeWireContext* context, const std::string& name, const std::string& description) {
        const auto vc = new VirtualChannel(
            context,
            name,
            description,
            createSourceStream(context->core, name, description),
            createSinkStream(context->core, name, description)
            );
        return vc;
    }
}