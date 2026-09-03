#include "InkwyrdAudioApplication.h"
#include "Mp3AudioFormat.h"
#include "MediaFoundationAudioFormat.h"
#include "Log.h"

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

    formatManager.registerBasicFormats(); // WAV/AIFF/FLAC/Ogg Vorbis
    formatManager.registerFormat(new Mp3AudioFormat(), false);
    formatManager.registerFormat(new MediaFoundationAudioFormat(), false); // AAC/M4A + WMA

    logLine("[App] Scanning for VST3 plugins...");
    foundPlugins = scanner.scan();
    logLine("[App] Found " + juce::String(foundPlugins.size()) + " plugin(s).");

    constexpr int kControlServerPort = 39231; // matches streamdeck-plugin/src/audioAppClient.ts
    if (!controlServer.start(kControlServerPort))
        logLine("[App] Warning: failed to start control server on port " + juce::String(kControlServerPort)
                 + " (Stream Deck integration won't work this run).");

    audioDeviceError = deviceManager.initialiseWithDefaultDevices(1, 2); // mic in, stereo out
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

    mainWindow = std::make_unique<MainWindow>(getApplicationName());

    if (!library.isEmpty() || settings.isPlaylistFolderSet())
    {
        if (settings.getSoundboardFolder().isDirectory())
            registerSoundboardFolder(settings.getSoundboardFolder());

        applyDefaultLocalMonitoring();
        showPlayer(); // after the above, so the Monitor button opens showing the right state

        // Come back up on whichever playlist was last in use.
        auto* startupPlaylist = library.findById(juce::Uuid(settings.getActivePlaylistId()));
        if (startupPlaylist == nullptr)
            startupPlaylist = library.getPlaylist(0);

        if (startupPlaylist != nullptr)
            activatePlaylist(startupPlaylist->id);

        discordConnectAttempted = true;
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

    if (auto* player = mainWindow->getPlayerComponent())
        player->setPlayingPlaylistId(id);

    updateWarningBanner();
}

void InkwyrdAudioApplication::handlePlaylistEdited(const juce::Uuid& id)
{
    // Editing a playlist you aren't listening to is purely a UI matter -
    // it gets picked up whenever it's next activated.
    if (id != activePlaylistId)
        return;

    auto* target = library.findById(id);
    if (target == nullptr)
        return;

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

    mainWindow.reset();

    ix::uninitNetSystem();
}

void InkwyrdAudioApplication::showSetup()
{
    mainWindow->showSetupView(settings, [this](SetupComponent::Result result) { completeSetupAndLaunch(result); });
}

void InkwyrdAudioApplication::applyDefaultLocalMonitoring()
{
    // Off by default whenever Discord is configured. The host is
    // normally already in the call when they open the app, so playing
    // locally as well means hearing every track twice, slightly offset.
    // Waiting until the connection completes to switch it off isn't good
    // enough - the doubling happens during the connect window, which can
    // last indefinitely while waiting for someone to join the channel.
    //
    // With no Discord configured, local output is the only way to hear
    // anything at all, so it stays on - otherwise "local monitor only"
    // mode would be completely silent with nothing explaining why.
    masterEngine.setLocalMonitoring(!settings.hasDiscordCredentials());
}

void InkwyrdAudioApplication::showPlayer()
{
    mainWindow->showPlayerView(playlist, soundboard, masterEngine, scanner, voiceChain, foundPlugins,
                                library,
                                [this](const juce::Uuid& id) { activatePlaylist(id); },
                                [this](const juce::Uuid& id) { handlePlaylistEdited(id); },
                                [this] { showSetup(); });

    if (auto* player = mainWindow->getPlayerComponent())
    {
        player->setSoundNames(soundboard.getRegisteredNames());
        if (!activePlaylistId.isNull())
            player->setPlayingPlaylistId(activePlaylistId);
    }

    updateWarningBanner();
}

void InkwyrdAudioApplication::updateWarningBanner()
{
    auto* player = mainWindow != nullptr ? mainWindow->getPlayerComponent() : nullptr;
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

    registerSoundboardFolder(result.soundboardFolder);

    // Only on the first pass through setup. Re-applying it on every save
    // would silently undo a monitor toggle the user had deliberately
    // flipped, just because they went in to change a folder.
    if (!discordConnectAttempted)
        applyDefaultLocalMonitoring();

    showPlayer();

    if (!discordConnectAttempted)
    {
        discordConnectAttempted = true;
        startDiscordConnectIfConfigured();
    }
    else if (auto* player = mainWindow->getPlayerComponent())
    {
        // Discord credentials are never live-reconnected once a connect
        // attempt has already happened this run - the DAVE/MLS handshake
        // has only ever been verified via the connect-once-then-shutdown
        // path (see CLAUDE.md). Saved above; takes effect next launch.
        // Only show the restart notice when Discord credentials are
        // actually configured - otherwise a folder-only settings change
        // would misleadingly imply a Discord setting changed too.
        if (settings.hasDiscordCredentials())
            player->setDiscordStatus(discordConnector.isConnected()
                                          ? "Connected - streaming to Discord. Restart to apply changed Discord settings."
                                          : "Restart Inkwyrd Audio to apply changed Discord settings.");
        else
            player->setDiscordStatus("Local monitor only - no Discord credentials configured.");
    }
}

void InkwyrdAudioApplication::startDiscordConnectIfConfigured()
{
    if (!settings.hasDiscordCredentials())
    {
        if (auto* player = mainWindow->getPlayerComponent())
            player->setDiscordStatus("Local monitor only - no Discord credentials configured.");
        return;
    }

    discordConnector.connectAsync(settings.getBotToken(), settings.getGuildId(), settings.getChannelId(),
        [this](juce::String status)
        {
            if (mainWindow != nullptr)
                if (auto* player = mainWindow->getPlayerComponent())
                    player->setDiscordStatus(status);
        },
        [this](bool success)
        {
            if (!success || mainWindow == nullptr)
                return;

            sender = std::make_unique<DiscordAudioSender>(*discordConnector.getVoiceGateway(), discordConnector.getUdpSocket());
            sender->start();
            masterEngine.setDiscordSender(sender.get());

            // The host is in the Discord call too, so they hear this mix
            // via the bot. Playing it locally as well doubles everything
            // with a slight offset, which reads as an annoying delay.
            masterEngine.setLocalMonitoring(false);
            if (auto* player = mainWindow->getPlayerComponent())
                player->refreshToggleStates();
        });
}

void InkwyrdAudioApplication::registerSoundboardFolder(const juce::File& folder)
{
    // Clear first. This used to leak: changing the soundboard folder
    // emptied the GUI's list but left every old sound registered in the
    // engine, so the Stream Deck could still trigger sounds the app no
    // longer showed anywhere.
    soundboard.clearSounds();

    if (!folder.isDirectory())
        return;

    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFiles))
    {
        auto file = entry.getFile();
        if (formatManager.findFormatForFileExtension(file.getFileExtension()) == nullptr)
            continue;

        // Name stays the bare filename: existing Stream Deck buttons
        // carry these strings, and changing the scheme would silently
        // stop every one of them matching.
        soundboard.registerSound(file.getFileNameWithoutExtension(), file);
    }
}
