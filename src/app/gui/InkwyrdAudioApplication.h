#pragma once

#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <map>

#include "AppSettings.h"
#include "DiscordConnector.h"
#include "MainWindow.h"
#include "PlaylistLibrary.h"
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
    void registerSoundboardFolder(const juce::File& folder);
    void migratePlaylistLibraryIfNeeded();
    void activatePlaylist(const juce::Uuid& id);

    // A playlist's contents changed (files dropped in, a folder added, a
    // linked folder re-scanned). Only matters to the engine if it's the
    // one currently playing.
    void handlePlaylistEdited(const juce::Uuid& id);
    void updateWarningBanner();

    AppSettings settings;

    juce::AudioFormatManager formatManager;
    PlaylistEngine playlist { formatManager };
    SoundboardEngine soundboard { formatManager };
    PluginScanner scanner;
    PluginChain voiceChain;
    MasterEngine masterEngine { playlist, soundboard, voiceChain };
    ControlServer controlServer { playlist, soundboard, masterEngine };
    juce::AudioDeviceManager deviceManager;

    PlaylistLibrary library { formatManager };

    juce::Array<juce::PluginDescription> foundPlugins;
    juce::String audioDeviceError; // non-empty if initialiseWithDefaultDevices() failed - see initialise()

    juce::Uuid activePlaylistId;

    // Where each playlist was when we switched away from it, so returning
    // to one resumes rather than restarting. Keyed by playlist id, storing
    // a FILE not an index - shuffle reshuffles the order on wrap, so a
    // saved index would point at an unrelated track. Session-only for now.
    std::map<juce::String, juce::File> lastPlayedByPlaylistId;

    DiscordConnector discordConnector;
    std::unique_ptr<DiscordAudioSender> sender;
    bool discordConnectAttempted = false;

    std::unique_ptr<MainWindow> mainWindow;
};
