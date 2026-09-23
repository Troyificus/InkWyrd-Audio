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
#include "SceneEditor.h"
#include "SceneLibrary.h"
#include "ScenesWindow.h"
#include "VolumeGlide.h"
#include "SoundboardWindow.h"
#include "TagEditorWindow.h"
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
class InkwyrdAudioApplication : public juce::JUCEApplication,
                                 private juce::ChangeListener
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

    // Pushes the saved ducking settings into the engine. Called at
    // startup and again whenever Settings is saved.
    void applyDuckSettings();

    // Asks GitHub whether there's a newer release, if the user hasn't
    // turned that off. Reports into the warning banner and never
    // downloads anything - see UpdateCheck.h.
    void startUpdateCheckIfEnabled();

    // Puts the update link on the Player, if the check found something
    // and the Player exists yet.
    void showUpdateLinkIfAny();

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

    // Puts the Setup music folder's tracks into the master track library,
    // once. Called on first run as Setup makes its playlist, and at
    // startup to repair installs set up before that happened - see
    // AppSettings::isSetupFolderInLibrary.
    void addSetupFolderToLibraryIfNeeded();

    void activatePlaylist(const juce::Uuid& id);

    // A playlist's contents changed (files dropped in, a folder added, a
    // linked folder re-scanned). Only matters to the engine if it's the
    // one currently playing.
    void handlePlaylistEdited(const juce::Uuid& id);

    // Reads tags for anything in the library OR any playlist that isn't
    // cached yet. Both, because a playlist can hold tracks the library
    // doesn't (dropped straight onto the Playlist window, or new files in
    // a linked folder) and the Player's display needs tags for whatever
    // is actually playing. Cheap when nothing's new: only a size/date
    // check per file.
    void rescanTrackMetadata();

    // Auditioning a track from the Library: local output only, and the
    // playlist pauses while it plays. See MasterEngine::startPreview.
    void startPreview(const juce::File& file);
    void stopPreview();

    // The tag editor, opened from either window's right-click menu. One
    // at a time: a second window editing the same file would be two
    // truths about one set of tags.
    void openTagEditor(const juce::Array<juce::File>& files);

    // Run immediately before any file is written. Stops a preview of
    // those files - a file open for reading can't be replaced - and
    // returns a message if one of them is loaded in the player, which
    // only the user can clear.
    juce::String prepareForTagWrite(const juce::Array<juce::File>& files);

    // Re-read what was just written, so the lists show it at once.
    void handleTagsSaved(const juce::Array<juce::File>& files);

    // The preview transport telling us it started or stopped - including
    // reaching the end of the file by itself, which is the case a poll
    // would have had to catch.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Whether WE paused the playlist for a preview. A playlist the user
    // had already paused must not start playing when a preview ends.
    bool previewPausedPlaylist = false;

    // Applies a skin by folder name ({} = the built-in look) and returns
    // what to tell the user: empty when it loaded cleanly, otherwise the
    // warnings, or why it couldn't be used.
    juce::String applySkin(const juce::String& skinName);

    // Writes the example skins once, so the skins folder has something
    // in it to copy.
    void writeExampleSkinsIfNeeded();
    void writeSpriteExampleSkinIfNeeded();

    // --- Scenes ---------------------------------------------------------
    // Carries out what planScene says pressing a scene should change. The
    // rules themselves live in planScene (SceneLibrary.h), where the
    // self-test can reach them; this only does what the plan says.
    void activateScene(const juce::Uuid& id);

    // A Stream Deck Scene key. Exact name, then ignoring case.
    void activateSceneByName(const juce::String& name);

    // What is true right now, as far as a scene cares.
    SceneContext buildSceneContext();

    // What is playing now, written into `base` - keeping its id, name,
    // colour and whether it sets the volume. Both "Save current as
    // scene" and "Update from what's playing now" are this.
    Scene captureCurrentScene(Scene base);

    void saveCurrentAsNewScene();
    void updateSceneFromCurrent(const juce::Uuid& id);
    void editScene(const juce::Uuid& id);
    void deleteScene(const juce::Uuid& id);
    void openSceneEditor(const Scene& scene, bool isNew);
    ScenesComponent::Callbacks makeScenesCallbacks();
    void refreshScenesWindow();

    // How long a scene change takes: loops fade and the volume glides over
    // this. The playlist crossfade length, so the whole room moves
    // together; never under a second, so a scene is never a cut even
    // with crossfading switched off.
    double sceneTransitionSeconds();

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
    MasterEngine masterEngine { playlist, soundboard, voiceChain, formatManager };
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

    // Set when the update check finds something newer. The URL has
    // already been through safeReleasePageUrl.
    juce::String updateVersion, updateUrl;

    juce::String audioDeviceError; // non-empty if initialiseWithDefaultDevices() failed - see initialise()

    juce::Uuid activePlaylistId;

    // Where each playlist was when we switched away from it, so returning
    // to one resumes rather than restarting. Keyed by playlist id, storing
    // a FILE not an index - shuffle reshuffles the order on wrap, so a
    // saved index would point at an unrelated track. Session-only for now.
    std::map<juce::String, juce::File> lastPlayedByPlaylistId;

    std::unique_ptr<TagEditorWindow> tagEditorWindow;

    SceneLibrary sceneLibrary;

    // The scene last pressed, for the highlight. Session-only: after a
    // restart nothing has been pressed yet, and highlighting a scene the
    // room isn't actually in would be a lie.
    juce::Uuid activeSceneId;

    VolumeGlide volumeGlide;

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
    std::unique_ptr<ScenesWindow> scenesWindow;

    // The one TooltipWindow for the whole app - see initialise().
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    // Last member, so it's destroyed first and its thread is joined
    // before anything it might log about goes away.
    MessageThreadWatchdog watchdog;
};
