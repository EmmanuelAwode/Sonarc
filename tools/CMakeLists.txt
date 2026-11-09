# sonarc_analyze: run the BPM and key detectors over audio files (WAV, MP3, FLAC, OGG, AIFF).
juce_add_console_app(sonarc_analyze PRODUCT_NAME "sonarc_analyze")
target_sources(sonarc_analyze PRIVATE AnalyzeFiles.cpp)
target_compile_definitions(sonarc_analyze PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(sonarc_analyze
    PRIVATE
        sonarc_core
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
