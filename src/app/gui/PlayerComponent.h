#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

#include "MasterEngine.h"
#include "PlaylistEngine.h"
#include "SoundboardEngine.h"
#include "PluginChain.h"
#include "PluginScanner.h"

// Main playback screen: now-playing/skip/shuffle/mute, one button per
// soundboard sound, a scanned-VST3-plugin list with per-row "Add", and
// the live voice chain with per-row "Remove". Calls straight into the
// engine objects from button onClick handlers - unlike the old console
// app's stdin thread, JUCE button callbacks already run on the message
// thread, so none of this needs callAsync marshaling.
class PlayerComponent : public juce::Component,
                         private juce::Timer
{
public:
    PlayerComponent(PlaylistEngine& playlistToUse, SoundboardEngine& soundboardToUse, MasterEngine& masterEngineToUse,
                     PluginScanner& scannerToUse, PluginChain& voiceChainToUse,
                     juce::Array<juce::PluginDescription> availablePluginsToUse,
                     juce::StringArray soundNamesToUse,
                     std::function<void()> onSettingsClickedToUse);

    void resized() override;

    // Pushed from InkwyrdAudioApplication as DiscordConnector's status
    // callback fires (on the message thread, already marshaled there).
    void setDiscordStatus(const juce::String& text);

private:
    void timerCallback() override;
    void updateShuffleButtonText();
    void updateMuteButtonText();
    void rebuildChainListUI();

    PlaylistEngine& playlist;
    SoundboardEngine& soundboard;
    MasterEngine& masterEngine;
    PluginScanner& scanner;
    PluginChain& voiceChain;
    juce::Array<juce::PluginDescription> availablePlugins;

    juce::Label nowPlayingLabel;
    juce::Label discordStatusLabel;

    juce::TextButton skipButton { "Skip" };
    juce::TextButton shuffleButton;
    juce::TextButton muteButton;
    juce::TextButton settingsButton { "Settings" };

    juce::Label soundboardCaption { {}, "Soundboard" };
    juce::Viewport soundboardViewport;
    juce::Component soundboardPanel;
    juce::OwnedArray<juce::TextButton> soundboardButtons;

    juce::Label pluginListCaption { {}, "Available VST3 plugins" };
    juce::Viewport pluginListViewport;
    juce::Component pluginListPanel;
    juce::OwnedArray<juce::TextButton> addPluginButtons;

    juce::Label chainListCaption { {}, "Live voice chain" };
    juce::Viewport chainListViewport;
    juce::Component chainListPanel;
    juce::OwnedArray<juce::TextButton> removeChainButtons;
};
