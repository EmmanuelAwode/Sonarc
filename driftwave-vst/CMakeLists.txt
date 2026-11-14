cmake_minimum_required(VERSION 3.22)

project(DriftwaveFX VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the MSVC runtime statically so the .vst3 has no VC++ redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DRIFTWAVE_BUILD_PLUGIN "Build the VST3 plugin (needs JUCE)" ON)
option(DRIFTWAVE_BUILD_TESTS  "Build the DSP unit tests" ON)
option(DRIFTWAVE_COPY_PLUGIN  "Copy the built .vst3 into the system VST3 folder (needs an elevated prompt)" OFF)
set(DRIFTWAVE_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout. Leave empty to download JUCE automatically.")

# ---------------------------------------------------------------------------
# driftwave_dsp: plain C++20, no JUCE. Everything that touches audio lives here
# so it can be unit-tested on its own and reused by other hosts later.
# ---------------------------------------------------------------------------
add_library(driftwave_dsp STATIC
    dsp/Biquad.h
    dsp/Smoothing.h
    dsp/BassBoost.h
    dsp/OutputStage.h
    dsp/TapeSpeed.h
    dsp/TapeSpeed.cpp
    dsp/Fft.h
    dsp/ConvReverb.h
    dsp/ConvReverb.cpp
    dsp/EffectChain.h
    dsp/EffectChain.cpp)
target_include_directories(driftwave_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(driftwave_dsp PRIVATE /W4)
else()
    target_compile_options(driftwave_dsp PRIVATE -Wall -Wextra)
endif()

if(DRIFTWAVE_BUILD_TESTS)
    enable_testing()
    add_executable(driftwave_dsp_tests tests/DspTests.cpp)
    target_link_libraries(driftwave_dsp_tests PRIVATE driftwave_dsp)
    add_test(NAME driftwave_dsp_tests COMMAND driftwave_dsp_tests)
    # Prints the processor cost of the speed section:  driftwave_speed_bench
    add_executable(driftwave_speed_bench tests/SpeedBench.cpp)
    target_link_libraries(driftwave_speed_bench PRIVATE driftwave_dsp)
endif()

# ---------------------------------------------------------------------------
# The VST3 plugin.
# ---------------------------------------------------------------------------
if(DRIFTWAVE_BUILD_PLUGIN)
    if(DRIFTWAVE_JUCE_DIR)
        add_subdirectory(${DRIFTWAVE_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        set(FETCHCONTENT_QUIET OFF)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.9
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(DriftwaveFX
        PRODUCT_NAME                "Driftwave FX"
        COMPANY_NAME                "Driftwave"
        BUNDLE_ID                   "studio.driftwave.fx"
        PLUGIN_MANUFACTURER_CODE    Drwv
        PLUGIN_CODE                 Dwfx
        FORMATS                     VST3
        VST3_CATEGORIES             Fx Reverb
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     ${DRIFTWAVE_COPY_PLUGIN})

    target_sources(DriftwaveFX PRIVATE
        plugin/Parameters.h
        plugin/Parameters.cpp
        plugin/Presets.h
        plugin/Presets.cpp
        plugin/SignalFeed.h
        plugin/Displays.h
        plugin/Displays.cpp
        plugin/PluginProcessor.h
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.h
        plugin/PluginEditor.cpp
        plugin/LookAndFeel.h
        plugin/LookAndFeel.cpp
        plugin/Theme.h
        plugin/Theme.cpp)

    target_compile_definitions(DriftwaveFX PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(DriftwaveFX
        PRIVATE
            driftwave_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)

    # A minimal host that loads the built .vst3 and checks it end to end.
    if(DRIFTWAVE_BUILD_TESTS)
        juce_add_console_app(driftwave_plugin_smoke)
        target_sources(driftwave_plugin_smoke PRIVATE tests/PluginSmokeTest.cpp)
        target_compile_definitions(driftwave_plugin_smoke PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_MODAL_LOOPS_PERMITTED=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)
        target_link_libraries(driftwave_plugin_smoke PRIVATE
            juce::juce_audio_processors
            juce::juce_recommended_config_flags)
        add_dependencies(driftwave_plugin_smoke DriftwaveFX_VST3)
        add_test(NAME driftwave_plugin_smoke
            COMMAND driftwave_plugin_smoke "$<GENEX_EVAL:$<TARGET_PROPERTY:DriftwaveFX_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")

        # Renders the interface to a PNG without a DAW:  driftwave_ui_snapshot out.png
        # It links the plugin's own code directly, so it is handy while working on the UI.
        add_executable(driftwave_ui_snapshot tests/UiSnapshot.cpp)
        target_link_libraries(driftwave_ui_snapshot PRIVATE DriftwaveFX driftwave_dsp)
        # Compile against JUCE exactly as the plugin does (same include paths and module settings).
        target_include_directories(driftwave_ui_snapshot PRIVATE $<TARGET_PROPERTY:DriftwaveFX,INCLUDE_DIRECTORIES>)
        target_compile_definitions(driftwave_ui_snapshot PRIVATE $<TARGET_PROPERTY:DriftwaveFX,COMPILE_DEFINITIONS>)

        # Presets: built the same way, since the preset code lives in the plugin layer.
        add_executable(driftwave_preset_tests tests/PresetTests.cpp)
        target_link_libraries(driftwave_preset_tests PRIVATE DriftwaveFX driftwave_dsp)
        target_include_directories(driftwave_preset_tests PRIVATE $<TARGET_PROPERTY:DriftwaveFX,INCLUDE_DIRECTORIES>)
        target_compile_definitions(driftwave_preset_tests PRIVATE $<TARGET_PROPERTY:DriftwaveFX,COMPILE_DEFINITIONS>)
        add_test(NAME driftwave_preset_tests COMMAND driftwave_preset_tests)
    endif()
endif()
