#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

#include "MasterEngine.h"
#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"
#include "PlaylistPanel.h"
#include "PluginChain.h"
#include "PluginScanner.h"
#include "SoundboardEngine.h"
#include "SoundboardGridComponent.h"
#include "SoundboardLayout.h"

// Main screen: status and transport across the top, then the playlist
// library and its tracks on the left, the SFX board on the right.
//
// Calls straight into the engine objects from button handlers - JUCE
// button callbacks already run on the message thread, which is the
// thread every engine method here expects.
class PlayerComponent : public juce::Component,
                         private juce::Timer
{
public:
    PlayerComponent(PlaylistEngine& playlistToUse,
                     SoundboardEngine& soundboardToUse,
                     MasterEngine& masterEngineToUse,
                     PluginScanner& scannerToUse,
                     PluginChain& voiceChainToUse,
                     juce::Array<juce::PluginDescription> availablePluginsToUse,
                     PlaylistLibrary& libraryToUse,
                     SoundboardLayout& soundboardLayoutToUse,
                     std::function<void(const juce::Uuid&)> onActivatePlaylistToUse,
                     std::function<void()> onSoundboardLayoutChangedToUse,
                     std::function<void(const juce::Uuid&)> onPlaylistEditedToUse,
                     std::function<void()> onSettingsClickedToUse);

    ~PlayerComponent() override;

    void resized() override;

    // Pushed from InkwyrdAudioApplication as DiscordConnector's status
    // callback fires (on the message thread, already marshaled there).
    void setDiscordStatus(const juce::String& text);

    // Prominent warning banner for things that silently produce "no
    // sound" and would otherwise have no visible indication: the audio
    // device failing to open, a playlist yielding zero playable files,
    // unreadable playlist files. Empty text keeps the banner hidden.
    void setWarningBanner(const juce::String& text);

    // Re-reads the engines' current shuffle/mute/monitor state into the
    // button labels, for when something outside this component changes
    // it (connecting to Discord turns local monitoring off).
    void refreshToggleStates();

    void refreshSoundboard();

    // Whether Discord credentials are configured, so the transport can
    // point out that Monitor being off means nothing is audible ANYWHERE
    // rather than just "not locally".
    void setDiscordConfigured(bool configured);

    // The plugin scan runs in the background now, so the list can arrive
    // after this screen is already up.
    void setAvailablePlugins(juce::Array<juce::PluginDescription> plugins);
    void setPluginScanInProgress(bool scanning);
    void setRescanPluginsCallback(std::function<void()> callback);
    void setPlayingPlaylistId(const juce::Uuid& id);
    PlaylistPanel& getPlaylistPanel() { return playlistPanel; }

private:
    void timerCallback() override;
    void updateShuffleButtonText();
    void updateMuteButtonText();
    void updateMonitorButtonText();
    void updatePlayButtonText();
    void updateMonitorHint();
    void showVoiceFxWindow();

    PlaylistEngine& playlist;
    SoundboardEngine& soundboard;
    MasterEngine& masterEngine;
    PluginScanner& scanner;
    PluginChain& voiceChain;
    juce::Array<juce::PluginDescription> availablePlugins;

    juce::Label nowPlayingLabel;
    juce::Label discordStatusLabel;
    juce::Label warningBannerLabel; // hidden (zero height) unless given non-empty text
    juce::Label monitorHintLabel;   // "Monitor is off" - only while that actually means silence

    bool discordConfigured = false;

    juce::TextButton playButton;
    juce::TextButton skipButton { "Skip" };
    juce::TextButton shuffleButton;
    juce::TextButton muteButton;
    juce::TextButton monitorButton;
    juce::TextButton voiceFxButton { "Voice FX..." };
    juce::TextButton settingsButton { "Settings" };

    PlaylistPanel playlistPanel;
    SoundboardGridComponent soundboardGrid;

    juce::File lastSeenTrack; // so the track list only repaints when it changes

    // Non-modal, so the user can keep driving the session while it's open.
    juce::Component::SafePointer<juce::DialogWindow> voiceFxWindow;

    bool pluginScanInProgress = false;
    std::function<void()> onRescanPlugins;
};
