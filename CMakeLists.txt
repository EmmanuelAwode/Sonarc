cmake_minimum_required(VERSION 3.22)

project(Sonarc VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Sonarc currently targets Windows only (the capture engine uses WASAPI).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SONARC_BUILD_TESTS "Build the DSP unit tests" ON)
set(SONARC_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout. Leave empty to download JUCE automatically.")

# ---------------------------------------------------------------------------
# JUCE (UI framework). Downloaded at configure time unless a local copy is given.
# ---------------------------------------------------------------------------
if(SONARC_JUCE_DIR)
    add_subdirectory(${SONARC_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

add_subdirectory(src)
add_subdirectory(tools)

if(SONARC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
