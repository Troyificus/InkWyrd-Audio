juce_add_console_app(DiscordVoiceSpike
    PRODUCT_NAME "Inkwyrd Discord Voice Spike"
)

target_sources(DiscordVoiceSpike PRIVATE
    Main.cpp
)

target_link_libraries(DiscordVoiceSpike PRIVATE
    DiscordVoice
)

inkwyrd_copy_dave_dll(DiscordVoiceSpike)
