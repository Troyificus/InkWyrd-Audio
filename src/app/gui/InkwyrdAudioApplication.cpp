#include "InkwyrdAudioApplication.h"
#include "Mp3AudioFormat.h"
#include "MediaFoundationAudioFormat.h"
#include "Log.h"

#include "Dialogs.h"

#include <ixwebsocket/IXNetSystem.h>
#include <sodium.h>

#ifdef _WIN32
#include <crtdbg.h>
#endif

void InkwyrdAudioApplication::initialise(const juce::String& commandLine)
{
    juce::ignoreUnused(commandLine);

#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    if (sodium_init() < 0)
    {
        logLine("[App] sodium_init failed");
        quit();
        return;
    }
    ix::initNetSystem();

    // Startup phase timing, kept permanently. The VST3 scan used to
    // block this thread for 18 seconds and nobody knew until a watchdog
    // was added; when a user reports a slow launch, the log should say
    // which step it was rather than needing another instrumented build.
    auto phaseStart = juce::Time::getMillisecondCounterHiRes();
    auto logPhase = [&phaseStart](const char* what)
    {
        auto now = juce::Time::getMillisecondCounterHiRes();
        auto elapsed = now - phaseStart;
        phaseStart = now;

        // Only the slow ones - a log line per trivial step is noise.
        if (elapsed >= 100.0)
            logLine("[App] startup: " + juce::String(what) + " took "
                     + juce::String(elapsed, 0) + " ms");
    };

    formatManager.registerBasicFormats(); // WAV/AIFF/FLAC/Ogg Vorbis
    formatManager.registerFormat(new Mp3AudioFormat(), false);
    formatManager.registerFormat(new MediaFoundationAudioFormat(), false); // AAC/M4A + WMA

    // Just the plugins the user chose. Deliberately NOT a folder scan:
    // that produced a list of everything installed and cost 15-20
    // seconds on a first launch. Reading this file is a few
    // milliseconds and loads no plugin binaries at all.
    scanner.restoreFromCache(getVoicePluginsFile());
    logLine("[App] " + juce::String(scanner.getNumKnownPlugins()) + " voice FX plugin(s) in your list.");
    logPhase("loading the voice FX plugin list");

    constexpr int kControlServerPort = 39231; // matches streamdeck-plugin/src/audioAppClient.ts
    if (!controlServer.start(kControlServerPort))
        logLine("[App] Warning: failed to start control server on port " + juce::String(kControlServerPort)
                 + " (Stream Deck integration won't work this run).");

    audioDeviceError = deviceManager.initialiseWithDefaultDevices(1, 2); // mic in, stereo out
    logPhase("opening the audio device");
    if (audioDeviceError.isNotEmpty())
        logLine("[App] Failed to open audio device: " + audioDeviceError);
    else
        deviceManager.addAudioCallback(&masterEngine);

    // Persist per-playlist shuffle, including when the Stream Deck
    // toggles it - the app can't just watch its own button for that.
    playlist.setShuffleChangedCallback([this](bool shuffleOn)
    {
        if (!activePlaylistId.isNull())
            library.setShuffle(activePlaylistId, shuffleOn);
    });

    library.loadAll();
    migratePlaylistLibraryIfNeeded();
    logPhase("loading playlists");

    // The master track list. Loaded AFTER the playlists, because its
    // one-time migration seeds itself from them.
    trackLibrary.setFile(TrackLibrary::getDefaultFile());
    trackLibrary.load();
    migrateTrackLibraryIfNeeded();
    logLine("[App] " + juce::String(trackLibrary.getNumTracks()) + " track(s) in your library.");
    logPhase("loading the track library");

    // Per-track trims, and the master fader position, both restored
    // before anything starts playing so nothing is briefly loud.
    trackGains.setFile(TrackSettingsStore::getDefaultFile());
    trackGains.load();
    playlist.setTrackGainProvider([this](const juce::File& file)
    {
        return trackGains.getLinearGain(file);
    });
    playlist.setTrackFadeProvider([this](const juce::File& file)
    {
        return trackGains.getFadeSeconds(file);
    });
    masterEngine.setMasterGain(settings.getMasterVolume());
    playlist.setCrossfadeEnabled(settings.isCrossfadeEnabled());
    playlist.setCrossfadeSeconds(settings.getCrossfadeSeconds());
    playlist.setLoopEnabled(settings.isLoopEnabled());
    playlist.setLoopGapSeconds(settings.getLoopGapSeconds());

    soundboardLayout.load();
    migrateSoundboardLayoutIfNeeded();
    registerSoundboardLayout();
    logPhase("loading the soundboard");

    mainWindow = std::make_unique<MainWindow>(getApplicationName(), settings);
    logPhase("creating the window");

    if (!library.isEmpty() || settings.isPlaylistFolderSet())
    {
        applyDefaultLocalMonitoring();
        showPlayer(); // after the above, so the Monitor button opens showing the right state
        logPhase("building the player screen");

        // Come back up on whichever playlist was last in use.
        auto* startupPlaylist = library.findById(juce::Uuid(settings.getActivePlaylistId()));
        if (startupPlaylist == nullptr)
            startupPlaylist = library.getPlaylist(0);

        if (startupPlaylist != nullptr)
            activatePlaylist(startupPlaylist->id);

        logPhase("starting the first playlist");

        startDiscordConnectIfConfigured();
    }
    else
    {
        showSetup();
    }
}

