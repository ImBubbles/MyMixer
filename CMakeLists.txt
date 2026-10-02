cmake_minimum_required(VERSION 3.31)

project(MyMixer CXX)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# JSON LIBRARY
include(FetchContent)
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

set(CMAKE_CXX_STANDARD 26)

# Generate headers embedding the default JSON resources.
option(MY_MIXER_BUILD_HEADERS "Build headers from template files" ON)

if(MY_MIXER_BUILD_HEADERS)
    find_program(XXD_EXECUTABLE xxd REQUIRED)

    set(DEFAULTS_JSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/environment/defaults")
        set(DEFAULTS_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/environment/generated/defaults")
    set(SETTINGS_JSON "${DEFAULTS_JSON_DIR}/settings.json")
    set(CONFIG_JSON "${DEFAULTS_JSON_DIR}/config.json")
    set(SETTINGS_HEADER "${DEFAULTS_GENERATED_DIR}/settings.h")
    set(CONFIG_HEADER "${DEFAULTS_GENERATED_DIR}/config.h")

    add_custom_command(
            OUTPUT ${SETTINGS_HEADER}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DEFAULTS_GENERATED_DIR}"
            COMMAND ${XXD_EXECUTABLE} -i -n resource_default_settings "${SETTINGS_JSON}" "${SETTINGS_HEADER}"
            DEPENDS ${SETTINGS_JSON}
            VERBATIM
    )

    add_custom_command(
            OUTPUT ${CONFIG_HEADER}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DEFAULTS_GENERATED_DIR}"
            COMMAND ${XXD_EXECUTABLE} -i -n resource_default_config "${CONFIG_JSON}" "${CONFIG_HEADER}"
            DEPENDS ${CONFIG_JSON}
            VERBATIM
    )

    add_custom_target(generate_default_headers DEPENDS ${SETTINGS_HEADER} ${CONFIG_HEADER})
endif()

add_executable(MyMixer
        main.cpp
        logger/Log.cpp
        pipewire/PipeWireContext.cpp
        Application.cpp
        pipewire/VirtualChannel.cpp
        util/UtilString.cpp
        config/Config.cpp
        environment/Environment.cpp
        ui/FlowLayout.cpp
        ui/MainWindow.cpp
        #util/UtilNumber.cpp
)

if(MY_MIXER_BUILD_HEADERS)
        target_sources(MyMixer PRIVATE ${SETTINGS_HEADER} ${CONFIG_HEADER})
        target_include_directories(MyMixer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        add_dependencies(MyMixer generate_default_headers)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

target_include_directories(MyMixer PRIVATE
        ${PIPEWIRE_INCLUDE_DIRS}
)

target_link_directories(MyMixer PRIVATE
        ${PIPEWIRE_LIBRARY_DIRS}
)

target_link_libraries(MyMixer PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        nlohmann_json::nlohmann_json
        ${PIPEWIRE_LIBRARIES}
)

target_compile_options(MyMixer PRIVATE
        ${PIPEWIRE_CFLAGS_OTHER}
)