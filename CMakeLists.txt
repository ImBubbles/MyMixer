cmake_minimum_required(VERSION 3.31)

project(MyMixer CXX)

set(CMAKE_CXX_STANDARD 26)

add_executable(MyMixer
        main.cpp
        logger/Log.cpp
        pipewire/PipeWireContext.cpp
        Application.cpp
        Application.h
        pipewire/VirtualChannel.cpp
        util/UtilString.cpp
        pipewire/StreamFactory.h
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
        ${PIPEWIRE_LIBRARIES}
)

target_compile_options(MyMixer PRIVATE
        ${PIPEWIRE_CFLAGS_OTHER}
)