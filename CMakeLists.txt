cmake_minimum_required(VERSION 3.20)
# Universal binary (Apple Silicon + Intel); override with -DCMAKE_OSX_ARCHITECTURES=arm64
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TONETRACE_VERSION_FILE LIMIT_COUNT 1)
project(Tonetrace VERSION ${TONETRACE_VERSION_FILE} LANGUAGES C CXX OBJCXX)
# One version source (VERSION): Info.plist, VST3 class info, UI, logger. The build hash is the git short sha.
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE TONETRACE_BUILD_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT TONETRACE_BUILD_HASH)
  set(TONETRACE_BUILD_HASH "nogit")
endif()
configure_file(${CMAKE_SOURCE_DIR}/src/core/Version.h.in ${CMAKE_BINARY_DIR}/generated/core/Version.h @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
enable_testing()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
add_compile_options($<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)

# ---- platform-neutral core (analysis, note model, MIDI, preview, state, kernel) ----
add_library(tonetrace_core STATIC
  src/analysis/SpectralFrames.cpp src/analysis/NoteAnalyzer.cpp src/analysis/KeyEstimator.cpp
  src/analysis/FftPortable.cpp src/analysis/FftAccelerate.cpp src/analysis/Hpss.cpp
  src/analysis/Spectrogram.cpp src/analysis/DrumFilter.cpp src/analysis/QuickGrab.cpp
  src/analysis/LowBand.cpp src/analysis/SustainGrab.cpp src/analysis/SustainRuns.cpp src/analysis/BeatHits.cpp
  src/analysis/LiveSpectrum.cpp src/analysis/LiveHarmonic.cpp src/analysis/WavePeaks.cpp
  src/analysis/PitchLines.cpp src/analysis/LineFrames.cpp src/analysis/LineTracker.cpp src/analysis/LineOnsets.cpp
  src/analysis/LineBuilder.cpp src/analysis/LiveLines.cpp
  src/analysis/ml/NeuralModel.cpp src/analysis/ml/Resampler.cpp src/analysis/ml/NoteDecoder.cpp src/analysis/ml/NeuralEngine.cpp
  src/analysis/ml/MuScriptorEngine.cpp
  src/render/GlowProfile.cpp
  src/midi/MidiFileWriter.cpp src/midi/ExportPlanner.cpp
  src/preview/PreviewSynth.cpp src/bridge/BridgeClient.cpp
  src/state/StateCodec.cpp src/state/UiSettings.cpp src/core/Logger.cpp src/core/Diagnostics.cpp
  src/core/NoteEdit.cpp src/core/Snapshot.cpp
  src/core/TonetraceKernel.cpp src/core/KernelModel.cpp src/core/KernelWorker.cpp src/core/KernelAudio.cpp)
target_include_directories(tonetrace_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
find_library(ACCELERATE Accelerate)
target_link_libraries(tonetrace_core PUBLIC ${ACCELERATE})
set_target_properties(tonetrace_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- neural engine: CoreML implementation (Basic Pitch, Apache-2.0) and the compiled model ----
find_library(COREML CoreML)
# ---- MuScriptor engine (muscriptor.cpp MIT + ggml; weights CC BY-NC, downloaded at run time, never bundled) ----
# muscriptor.cpp builds one architecture per tree: an arm64 sub-build (cmake/muscriptor, fetches the pinned sources) is
# lipo'd with an x86_64 stub that reports "needs Apple Silicon".
include(ExternalProject)
set(TONETRACE_MS_BUILD "${CMAKE_BINARY_DIR}/muscriptor-arm64")
set(TONETRACE_MS_ARM64 "${TONETRACE_MS_BUILD}/libtonetrace_muscriptor_arm64.a")
ExternalProject_Add(muscriptor_arm64
  SOURCE_DIR "${CMAKE_SOURCE_DIR}/cmake/muscriptor"
  BINARY_DIR "${TONETRACE_MS_BUILD}"
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
             -DTT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target merged --parallel
  INSTALL_COMMAND ""
  BUILD_ALWAYS ON
  BUILD_BYPRODUCTS "${TONETRACE_MS_ARM64}")
add_library(tonetrace_msstub STATIC src/analysis/ml/muscriptor/MuScriptorStub.cpp)
target_include_directories(tonetrace_msstub PRIVATE src)
set_target_properties(tonetrace_msstub PROPERTIES OSX_ARCHITECTURES x86_64)
set(TONETRACE_MS_FAT "${CMAKE_BINARY_DIR}/libtonetrace_muscriptor.a")
add_custom_command(OUTPUT "${TONETRACE_MS_FAT}"
  COMMAND lipo -create "${TONETRACE_MS_ARM64}" "$<TARGET_FILE:tonetrace_msstub>" -output "${TONETRACE_MS_FAT}"
  DEPENDS muscriptor_arm64 tonetrace_msstub "${TONETRACE_MS_ARM64}" COMMENT "Joining the MuScriptor slices (arm64 engine, x86_64 stub)")
add_custom_target(tonetrace_muscriptor_fat DEPENDS "${TONETRACE_MS_FAT}")

add_library(tonetrace_ml STATIC src/analysis/ml/NeuralTranscriber.mm src/analysis/ml/MuScriptorTranscriber.mm)
add_dependencies(tonetrace_ml tonetrace_muscriptor_fat)
target_link_libraries(tonetrace_ml PUBLIC tonetrace_core ${COREML} "${TONETRACE_MS_FAT}" "-framework Foundation" "-framework Metal" "-framework MetalKit")
set_target_properties(tonetrace_ml PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(TONETRACE_MODEL_SRC "${CMAKE_SOURCE_DIR}/assets/ml/nmp.mlpackage")
set(TONETRACE_MODEL_OUT "${CMAKE_BINARY_DIR}/ml/nmp.mlmodelc")
file(GLOB_RECURSE TONETRACE_MODEL_FILES "${TONETRACE_MODEL_SRC}/*")
add_custom_command(OUTPUT "${TONETRACE_MODEL_OUT}/coremldata.bin"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ml"
  COMMAND xcrun coremlc compile "${TONETRACE_MODEL_SRC}" "${CMAKE_BINARY_DIR}/ml" >/dev/null
  DEPENDS ${TONETRACE_MODEL_FILES} COMMENT "Compiling the neural model (coremlc)")
add_custom_target(tonetrace_model ALL DEPENDS "${TONETRACE_MODEL_OUT}/coremldata.bin")

# ---- unit tests ----
file(GLOB TEST_SOURCES tests/test_*.cpp)
add_executable(tonetrace_tests ${TEST_SOURCES})
target_link_libraries(tonetrace_tests PRIVATE tonetrace_core)

# ---- neural engine test with the real model (sawtooth bass + octave note, fallback) ----
add_executable(tonetrace_mltest tools/mltest.mm)
target_include_directories(tonetrace_mltest PRIVATE tests)
target_link_libraries(tonetrace_mltest PRIVATE tonetrace_ml)
add_dependencies(tonetrace_mltest tonetrace_model)

# ---- MuScriptor engine test with the real model (skips when the model is not downloaded) ----
add_executable(tonetrace_mstest tools/mstest.mm)
target_link_libraries(tonetrace_mstest PRIVATE tonetrace_ml)
target_compile_definitions(tonetrace_mstest PRIVATE TONETRACE_MS_FIXTURE="${TONETRACE_MS_BUILD}/_deps/muscriptor-src/testdata/audio/fixture_3chunks_16k.wav")
add_test(NAME tonetrace_tests COMMAND tonetrace_tests)

# ---- Cocoa UI ----
file(GLOB UI_SOURCES src/ui/*.mm)
find_library(AUDIOTOOLBOX AudioToolbox)
find_library(COREAUDIO CoreAudio)
find_library(APPKIT AppKit)
find_library(FOUNDATION Foundation)
find_library(QUARTZCORE QuartzCore)

add_library(tonetrace_ui STATIC ${UI_SOURCES})
target_include_directories(tonetrace_ui PUBLIC src)
target_link_libraries(tonetrace_ui PUBLIC tonetrace_core ${APPKIT} ${FOUNDATION} ${QUARTZCORE} ${AUDIOTOOLBOX})
set_target_properties(tonetrace_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- screenshot tool and UI interaction test (real view, synthetic events) ----
foreach(tool uishot uitest bench mklogo)
  add_executable(tonetrace_${tool} tools/${tool}.mm)
  target_include_directories(tonetrace_${tool} PRIVATE tests)
  target_link_libraries(tonetrace_${tool} PRIVATE tonetrace_ui)
  target_link_options(tonetrace_${tool} PRIVATE -ObjC)
endforeach()

target_link_libraries(tonetrace_mklogo PRIVATE "-framework ImageIO" "-framework UniformTypeIdentifiers")

include(cmake/Vst3.cmake)