void InkwyrdAudioApplication::migratePlaylistLibraryIfNeeded()
{
    if (settings.isPlaylistLibraryMigrated())
        return;

    // Upgrading from the single-music-folder version: wrap that folder as
    // a live recursive folder link, which is exactly what the old
    // loadFolder() did, so the user's setup keeps working untouched.
    if (settings.isPlaylistFolderSet())
    {
        auto& migrated = library.createFromLegacyFolder(settings.getPlaylistFolder());
        settings.setActivePlaylistId(migrated.id.toDashedString());
        logLine("[App] Migrated existing music folder into playlist \"" + migrated.name + "\"");
    }

    settings.setPlaylistLibraryMigrated(true);
    settings.save(); // AppSettings has no autosave - this call is mandatory
}

void InkwyrdAudioApplication::migrateSoundboardLayoutIfNeeded()
{
    if (settings.isSoundboardLayoutMigrated())
        return;

    // Upgrading from the version where the board WAS the sound-effects
    // folder, alphabetically. importFolder() keeps that exact order and
    // keeps each sound's name as the bare filename, so every existing
    // Stream Deck button still matches afterwards.
    if (settings.getSoundboardFolder().isDirectory())
    {
        auto imported = soundboardLayout.importFolder(settings.getSoundboardFolder());
        logLine("[App] Migrated " + juce::String(imported) + " sound(s) from the sound effects folder "
                 "onto the soundboard");
    }

    settings.setSoundboardLayoutMigrated(true);
    settings.save(); // AppSettings has no autosave - this call is mandatory
}

void InkwyrdAudioApplication::migrateTrackLibraryIfNeeded()
{
    // An explicit flag, not "is the library empty?" - the same reasoning
    // as the two migrations above. Someone who deliberately clears every
    // track out of their library must not have it refilled from their
    // playlists on the next launch.
    if (settings.isTrackLibraryMigrated())
        return;

    juce::Array<juce::File> everything;
    for (int i = 0; i < library.getNumPlaylists(); ++i)
        if (auto* playlistToScan = library.getPlaylist(i))
            everything.addArray(library.resolve(*playlistToScan).files);

    // registerTracks dedupes by lowercased path, so a track in three
    // playlists still lands here once.
    auto added = trackLibrary.registerTracks(everything);
    logLine("[App] Seeded the track library with " + juce::String(added)
             + " track(s) from existing playlists");

    settings.setTrackLibraryMigrated(true);
    settings.save(); // AppSettings has no autosave - this call is mandatory
}

