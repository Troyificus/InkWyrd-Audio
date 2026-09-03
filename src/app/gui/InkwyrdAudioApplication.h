#pragma once

#include <atomic>
#include <memory>
#include <thread>
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
#include "MessageThreadWatchdog.h"
#include "PlaylistEngine.h"
#include "SoundboardEngine.h"
#include "SoundboardLayout.h"
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
    // One audio device, one bot token, one control-server port - so
    // normally no. The env-var escape hatch exists purely so a second
    // copy can be launched for UI verification while a real session is
    // running: without it, JUCE skips initialise() entirely and goes
    // straight to shutdown() with exit code 0, which looks exactly like
    // a silent startup crash and has now cost two debugging detours.
    // Unset for every real user, so behaviour is unchanged.
    bool moreThanOneInstanceAllowed() override
    {
        return juce::SystemStats::getEnvironmentVariable("INKWYRD_ALLOW_MULTIPLE_INSTANCES", "").isNotEmpty();
    }

    void initialise(const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override { quit(); }

private:
    void showSetup();
    void showPlayer();
    void completeSetupAndLaunch(SetupComponent::Result result);
    void startDiscordConnectIfConfigured();
    void applyDefaultLocalMonitoring();

    // Relaunch the app. Only offered when Discord settings genuinely
    // can't be applied to the running session (a connection has already
    // been made this run).
    void offerRestart();
    // Re-registers the engine's sounds from the board layout. Called
    // after anything changes a slot - the NAME is the engine's key, so a
    // rename genuinely has to re-register, not just repaint.
    void registerSoundboardLayout();

    // The VST3 scan loads every plugin on the machine and took ~18
    // seconds on the test machine - long enough that running it on the
    // message thread made the whole app unresponsive at every launch
    // (caught by MessageThreadWatchdog). It now runs on its own thread,
    // and its result is cached so a scan normally never happens at all.
    static juce::File getPluginCacheFile();
    void startPluginScan();
    void publishScannedPlugins(juce::Array<juce::PluginDescription> plugins);
    void migratePlaylistLibraryIfNeeded();
    void migrateSoundboardLayoutIfNeeded();
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
    SoundboardLayout soundboardLayout { formatManager };

    juce::Array<juce::PluginDescription> foundPlugins;

    // Joined in shutdown(). Only ever touched from the message thread.
    std::unique_ptr<std::thread> pluginScanThread;
    std::atomic<bool> pluginScanRunning { false };
    juce::String audioDeviceError; // non-empty if initialiseWithDefaultDevices() failed - see initialise()

    juce::Uuid activePlaylistId;

    // Where each playlist was when we switched away from it, so returning
    // to one resumes rather than restarting. Keyed by playlist id, storing
    // a FILE not an index - shuffle reshuffles the order on wrap, so a
    // saved index would point at an unrelated track. Session-only for now.
    std::map<juce::String, juce::File> lastPlayedByPlaylistId;

    DiscordConnector discordConnector;
    std::unique_ptr<DiscordAudioSender> sender;

    // Whether a Discord connection has actually been STARTED this run -
    // not merely "startup got as far as trying". Launching with no
    // credentials used to set this anyway, so pasting a token into
    // Settings afterwards left the app insisting on a restart it didn't
    // need. Set inside startDiscordConnectIfConfigured(), where the
    // connect really happens.
    bool discordConnectStarted = false;

    // Whether the player view has ever been shown this run, so reopening
    // Settings can label its button honestly ("Save & Apply", not
    // "Save & Launch").
    bool hasShownPlayer = false;

    std::unique_ptr<MainWindow> mainWindow;

    // Last member, so it's destroyed first and its thread is joined
    // before anything it might log about goes away.
    MessageThreadWatchdog watchdog;
};
