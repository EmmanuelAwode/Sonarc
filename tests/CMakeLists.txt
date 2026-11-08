add_executable(sonarc_tests
    TestMain.cpp
    FftTests.cpp
    KeyDetectorTests.cpp
    LevelMeterTests.cpp
    RingBufferTests.cpp
    SampleConversionTests.cpp
    SpectrogramAnalyzerTests.cpp
    SpectrumAnalyzerTests.cpp
    TempoAnalyzerTests.cpp
    WaveformBuilderTests.cpp)

target_link_libraries(sonarc_tests PRIVATE sonarc_core)

if(MSVC)
    target_compile_options(sonarc_tests PRIVATE /W4 /permissive- /utf-8)
endif()

add_test(NAME sonarc_tests COMMAND sonarc_tests)

# Preset model tests (need juce_core for JSON).
juce_add_console_app(sonarc_preset_tests PRODUCT_NAME "SonarcPresetTests")
target_sources(sonarc_preset_tests PRIVATE
    TestMain.cpp
    PresetTests.cpp
    ${CMAKE_SOURCE_DIR}/src/presets/WaveformPresets.cpp)
target_include_directories(sonarc_preset_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(sonarc_preset_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(sonarc_preset_tests
    PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME sonarc_preset_tests COMMAND sonarc_preset_tests)