void InkwyrdAudioApplication::activatePlaylist(const juce::Uuid& id)
{
    auto* target = library.findById(id);
    if (target == nullptr)
        return;

    // Remember where the outgoing playlist got to, so coming back to it
    // resumes instead of starting over.
    if (!activePlaylistId.isNull() && activePlaylistId != id)
    {
        auto current = playlist.getCurrentTrackFile();
        if (current != juce::File())
            lastPlayedByPlaylistId[activePlaylistId.toDashedString()] = current;
    }

    activePlaylistId = id;
    settings.setActivePlaylistId(id.toDashedString());
    settings.save();

    playlist.setShuffle(target->shuffle);

    auto resolved = library.resolve(*target);

    juce::File resumeFrom;
    auto remembered = lastPlayedByPlaylistId.find(id.toDashedString());
    if (remembered != lastPlayedByPlaylistId.end())
        resumeFrom = remembered->second;

    // Crossfades if something is already playing, plain-starts if not.
    playlist.crossfadeToTracks(resolved.files, resumeFrom);

    if (libraryWindow != nullptr)
        libraryWindow->setPlayingPlaylistId(id);

    // Activating a playlist also brings it into view: playing something
    // you can't see the contents of would be a strange result.
    if (playlistWindow != nullptr)
        playlistWindow->getTrackList().setPlaylist(id);

    updateWarningBanner();
}

void InkwyrdAudioApplication::handlePlaylistSelected(const juce::Uuid& id)
{
    // Selection is browsing. It moves what the Playlist window shows and
    // deliberately does NOT touch playback - that's what activating
    // (double-click, or Play) is for.
    if (playlistWindow != nullptr)
        playlistWindow->getTrackList().setPlaylist(id);
}

void InkwyrdAudioApplication::playTrackInPlaylist(const juce::Uuid& playlistId, const juce::File& file)
{
    if (library.findById(playlistId) == nullptr)
        return;

    // Same list that's already running: jump within it, which keeps the
    // crossfade and the existing shuffle order.
    if (playlistId == activePlaylistId)
    {
        playlist.crossfadeToTrackInCurrentList(file);
        return;
    }

    // A different list: switch to it, then jump to the track that was
    // actually double-clicked rather than wherever the list would have
    // started on its own.
    activatePlaylist(playlistId);
    playlist.crossfadeToTrackInCurrentList(file);
}

void InkwyrdAudioApplication::handlePlaylistEdited(const juce::Uuid& id)
{
    // Editing a playlist you aren't listening to is purely a UI matter -
    // it gets picked up whenever it's next activated.
    if (id != activePlaylistId)
        return;

    auto* target = library.findById(id);
    if (target == nullptr)
    {
        // The playlist being played was DELETED. Carrying on playing a
        // list that no longer exists leaves audio running with nothing
        // on screen owning it, and no obvious way to stop it.
        playlist.stop();
        activePlaylistId = juce::Uuid();
        settings.setActivePlaylistId({});
        settings.save();

        if (libraryWindow != nullptr)
            libraryWindow->setPlayingPlaylistId({});
        if (playlistWindow != nullptr)
            playlistWindow->getTrackList().setPlaylist({});
        if (playerWindow != nullptr)
            playerWindow->getPlayerComponent().refreshToggleStates();

        updateWarningBanner();
        return;
    }

    // Preserving order rather than setTracks(): this is an edit to the
    // list that's already playing, so it must not restart the track,
    // reshuffle what's left, or jump back to the top of the list.
    playlist.updateTracksPreservingOrder(library.resolve(*target).files);

    updateWarningBanner();
}

void InkwyrdAudioApplication::shutdown()
{
    if (sender != nullptr)
        sender->stop();
    masterEngine.setDiscordSender(nullptr);
    discordConnector.disconnect();

    deviceManager.removeAudioCallback(&masterEngine);
    playlist.stop();
    controlServer.stop();

    playerWindow.reset();
    playlistWindow.reset();
    libraryWindow.reset();
    voiceFxWindow.reset();
    soundboardWindow.reset();
    mainWindow.reset();

    ix::uninitNetSystem();
}

void InkwyrdAudioApplication::showSetup()
{
    // First run is the case where there's nothing to come back to: no
    // player view has ever been shown this session.
    auto isFirstRun = playerWindow == nullptr && !hasShownPlayer;

    // Hide every window in the Player-side layout before Setup takes
    // over. They're independent windows now, not children of one
    // PlayerComponent that Setup's content-swap would delete - "hide" is
    // all that's needed, nothing is destroyed and nothing dangles.
    if (playerWindow != nullptr)
        playerWindow->setVisible(false);
    if (playlistWindow != nullptr)
        playlistWindow->setVisible(false);
    if (libraryWindow != nullptr)
        libraryWindow->setVisible(false);
    if (voiceFxWindow != nullptr)
    {
        voiceFxWasVisibleBeforeSetup = voiceFxWindow->isVisible();
        voiceFxWindow->setVisible(false);
    }
    if (soundboardWindow != nullptr)
    {
        soundboardWasVisibleBeforeSetup = soundboardWindow->isVisible();
        soundboardWindow->setVisible(false);
    }

    mainWindow->showSetupView(settings, isFirstRun,
                               [this](SetupComponent::Result result) { completeSetupAndLaunch(result); });
    mainWindow->setVisible(true);
    mainWindow->toFront(true);
}

