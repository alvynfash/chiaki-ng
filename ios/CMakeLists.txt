add_library(fash SHARED
	deckstation-ios-runtime.h
	deckstation-ios-runtime.mm
	deckstation-ios-video-decoder.hpp
	deckstation-ios-video-decoder.mm
	deckstation-ios-audio-decoder.hpp
	deckstation-ios-audio-decoder.mm)

set_target_properties(fash PROPERTIES
	FRAMEWORK TRUE
	FRAMEWORK_VERSION A
	MACOSX_FRAMEWORK_IDENTIFIER com.tribestick.deckstation.fash
	OUTPUT_NAME Fash
	PUBLIC_HEADER deckstation-ios-runtime.h
	XCODE_ATTRIBUTE_DEFINES_MODULE NO
	XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
	XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
	XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.tribestick.deckstation.fash)

target_compile_features(fash PRIVATE cxx_std_17)
target_link_libraries(fash PRIVATE
	chiaki-core-headless
	"-framework CoreMedia"
	"-framework CoreVideo"
	"-framework Foundation"
	"-framework VideoToolbox")

# These cloud entry points are loaded by Dart FFI rather than referenced by
# the Objective-C++ runtime. Keep them as linker roots so Apple's dead-strip
# pass includes the provisioning and catalog objects in Fash.framework.
target_link_options(fash PRIVATE
  "LINKER:-u,_chiaki_cloud_provision_session_json"
  "LINKER:-u,_chiaki_cloud_provision_json_free"
  "LINKER:-u,_chiaki_cloudcatalog_fetch_unified_json"
  "LINKER:-u,_chiaki_cloudcatalog_json_free")
target_include_directories(fash PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
