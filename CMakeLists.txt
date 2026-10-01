cmake_minimum_required(VERSION 3.31)

project(MyMixer CXX)

# JSON LIBRARY
include(FetchContent)
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

set(CMAKE_CXX_STANDARD 26)

# SHOULD BUILD HEADERS
option(MY_MIXER_BUILD_HEADERS "Build headers from template files" ON)

if(MY_MIXER_BUILD_HEADERS)
    # INPUTS
    set(SETTINGS_JSON ${CMAKE_SOURCE_DIR}/environment/defaults/settings.json)
    set(CONFIG_JSON ${CMAKE_SOURCE_DIR}/environment/defaults/config.json)

    # OUTPUTS
    set(SETTINGS_HEADER environment/defaults/settings.h)
    set(CONFIG_HEADER environment/defaults/config.json)

    # GENERATE CONFIG JSON
    add_custom_command(
            OUTPUT ${SETTINGS_HEADER}
            COMMAND xxd -i -n resource_default_settings ${SETTINGS_JSON} > ${SETTINGS_HEADER}
            DEPENDS ${SETTINGS_JSON}
    )

    # GENERATE EXAMPLE HTML
    add_custom_command(
            OUTPUT ${CONFIG_HEADER}
            COMMAND xxd -i -n resource_default_config ${CONFIG_JSON} > ${CONFIG_HEADER}
            DEPENDS ${CONFIG_JSON}
    )
endif()

add_custom_target(generate_default_headers DEPENDS ${SETTINGS_HEADER} ${CONFIG_HEADER})

add_executable(MyMixer
        main.cpp
        logger/Log.cpp
        pipewire/PipeWireContext.cpp
        Application.cpp
        pipewire/VirtualChannel.cpp
        util/UtilString.cpp
        config/Config.cpp
        environment/Environment.cpp
        ${CONFIG_HEADER}
        ${EXAMPLE_HEADER}
        #util/UtilNumber.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

target_include_directories(MyMixer PRIVATE
        ${PIPEWIRE_INCLUDE_DIRS}
)

target_link_directories(MyMixer PRIVATE
        ${PIPEWIRE_LIBRARY_DIRS}
)

target_link_libraries(MyMixer PRIVATE
        nlohmann_json::nlohmann_json
        ${PIPEWIRE_LIBRARIES}
)

target_compile_options(MyMixer PRIVATE
        ${PIPEWIRE_CFLAGS_OTHER}
)