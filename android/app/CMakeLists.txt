
cmake_minimum_required(VERSION 3.2)

set(CMAKE_CXX_STANDARD 14)
option(CHIAKI_ANDROID_DECKSTATION_BRIDGE
        "Build the DeckStation Android host bridge" OFF)

add_library(chiaki-jni SHARED
        src/main/cpp/chiaki-jni.c
        src/main/cpp/log.h
        src/main/cpp/log.c
        src/main/cpp/video-decoder.h
        src/main/cpp/video-decoder.c
        src/main/cpp/audio-decoder.h
        src/main/cpp/audio-decoder.c
        src/main/cpp/audio-output.h
        src/main/cpp/audio-output.cpp
        src/main/cpp/circular-fifo.hpp)
if(CHIAKI_ANDROID_DECKSTATION_BRIDGE)
    target_sources(chiaki-jni PRIVATE
            src/main/cpp/deckstation-jni.h
            src/main/cpp/deckstation-jni.c)
    target_compile_definitions(chiaki-jni PRIVATE
            CHIAKI_ANDROID_DECKSTATION_BRIDGE=1)
endif()
set_target_properties(chiaki-jni PROPERTIES OUTPUT_NAME fash)
target_link_libraries(chiaki-jni chiaki-lib)

find_library(ANDROID_LIB_LOG log)
find_library(ANDROID_LIB_MEDIANDK mediandk)
find_library(ANDROID_LIB_ANDROID android)
target_link_libraries(chiaki-jni "${ANDROID_LIB_LOG}" "${ANDROID_LIB_MEDIANDK}" "${ANDROID_LIB_ANDROID}")

find_package (oboe REQUIRED CONFIG)
target_link_libraries(chiaki-jni oboe::oboe)
