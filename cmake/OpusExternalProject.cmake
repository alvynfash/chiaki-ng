include(FetchContent)

set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(opus
	URL https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz
	URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(opus)

# Chiaki uses the installed-style <opus/opus.h> include path. Recreate that
# layout for the fetched source tree without patching upstream libopus.
set(OPUS_PREFIX_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/opus-prefix-include")
file(MAKE_DIRECTORY "${OPUS_PREFIX_INCLUDE_DIR}")
if(NOT EXISTS "${OPUS_PREFIX_INCLUDE_DIR}/opus")
	file(CREATE_LINK "${opus_SOURCE_DIR}/include"
		"${OPUS_PREFIX_INCLUDE_DIR}/opus" SYMBOLIC)
endif()
