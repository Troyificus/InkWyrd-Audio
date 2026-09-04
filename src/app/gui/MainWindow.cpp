#include "MainWindow.h"

MainWindow::MainWindow(const juce::String& name)
    : DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel()
                                .findColour(juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons)
{
    setResizable(true, true);
    centreWithSize(900, 620);
    setVisible(true);
}

void MainWindow::showSetupView(AppSettings& settings, bool isFirstRun,
                                std::function<void(SetupComponent::Result)> onSaveAndLaunch)
{
    playerComponent = nullptr;
    setContentOwned(new SetupComponent(settings, isFirstRun, std::move(onSaveAndLaunch)), true);
}

void MainWindow::showPlayerView(PlaylistEngine& playlist, SoundboardEngine& soundboard, MasterEngine& masterEngine,
                                 PluginScanner& scanner, PluginChain& voiceChain,
                                 PlaylistLibrary& library,
                                 SoundboardLayout& soundboardLayout,
                                 TrackSettingsStore& trackGains,
                                 std::function<void(const juce::Uuid&)> onActivatePlaylist,
                                 std::function<void()> onSoundboardLayoutChanged,
                                 std::function<void(const juce::Uuid&)> onPlaylistEdited,
                                 std::function<void()> onSettingsClicked)
{
    auto* component = new PlayerComponent(playlist, soundboard, masterEngine, scanner, voiceChain,
                                           library, soundboardLayout, trackGains,
                                           std::move(onActivatePlaylist),
                                           std::move(onSoundboardLayoutChanged),
                                           std::move(onPlaylistEdited),
                                           std::move(onSettingsClicked));
    playerComponent = component;
    setContentOwned(component, true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
