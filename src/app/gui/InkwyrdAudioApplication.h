#pragma once

#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "AppSettings.h"
#include "DiscordConnector.h"
#include "MainWindow.h"
#include "MasterEngine.h"
#include "DiscordAudioSender.h"
#include "ControlServer.h"
#include "PlaylistEngine.h"
#include "SoundboardEngine.h"
#include "PluginScanner.h"
#include "PluginChain.h"

// Everything main() used to own directly, for the app's whole lifetime.
// initialise()/shutdown() replace the old manual
// ScopedJuceInitialiser_GUI + runDispatchLoop() pump - JUCE drives its
// own real message loop for a GUI app, so PlaylistEngine's juce::Timer-
// based crossfade (message-thread-only, see PlaylistEngine.h) works
// automatically, no special driving required.
class InkwyrdAudioApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; } // one audio device, one bot token, one control-server port

    void initialise(const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override { quit(); }

private:
    void showSetup();
    void showPlayer();
    void completeSetupAndLaunch(SetupComponent::Result result);
    void startDiscordConnectIfConfigured();
    void applyDefaultLocalMonitoring();
    juce::StringArray registerSoundboardFolder(const juce::File& folder);

    AppSettings settings;

    juce::AudioFormatManager formatManager;
    PlaylistEngine playlist { formatManager };
    SoundboardEngine soundboard { formatManager };
    PluginScanner scanner;
    PluginChain voiceChain;
    MasterEngine masterEngine { playlist, soundboard, voiceChain };
    ControlServer controlServer { playlist, soundboard, masterEngine };
    juce::AudioDeviceManager deviceManager;

    juce::Array<juce::PluginDescription> foundPlugins;
    juce::StringArray soundNames;
    juce::String audioDeviceError; // non-empty if initialiseWithDefaultDevices() failed - see initialise()

    DiscordConnector discordConnector;
    std::unique_ptr<DiscordAudioSender> sender;
    bool discordConnectAttempted = false;

    std::unique_ptr<MainWindow> mainWindow;
};