juce::File InkwyrdAudioApplication::getVoicePluginsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Inkwyrd Audio")
        .getChildFile("voice-plugins.xml");
}

void InkwyrdAudioApplication::saveVoicePlugins()
{
    scanner.saveToCache(getVoicePluginsFile());
}

void InkwyrdAudioApplication::applyDefaultLocalMonitoring()
{
    // ALWAYS off at startup. The host is normally already in the call
    // when they open the app, so playing locally as well means hearing
    // every track twice, slightly offset. Waiting until the connection
    // completes to switch it off isn't good enough - the doubling
    // happens during the connect window, which can last indefinitely.
    //
    // This used to be `!hasDiscordCredentials()`, i.e. ON whenever
    // Discord wasn't set up, reasoning that local-only mode would
    // otherwise be silent with nothing explaining why. That produced
    // exactly the surprise it was meant to avoid: launching with an
    // empty bot token started playing out of the speakers immediately.
    // The silence problem is now solved by SAYING SO - PlayerComponent
    // shows a "Monitor is off" hint - rather than by overriding the
    // user's stated default.
    masterEngine.setLocalMonitoring(false);
}

void InkwyrdAudioApplication::showPlayer()
{
    hasShownPlayer = true;

    mainWindow->setVisible(false);

    if (playerWindow == nullptr)
    {
        // First time this run - build the whole layout. From here on
        // these five windows live for the rest of the app's life;
        // Settings only ever hides them (see showSetup()).
        playerWindow = std::make_unique<PlayerWindow>(
            settings, playlist, masterEngine,
            [this]
            {
                if (voiceFxWindow != nullptr)
                    voiceFxWindow->setVisible(!voiceFxWindow->isVisible());
            },
            [this]
            {
                if (soundboardWindow != nullptr)
                    soundboardWindow->setVisible(!soundboardWindow->isVisible());
            },
            [this] { showSetup(); });

        playlistWindow = std::make_unique<PlaylistWindow>(
            settings, library, playlist,
            [this](const juce::Uuid& id) { handlePlaylistEdited(id); },
            [this](const juce::Uuid& id, const juce::File& file) { playTrackInPlaylist(id, file); });

        libraryWindow = std::make_unique<LibraryWindow>(
            settings, library, trackLibrary, playlist, trackGains,
            [this](const juce::Uuid& id) { activatePlaylist(id); },
            [this](const juce::Uuid& id) { handlePlaylistEdited(id); },
            [this](const juce::Uuid& id) { handlePlaylistSelected(id); });

        voiceFxWindow = std::make_unique<VoiceFxWindow>(settings, scanner, voiceChain,
                                                          [this] { saveVoicePlugins(); });

        soundboardWindow = std::make_unique<SoundboardWindow>(settings, soundboard, soundboardLayout,
                                                                [this] { registerSoundboardLayout(); });
    }
    else
    {
        // Returning from Settings. Player/Playlist/Library are core, so
        // they always come back; Voice FX/Soundboard restore to exactly
        // what they were right before Settings hid them, rather than
        // being forced open.
        playerWindow->setVisible(true);
        playlistWindow->setVisible(true);
        libraryWindow->setVisible(true);
        if (voiceFxWindow != nullptr)
            voiceFxWindow->setVisible(voiceFxWasVisibleBeforeSetup);
        if (soundboardWindow != nullptr)
            soundboardWindow->setVisible(soundboardWasVisibleBeforeSetup);
    }

    auto& player = playerWindow->getPlayerComponent();

    // So it can point out that Monitor being off means silence when
    // there's no Discord to send to either.
    player.setDiscordConfigured(settings.hasDiscordCredentials());
    player.setMasterVolume(settings.getMasterVolume());
    player.setPlaybackSettings(settings.isCrossfadeEnabled(),
                                settings.getCrossfadeSeconds(),
                                settings.getFadeOutSeconds(),
                                settings.isLoopEnabled(),
                                settings.getLoopGapSeconds());
    player.setPlaybackSettingsChangedCallback([this]
    {
        settings.setCrossfadeEnabled(playlist.isCrossfadeEnabled());
        settings.setCrossfadeSeconds(playlist.getCrossfadeSeconds());
        settings.setFadeOutSeconds(playerWindow->getPlayerComponent().getFadeOutSeconds());
        settings.setLoopEnabled(playlist.isLoopEnabled());
        settings.setLoopGapSeconds(playlist.getLoopGapSeconds());
        settings.save();
    });
    player.setMasterVolumeChangedCallback([this](float volume)
    {
        settings.setMasterVolume(volume);
        settings.save();
    });

    if (!activePlaylistId.isNull())
    {
        libraryWindow->setPlayingPlaylistId(activePlaylistId);
        playlistWindow->getTrackList().setPlaylist(activePlaylistId);
    }
    else
    {
        // Nothing playing yet, so show whatever the Library window has
        // selected rather than leaving the Playlist window blank.
        playlistWindow->getTrackList().setPlaylist(libraryWindow->getPanel().getSelectedPlaylistId());
    }

    updateWarningBanner();
}

