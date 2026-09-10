#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <map>

#include "AppSettings.h"
#include "TrackMetadataStore.h"
#include "DiscordConnector.h"
#include "DiscordRpcClient.h"
#include "InkwyrdLookAndFeel.h"
#include "LibraryWindow.h"
#include "MainWindow.h"
#include "PlayerWindow.h"
#include "PlaylistLibrary.h"
#include "PlaylistWindow.h"
#include "SoundboardWindow.h"
#include "VoiceFxWindow.h"
#include "TrackLibrary.h"
#include "TrackSettingsStore.h"
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

    // The VST3 plugins the user has picked for the voice chain. There is
    // no folder scan any more - see PluginScanner's header - so this is
    // just a list to read at startup and write when it changes.
    static juce::File getVoicePluginsFile();
    void saveVoicePlugins();
    void migratePlaylistLibraryIfNeeded();
    void migrateSoundboardLayoutIfNeeded();

    // One-time: seed the new master track library from every track the
    // existing playlists resolve to, so an upgrading user's music is
    // already there rather than the list starting empty next to full
    // playlists.
    void migrateTrackLibraryIfNeeded();

    void activatePlaylist(const juce::Uuid& id);

    // A playlist's contents changed (files dropped in, a folder added, a
    // linked folder re-scanned). Only matters to the engine if it's the
    // one currently playing.
    void handlePlaylistEdited(const juce::Uuid& id);

    // A different playlist was SELECTED for browsing - the Playlist
    // window follows this. Never touches playback.
    void handlePlaylistSelected(const juce::Uuid& id);

    // Double-clicked a track in the Playlist window: play it, activating
    // its playlist first if that isn't the one already running.
    void playTrackInPlaylist(const juce::Uuid& playlistId, const juce::File& file);

    void updateWarningBanner();

    // Pushes the saved credentials into discordRpc and switches it on or
    // off. Called at startup and again whenever Settings is saved.
    void applyDiscordRpcSettings();

    // First member, and deliberately so: it must outlive every window
    // and component that points at it, and members are destroyed in
    // reverse declaration order. Installed as the default LookAndFeel in
    // initialise() and cleared again in shutdown().
    InkwyrdLookAndFeel lookAndFeel;

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
    TrackSettingsStore trackGains;
    TrackLibrary trackLibrary;

    // Embedded tags for everything in the library, scanned in the
    // background and cached to disk. Read by the Library and Playlist
    // windows to show real titles instead of filenames.
    TrackMetadataStore trackMetadata;

    juce::String audioDeviceError; // non-empty if initialiseWithDefaultDevices() failed - see initialise()

    juce::Uuid activePlaylistId;

    // Where each playlist was when we switched away from it, so returning
    // to one resumes rather than restarting. Keyed by playlist id, storing
    // a FILE not an index - shuffle reshuffles the order on wrap, so a
    // saved index would point at an unrelated track. Session-only for now.
    std::map<juce::String, juce::File> lastPlayedByPlaylistId;

    DiscordConnector discordConnector;
    std::unique_ptr<DiscordAudioSender> sender;

    // Mutes the user's own Discord client while their mic is live here.
    // Entirely separate from discordConnector, which is the BOT's
    // connection - this one talks to the desktop client on this machine.
    DiscordRpcClient discordRpc;

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

    // Setup/Settings only, now - see MainWindow.h. Always exists once
    // initialise() creates it; never destroyed until shutdown().
    std::unique_ptr<MainWindow> mainWindow;

    // The Winamp-style layout: created once, the first time showPlayer()
    // runs, and kept alive for the rest of the app's life from then on -
    // Settings hides these, it never destroys them, so nothing a
    // satellite points into can be pulled out from under it.
    std::unique_ptr<PlayerWindow> playerWindow;
    std::unique_ptr<PlaylistWindow> playlistWindow;
    std::unique_ptr<LibraryWindow> libraryWindow;
    std::unique_ptr<VoiceFxWindow> voiceFxWindow;
    std::unique_ptr<SoundboardWindow> soundboardWindow;

    // Voice FX/Soundboard are hideable independent of the Setup
    // transition (their own activator buttons/close boxes). Remembered
    // here so going into Settings and back restores exactly how the user
    // had them, rather than forcing them open or leaving them hidden.
    bool playlistWasVisibleBeforeSetup = true;
    bool libraryWasVisibleBeforeSetup = true;
    bool voiceFxWasVisibleBeforeSetup = false;
    bool soundboardWasVisibleBeforeSetup = false;

    // Last member, so it's destroyed first and its thread is joined
    // before anything it might log about goes away.
    MessageThreadWatchdog watchdog;
};
