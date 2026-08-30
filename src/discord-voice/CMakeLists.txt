add_library(DiscordVoice STATIC
    GatewayClient.cpp
    VoiceGatewayClient.cpp
    VoiceUdpSocket.cpp
    DaveSession.cpp
    GatewayClient.h
    VoiceGatewayClient.h
    VoiceUdpSocket.h
    DaveSession.h
    Log.h
)

target_include_directories(DiscordVoice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(DiscordVoice PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

target_link_libraries(DiscordVoice PUBLIC
    juce::juce_core
    juce::juce_events
    ixwebsocket::ixwebsocket
    Opus::opus
    unofficial-sodium::sodium
    bcrypt
    dave
)
