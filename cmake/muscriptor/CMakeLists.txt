# MuScriptor sub-build (arm64 only): muscriptor.cpp (MIT, pinned commit) + ggml, wrapped by MuScriptorBridge.cpp (C++23),
# merged into one static archive. Built as an ExternalProject by the main CMakeLists, which lipo's it with the x86_64 stub.
cmake_minimum_required(VERSION 3.25)
project(tonetrace_muscriptor LANGUAGES C CXX)

include(FetchContent)
set(MUSCRIPTOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MUSCRIPTOR_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(MUSCRIPTOR_GGML_FP32_ACCUM OFF CACHE BOOL "" FORCE)
set(MUSCRIPTOR_METAL_PRECOMPILED OFF CACHE BOOL "" FORCE)  # shaders compile at first use; no Metal toolchain needed
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(muscriptor
  GIT_REPOSITORY https://github.com/DamRsn/muscriptor.cpp.git
  GIT_TAG 34a01513f3edfacf21fba7849859b703a6e2a9df
  SOURCE_SUBDIR cpp)
FetchContent_MakeAvailable(muscriptor)

add_library(tonetrace_msbridge STATIC ${TT_SOURCE_DIR}/src/analysis/ml/muscriptor/MuScriptorBridge.cpp)
target_include_directories(tonetrace_msbridge PRIVATE ${TT_SOURCE_DIR}/src)
target_compile_features(tonetrace_msbridge PRIVATE cxx_std_23)
target_link_libraries(tonetrace_msbridge PRIVATE muscriptor_ggml)

# One archive with everything the bridge needs (the main build cannot see this tree's targets).
set(MERGED ${CMAKE_BINARY_DIR}/libtonetrace_muscriptor_arm64.a)
add_custom_command(OUTPUT ${MERGED}
  COMMAND libtool -static -o ${MERGED}
    $<TARGET_FILE:tonetrace_msbridge> $<TARGET_FILE:muscriptor_ggml> $<TARGET_FILE:pffft>
    $<TARGET_FILE:ggml> $<TARGET_FILE:ggml-base> $<TARGET_FILE:ggml-cpu> $<TARGET_FILE:ggml-metal>
  DEPENDS tonetrace_msbridge muscriptor_ggml pffft ggml ggml-base ggml-cpu ggml-metal
  COMMENT "Merging the MuScriptor archives")
add_custom_target(merged ALL DEPENDS ${MERGED})
