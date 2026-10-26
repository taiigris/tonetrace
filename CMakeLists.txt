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
  src/analysis/LiveSpectrum.cpp src/analysis/LiveHarmonic.cpp src/analysis/WavePeaks.cpp
  src/analysis/PitchLines.cpp src/analysis/LineFrames.cpp src/analysis/LineTracker.cpp src/analysis/LineOnsets.cpp
  src/analysis/LineBuilder.cpp src/analysis/LiveLines.cpp
  src/midi/MidiFileWriter.cpp src/midi/ExportPlanner.cpp
  src/preview/PreviewSynth.cpp
  src/state/StateCodec.cpp src/state/UiSettings.cpp src/core/Logger.cpp src/core/Diagnostics.cpp
  src/core/NoteEdit.cpp src/core/Snapshot.cpp
  src/core/TonetraceKernel.cpp src/core/KernelModel.cpp src/core/KernelWorker.cpp src/core/KernelAudio.cpp)
target_include_directories(tonetrace_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
find_library(ACCELERATE Accelerate)
target_link_libraries(tonetrace_core PUBLIC ${ACCELERATE})
set_target_properties(tonetrace_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- unit tests ----
file(GLOB TEST_SOURCES tests/test_*.cpp)
add_executable(tonetrace_tests ${TEST_SOURCES})
target_link_libraries(tonetrace_tests PRIVATE tonetrace_core)
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
