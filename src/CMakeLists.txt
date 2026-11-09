# ---------------------------------------------------------------------------
# sonarc_core: capture engine + DSP. Plain C++20 and Win32, no JUCE, so it can
# be unit tested on its own and reused by a future plugin bridge.
# ---------------------------------------------------------------------------
add_library(sonarc_core STATIC
    audio/AudioEngine.cpp
    audio/WasapiCaptureBackend.cpp
    dsp/Fft.cpp
    dsp/KeyDetector.cpp
    dsp/LevelMeter.cpp
    dsp/MagnitudeSpectrum.cpp
    dsp/SpectrogramAnalyzer.cpp
    dsp/SpectrumAnalyzer.cpp
    dsp/TempoAnalyzer.cpp)

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
    VERSION ${PROJECT_VERSION}
    # The musical-S logo (tools/make_logo.py); JUCE turns these into the exe's .ico,
    # which Windows shows on the taskbar, Start menu, shortcuts and title bar.
    ICON_BIG "${CMAKE_SOURCE_DIR}/resources/logo.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/resources/logo-small.png")

target_sources(Sonarc PRIVATE
    app/Main.cpp
    presets/WaveformPresets.cpp
    settings/AppSettings.cpp
    ui/ColourSettingsPanel.cpp
    ui/MainComponent.cpp
    ui/MainWindow.cpp
    ui/WaveformStyleEditor.cpp
    ui/WindowsDesktop.cpp
    ui/SonarcLookAndFeel.cpp
    ui/Theme.cpp
    visualizers/KeyView.cpp
    visualizers/SpectrogramView.cpp
    visualizers/SpectrumView.cpp
    visualizers/StereoPeakMeterView.cpp
    visualizers/TempoView.cpp
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

# The logo, drawn next to the wordmark in the control bar.
juce_add_binary_data(SonarcAssets SOURCES "${CMAKE_SOURCE_DIR}/resources/logo-small.png")

target_link_libraries(Sonarc
    PRIVATE
        sonarc_core
        SonarcAssets
        juce::juce_gui_extra
        comctl32
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
