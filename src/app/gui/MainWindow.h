#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"
#include "SetupComponent.h"
#include "PlayerComponent.h"

// Thin window chrome - owns no engine state itself, just swaps its
// content component between the Setup and Player views on request.
class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name);

    void showSetupView(AppSettings& settings, bool isFirstRun,
                        std::function<void(SetupComponent::Result)> onSaveAndLaunch);

    void showPlayerView(PlaylistEngine& playlist, SoundboardEngine& soundboard, MasterEngine& masterEngine,
                         PluginScanner& scanner, PluginChain& voiceChain,
                         juce::Array<juce::PluginDescription> availablePlugins,
                         PlaylistLibrary& library,
                         SoundboardLayout& soundboardLayout,
                         TrackGainStore& trackGains,
                         std::function<void(const juce::Uuid&)> onActivatePlaylist,
                         std::function<void()> onSoundboardLayoutChanged,
                         std::function<void(const juce::Uuid&)> onPlaylistEdited,
                         std::function<void()> onSettingsClicked);

    // nullptr if the Setup view is currently showing.
    PlayerComponent* getPlayerComponent() const { return playerComponent; }

    void closeButtonPressed() override;

private:
    PlayerComponent* playerComponent = nullptr; // non-owning; tracks whatever setContentOwned currently holds
};
