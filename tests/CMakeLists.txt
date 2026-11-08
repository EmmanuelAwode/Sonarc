add_executable(sonarc_tests
    TestMain.cpp
    FftTests.cpp
    LevelMeterTests.cpp
    RingBufferTests.cpp
    SampleConversionTests.cpp
    SpectrogramAnalyzerTests.cpp
    SpectrumAnalyzerTests.cpp
    WaveformBuilderTests.cpp)

target_link_libraries(sonarc_tests PRIVATE sonarc_core)

if(MSVC)
    target_compile_options(sonarc_tests PRIVATE /W4 /permissive- /utf-8)
endif()

add_test(NAME sonarc_tests COMMAND sonarc_tests)
