juce_add_console_app(DiscordVoiceSpike
    PRODUCT_NAME "Inkwyrd Discord Voice Spike"
)

target_sources(DiscordVoiceSpike PRIVATE
    Main.cpp
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

target_compile_definitions(DiscordVoiceSpike PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

target_link_libraries(DiscordVoiceSpike PRIVATE
    juce::juce_core
    juce::juce_events
    ixwebsocket::ixwebsocket
    Opus::opus
    unofficial-sodium::sodium
    bcrypt
    dave
)

# dave is a manually-declared IMPORTED target (not vcpkg-installed), so
# vcpkg's automatic post-build DLL copy (which handles opus/sodium/zlib)
# doesn't know about it - copy it alongside the exe explicitly.
add_custom_command(TARGET DiscordVoiceSpike POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:dave>"
        "$<TARGET_FILE_DIR:DiscordVoiceSpike>"
)
