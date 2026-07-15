#pragma once
#include <string>
#include <vector>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format.h>
#include <spa/pod/builder.h>

#include "../util/UtilString.h"

struct VirtualChannel {
    explicit VirtualChannel(pw_core* core, const std::string& name, const std::string& description);
    ~VirtualChannel();
    std::string name;
    std::string description;

    static bool waitForNodeIds(pw_stream* input, pw_stream* output, const std::string& name);
    bool waitForNodeIds() const;
private:
    pw_core* core;
    pw_stream* input;
    pw_stream* output;
};

namespace StreamFactory {
    static pw_stream* createInputNode(pw_core* core, const std::string& name, const std::string& description) {
        const std::string nodeName = UtilString::asLowercase("mymixer_" + name + "_input");
        pw_properties* props = pw_properties_new(
            PW_KEY_NODE_NAME, nodeName.c_str(),
            PW_KEY_NODE_DESCRIPTION, (description + " Input").c_str(),
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_NODE_VIRTUAL, "true",
            nullptr
        );

        return pw_stream_new(core, nodeName.c_str(), props);
    }

    static pw_stream* createOutputNode(pw_core* core, const std::string& name, const std::string& description) {
        const std::string nodeName = UtilString::asLowercase("mymixer_" + name + "_output");
        pw_properties* props = pw_properties_new(
            PW_KEY_NODE_NAME, nodeName.c_str(),
            PW_KEY_NODE_DESCRIPTION, (description + " Output").c_str(),
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_NODE_VIRTUAL, "true",
            nullptr
        );

        return pw_stream_new(core, nodeName.c_str(), props);
    }
}