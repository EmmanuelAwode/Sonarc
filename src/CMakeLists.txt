# ---------------------------------------------------------------------------
# sonarc_core: capture engine + DSP. Plain C++20 and Win32, no JUCE, so it can
# be unit tested on its own and reused by a future plugin bridge.
# ---------------------------------------------------------------------------
add_library(sonarc_core STATIC
    audio/AudioEngine.cpp
    audio/WasapiCaptureBackend.cpp
    dsp/Fft.cpp
    dsp/LevelMeter.cpp
    dsp/SpectrumAnalyzer.cpp)

target_include_directories(sonarc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sonarc_core PUBLIC ole32 avrt mmdevapi version)

if(MSVC)
    target_compile_options(sonarc_core PRIVATE /W4 /permissive- /utf-8)
endif()

# ---------------------------------------------------------------------------
# Sonarc: the desktop application (JUCE UI).
# ---------------------------------------------------------------------------
juce_add_gui_app(Sonarc
    PRODUCT_NAME "Sonarc"
    COMPANY_NAME "Sonarc"
    VERSION ${PROJECT_VERSION})

target_sources(Sonarc PRIVATE
    app/Main.cpp
    settings/AppSettings.cpp
    ui/MainComponent.cpp
    ui/MainWindow.cpp
    ui/SonarcLookAndFeel.cpp
    visualizers/SpectrumView.cpp
    visualizers/StereoPeakMeterView.cpp
    visualizers/WaveformView.cpp)

target_compile_definitions(Sonarc PRIVATE
    SONARC_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Sonarc,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Sonarc,JUCE_VERSION>")

if(MSVC)
    target_compile_options(Sonarc PRIVATE /utf-8)
endif()

target_link_libraries(Sonarc
    PRIVATE
        sonarc_core
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
