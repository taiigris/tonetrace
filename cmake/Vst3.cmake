# VST3 build: Tonetrace.vst3 plus the VST3 host test. Needs the Steinberg VST3 SDK (MIT) in external/vst3sdk.
# See README for how to get it. Without the SDK this file only prints a notice.
set(TONETRACE_VST3_SDK_DIR "${CMAKE_SOURCE_DIR}/external/vst3sdk" CACHE PATH "Steinberg VST3 SDK checkout")
if(NOT EXISTS "${TONETRACE_VST3_SDK_DIR}/CMakeLists.txt")
  message(STATUS "VST3 SDK not found in ${TONETRACE_VST3_SDK_DIR}: skipping the VST3 targets")
  return()
endif()

# SDK options: no VSTGUI, no samples, nothing that is not needed. The validator is built on demand only.
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_UTILITIES OFF CACHE BOOL "" FORCE)
add_subdirectory("${TONETRACE_VST3_SDK_DIR}" vst3sdk EXCLUDE_FROM_ALL)
# Variables the SDK's helper functions read (they are only set inside the SDK's own directory scope).
set(SDK_ROOT "${TONETRACE_VST3_SDK_DIR}")
set(public_sdk_SOURCE_DIR "${TONETRACE_VST3_SDK_DIR}/public.sdk")

set(VST3_PLUGIN_SOURCES
  src/vst3/TonetraceProcessor.cpp src/vst3/TonetraceController.cpp src/vst3/TonetraceEntry.cpp src/vst3/TonetraceViewMac.mm)
smtg_add_vst3plugin_with_pkgname(Tonetrace_vst3 Tonetrace SOURCES_LIST ${VST3_PLUGIN_SOURCES})
target_include_directories(Tonetrace_vst3 PRIVATE src)
target_compile_features(Tonetrace_vst3 PUBLIC cxx_std_17)
target_link_libraries(Tonetrace_vst3 PRIVATE sdk tonetrace_ui tonetrace_ml tonetrace_core ${COREAUDIO})
# the categories of the Cocoa views are not referenced statically
target_link_options(Tonetrace_vst3 PRIVATE -Wl,-force_load,$<TARGET_FILE:tonetrace_ui>)
# The SDK's bundle helper only knows Xcode; the Makefile build gets an explicit Info.plist.
configure_file(${CMAKE_SOURCE_DIR}/src/vst3/Info.plist.in ${CMAKE_BINARY_DIR}/Info-vst3.plist @ONLY)
set_target_properties(Tonetrace_vst3 PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/Info-vst3.plist)
# Bundle icon (committed, rendered by tools/make_icon.sh): Contents/Resources/Tonetrace.icns, referenced by CFBundleIconFile.
add_custom_command(TARGET Tonetrace_vst3 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:Tonetrace_vst3>/Resources"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/assets/logo/Tonetrace.icns" "$<TARGET_BUNDLE_CONTENT_DIR:Tonetrace_vst3>/Resources/Tonetrace.icns")

# ---- headless VST3 host test (SDK hosting classes) ----
set(VST3_SRC "${TONETRACE_VST3_SDK_DIR}/public.sdk/source")
add_executable(tonetrace_hosttest_vst3 tools/hosttest_vst3.mm tools/HostStress.mm
  ${VST3_SRC}/common/memorystream.cpp ${VST3_SRC}/vst/hosting/plugprovider.cpp ${VST3_SRC}/vst/hosting/module_mac.mm
  ${VST3_SRC}/vst/moduleinfo/moduleinfoparser.cpp)
target_include_directories(tonetrace_hosttest_vst3 PRIVATE tests src ${TONETRACE_VST3_SDK_DIR})
target_compile_features(tonetrace_hosttest_vst3 PRIVATE cxx_std_17)
target_compile_options(tonetrace_hosttest_vst3 PRIVATE -fobjc-arc)
target_link_libraries(tonetrace_hosttest_vst3 PRIVATE sdk_hosting tonetrace_core ${APPKIT} ${FOUNDATION})