void InkwyrdAudioApplication::updateWarningBanner()
{
    auto* player = playerWindow != nullptr ? &playerWindow->getPlayerComponent() : nullptr;
    if (player == nullptr)
        return;

    // All of these otherwise produce silence, or a missing playlist, with
    // no visible reason at all.
    juce::StringArray warnings;

    if (audioDeviceError.isNotEmpty())
        warnings.add("Audio device failed to open: " + audioDeviceError
                      + " - no sound (mic, playlist or Discord) will work until this is fixed.");

    if (auto* active = library.findById(activePlaylistId))
    {
        auto resolved = library.resolve(*active);

        if (resolved.files.isEmpty())
            warnings.add("\"" + active->name + "\" has no playable audio files - add files or a folder "
                          "containing WAV, AIFF, FLAC, Ogg, MP3, AAC/M4A or WMA.");
        else if (!resolved.missingPaths.isEmpty())
            warnings.add(juce::String(resolved.missingPaths.size())
                          + " track(s) in \"" + active->name + "\" are missing from disk and were skipped.");
    }

    warnings.addArray(library.getLoadWarnings());

    player->setWarningBanner(warnings.joinIntoString("  |  "));
}

void InkwyrdAudioApplication::completeSetupAndLaunch(SetupComponent::Result result)
{
    settings.setPlaylistFolder(result.playlistFolder);
    settings.setSoundboardFolder(result.soundboardFolder);
    settings.setBotToken(result.botToken);
    settings.setGuildId(result.guildId);
    settings.setChannelId(result.channelId);
    settings.save();

    // The Setup screen's music folder now seeds a playlist rather than
    // being the one and only source. Idempotent: re-saving Settings
    // without changing the folder must not pile up duplicate playlists.
    if (result.playlistFolder.isDirectory())
    {
        Playlist* existing = nullptr;
        for (int i = 0; i < library.getNumPlaylists() && existing == nullptr; ++i)
        {
            auto* candidate = library.getPlaylist(i);
            for (const auto& entry : candidate->entries)
                if (entry.kind == PlaylistEntry::Kind::folder && entry.path == result.playlistFolder)
                    existing = candidate;
        }

        if (existing == nullptr)
            existing = &library.createFromLegacyFolder(result.playlistFolder);

        activatePlaylist(existing->id);
    }

    // Non-destructive: the board is the source of truth now, so changing
    // the folder ADDS anything new from it to the free buttons rather
    // than rebuilding the board and throwing away an arrangement the
    // user has set up by hand.
    if (result.soundboardFolder.isDirectory())
        soundboardLayout.importFolder(result.soundboardFolder);

    registerSoundboardLayout();

    // Only on the first pass through setup. Re-applying it on every save
    // would silently undo a monitor toggle the user had deliberately
    // flipped, just because they went in to change a folder.
    if (!discordConnectStarted)
        applyDefaultLocalMonitoring();

    showPlayer();

    if (!discordConnectStarted)
    {
        // Nothing has connected yet this run, so new credentials take
        // effect right now - no restart needed. This is the common case
        // after launching with an empty or cleared bot token.
        startDiscordConnectIfConfigured();
    }
    else if (playerWindow != nullptr)
    {
        auto& player = playerWindow->getPlayerComponent();

        // Discord credentials are never live-reconnected once a connect
        // attempt has already happened this run - the DAVE/MLS handshake
        // has only ever been verified via the connect-once-then-shutdown
        // path (see CLAUDE.md). Saved above; takes effect next launch.
        // Only show the restart notice when Discord credentials are
        // actually configured - otherwise a folder-only settings change
        // would misleadingly imply a Discord setting changed too.
        if (settings.hasDiscordCredentials())
        {
            player.setDiscordStatus(discordConnector.isConnected()
                                         ? "Connected - streaming to Discord. Restart to apply changed Discord settings."
                                         : "Restart Inkwyrd Audio to apply changed Discord settings.");
            offerRestart();
        }
        else
        {
            player.setDiscordStatus("Local monitor only - no Discord credentials configured.");
        }
    }
}

