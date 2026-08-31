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

    mainWindow = std::make_unique<MainWindow>(getApplicationName());

    if (settings.isPlaylistFolderSet())
    {
        playlist.loadFolder(settings.getPlaylistFolder());
        playlist.start();

        if (settings.getSoundboardFolder().isDirectory())
            soundNames = registerSoundboardFolder(settings.getSoundboardFolder());

        showPlayer();

        discordConnectAttempted = true;
        startDiscordConnectIfConfigured();
    }
    else
    {
        showSetup();
    }
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

void InkwyrdAudioApplication::showPlayer()
{
    mainWindow->showPlayerView(playlist, soundboard, masterEngine, scanner, voiceChain, foundPlugins, soundNames,
                                [this] { showSetup(); });

    if (audioDeviceError.isNotEmpty())
        if (auto* player = mainWindow->getPlayerComponent())
            player->setAudioDeviceStatus("Audio device failed to open: " + audioDeviceError
                                          + " - no sound (mic, playlist, or Discord) will work until this is fixed.");
}

void InkwyrdAudioApplication::completeSetupAndLaunch(SetupComponent::Result result)
{
    settings.setPlaylistFolder(result.playlistFolder);
    settings.setSoundboardFolder(result.soundboardFolder);
    settings.setBotToken(result.botToken);
    settings.setGuildId(result.guildId);
    settings.setChannelId(result.channelId);
    settings.save();

    // Folder changes hot-swap immediately - loadFolder() only rebuilds
    // the internal play order, and start() cleanly (re)loads deck 0, so
    // this is safe to do live. One playback interruption is expected -
    // the user just asked to change the folder.
    playlist.stop();
    playlist.loadFolder(result.playlistFolder);
    playlist.start();

    soundNames.clear();
    if (result.soundboardFolder.isDirectory())
        soundNames = registerSoundboardFolder(result.soundboardFolder);

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
                                          ? "Connected to Discord. Restart to apply changed Discord settings."
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
        });
}

juce::StringArray InkwyrdAudioApplication::registerSoundboardFolder(const juce::File& folder)
{
    juce::StringArray names;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFiles))
    {
        auto file = entry.getFile();
        if (formatManager.findFormatForFileExtension(file.getFileExtension()) == nullptr)
            continue;
        auto name = file.getFileNameWithoutExtension();
        soundboard.registerSound(name, file);
        names.add(name);
    }
    return names;
}
