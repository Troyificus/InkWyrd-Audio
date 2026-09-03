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

void MainWindow::showSetupView(AppSettings& settings, std::function<void(SetupComponent::Result)> onSaveAndLaunch)
{
    playerComponent = nullptr;
    setContentOwned(new SetupComponent(settings, std::move(onSaveAndLaunch)), true);
}

void MainWindow::showPlayerView(PlaylistEngine& playlist, SoundboardEngine& soundboard, MasterEngine& masterEngine,
                                 PluginScanner& scanner, PluginChain& voiceChain,
                                 juce::Array<juce::PluginDescription> availablePlugins,
                                 PlaylistLibrary& library,
                                 std::function<void(const juce::Uuid&)> onActivatePlaylist,
                                 std::function<void(const juce::Uuid&)> onPlaylistEdited,
                                 std::function<void()> onSettingsClicked)
{
    auto* component = new PlayerComponent(playlist, soundboard, masterEngine, scanner, voiceChain,
                                           std::move(availablePlugins), library,
                                           std::move(onActivatePlaylist),
                                           std::move(onPlaylistEdited),
                                           std::move(onSettingsClicked));
    playerComponent = component;
    setContentOwned(component, true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
