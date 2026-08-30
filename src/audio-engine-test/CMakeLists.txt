juce_add_console_app(AudioEngineTest
    PRODUCT_NAME "Inkwyrd Audio Engine Test"
)

target_sources(AudioEngineTest PRIVATE
    Main.cpp
)

target_compile_definitions(AudioEngineTest PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_ASIO=0
    JUCE_WASAPI=1
)

target_link_libraries(AudioEngineTest PRIVATE
    AudioEngine
)
