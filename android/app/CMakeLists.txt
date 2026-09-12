
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

# Dart opens the cloud-catalog and cloud-provisioning ABIs through FFI at
# runtime, so no Android-native object references these functions directly.
# Keep the archive members that define them when lld performs dead-code
# elimination; otherwise the final libfash.so is missing the symbols even
# though the sources were compiled successfully.
target_link_options(chiaki-jni PRIVATE
        "-Wl,--undefined=chiaki_cloud_provision_session_json"
        "-Wl,--undefined=chiaki_cloud_provision_session_json_with_progress"
        "-Wl,--undefined=chiaki_cloud_provision_json_free"
        "-Wl,--undefined=chiaki_cloudcatalog_fetch_unified_json"
        "-Wl,--undefined=chiaki_cloudcatalog_json_free")

find_library(ANDROID_LIB_LOG log)
find_library(ANDROID_LIB_MEDIANDK mediandk)
find_library(ANDROID_LIB_ANDROID android)
target_link_libraries(chiaki-jni "${ANDROID_LIB_LOG}" "${ANDROID_LIB_MEDIANDK}" "${ANDROID_LIB_ANDROID}")

find_package (oboe REQUIRED CONFIG)
target_link_libraries(chiaki-jni oboe::oboe)