void InkwyrdAudioApplication::startDiscordConnectIfConfigured()
{
    if (!settings.hasDiscordCredentials())
    {
        if (playerWindow != nullptr)
            playerWindow->getPlayerComponent().setDiscordStatus("Local monitor only - no Discord credentials configured.");
        return;
    }

    discordConnectStarted = true;

    discordConnector.connectAsync(settings.getBotToken(), settings.getGuildId(), settings.getChannelId(),
        [this](juce::String status)
        {
            if (playerWindow != nullptr)
                playerWindow->getPlayerComponent().setDiscordStatus(status);
        },
        [this](bool success)
        {
            if (!success || playerWindow == nullptr)
                return;

            sender = std::make_unique<DiscordAudioSender>(*discordConnector.getVoiceGateway(), discordConnector.getUdpSocket());
            sender->start();
            masterEngine.setDiscordSender(sender.get());

            // The host is in the Discord call too, so they hear this mix
            // via the bot. Playing it locally as well doubles everything
            // with a slight offset, which reads as an annoying delay.
            masterEngine.setLocalMonitoring(false);
            playerWindow->getPlayerComponent().refreshToggleStates();
        });
}

void InkwyrdAudioApplication::offerRestart()
{
    // Only ever called from the discordConnectStarted branch of
    // completeSetupAndLaunch(), which requires Player to have already
    // been shown this run - playerWindow is guaranteed to exist here.
    auto options = inkwyrd::dialogOptions(playerWindow.get(), juce::MessageBoxIconType::QuestionIcon,
                                           "Restart to apply Discord settings",
                                           "Your settings are saved, but Discord settings only take "
                                           "effect when Inkwyrd Audio restarts.\n\n"
                                           "Restart now?")
                        .withButton("Restart now")
                        .withButton("Later");

    juce::AlertWindow::showAsync(options, [this](int result)
    {
        if (result != 1)
            return;

        // The app refuses to run twice at once (one audio device, one bot
        // token, one control-server port), so the replacement can't just
        // be started here - it would find this instance still alive and
        // quit immediately. Hand the relaunch to a detached shell that
        // waits for this process to go away first.
        auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

        juce::ChildProcess relauncher;
        relauncher.start("cmd.exe /c ping -n 4 127.0.0.1 > nul & start \"\" \""
                          + exe.getFullPathName() + "\"");

        quit();
    });
}

void InkwyrdAudioApplication::registerSoundboardLayout()
{
    // Clear first. This used to leak: changing the soundboard folder
    // emptied the GUI's list but left every old sound registered in the
    // engine, so the Stream Deck could still trigger sounds the app no
    // longer showed anywhere. The same applies to a renamed or cleared
    // slot now.
    soundboard.clearSounds();

    for (const auto& slot : soundboardLayout.getFilledSlots())
    {
        // A slot whose file has gone (moved, or an unplugged drive) stays
        // ON the board - the button shows it as missing - but isn't
        // registered, so pressing it does nothing rather than throwing.
        if (slot.file.existsAsFile())
            soundboard.registerSound(slot.name, slot.file,
                                      juce::Decibels::decibelsToGain(slot.gainDb));
    }
}
