#include <cstdlib>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <crtdbg.h>
#endif

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <ixwebsocket/IXNetSystem.h>
#include <sodium.h>

#include "GatewayClient.h"
#include "VoiceGatewayClient.h"
#include "VoiceUdpSocket.h"
#include "Log.h"

#include "PlaylistEngine.h"
#include "SoundboardEngine.h"
#include "Mp3AudioFormat.h"
#include "MediaFoundationAudioFormat.h"
#include "PluginScanner.h"
#include "PluginChain.h"
#include "MasterEngine.h"
#include "DiscordAudioSender.h"
#include "ControlServer.h"

namespace
{
    // Same join/handshake sequence proven in src/discord-spike/Main.cpp,
    // just without the generated test tone at the end - the live master
    // mix takes over via DiscordAudioSender instead. See
    // docs/dave-protocol-notes.md for why each step is here.
    struct DiscordConnection
    {
        std::unique_ptr<GatewayClient> gateway;
        std::unique_ptr<VoiceGatewayClient> voiceGateway;
        VoiceUdpSocket udp;
        bool connected = false;
    };

    bool attemptDiscordConnection(DiscordConnection& conn, const juce::String& botToken,
                                   const juce::String& guildId, const juce::String& channelId);

    bool connectToDiscord(DiscordConnection& conn, const juce::String& botToken,
                           const juce::String& guildId, const juce::String& channelId)
    {
        if (attemptDiscordConnection(conn, botToken, guildId, channelId))
            return true;

        // A failed attempt's connections would otherwise stay open
        // until the whole process exits (their destructors do
        // disconnect, but not until then) - leaving a stale Discord
        // session alive for the rest of the run. Tear down now instead.
        if (conn.voiceGateway != nullptr)
            conn.voiceGateway->disconnect();
        if (conn.gateway != nullptr)
            conn.gateway->disconnect();
        return false;
    }

    bool attemptDiscordConnection(DiscordConnection& conn, const juce::String& botToken,
                                   const juce::String& guildId, const juce::String& channelId)
    {
        logLine("[App] Connecting to Discord gateway...");
        conn.gateway = std::make_unique<GatewayClient>(botToken);
        conn.gateway->connect();

        if (!conn.gateway->waitForReady(10000))
        {
            logLine("[App] Timed out waiting for gateway READY.");
            return false;
        }

        logLine("[App] Resetting any stale voice state first...");
        conn.gateway->requestJoinVoiceChannel(guildId, "");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        logLine("[App] Requesting to join voice channel " + channelId);
        conn.gateway->requestJoinVoiceChannel(guildId, channelId);

        GatewayClient::VoiceServerInfo serverInfo;
        if (!conn.gateway->waitForVoiceServerInfo(serverInfo, 10000))
        {
            logLine("[App] Timed out waiting for voice server info.");
            return false;
        }

        conn.voiceGateway = std::make_unique<VoiceGatewayClient>(serverInfo.endpoint, serverInfo.guildId, channelId,
                                                                   conn.gateway->getBotUserId(), serverInfo.sessionId,
                                                                   serverInfo.voiceToken);
        conn.voiceGateway->connect();

        VoiceGatewayClient::ReadyInfo voiceReady;
        if (!conn.voiceGateway->waitForReady(voiceReady, 10000))
        {
            logLine("[App] Timed out waiting for voice Ready.");
            return false;
        }

        if (!conn.udp.bindSocket())
        {
            logLine("[App] Failed to bind local UDP socket.");
            return false;
        }

        juce::String externalIp;
        int externalPort = 0;
        if (!conn.udp.performIpDiscovery(voiceReady.ip, voiceReady.port, voiceReady.ssrc, externalIp, externalPort))
        {
            logLine("[App] IP discovery failed.");
            return false;
        }

        conn.udp.setDestination(voiceReady.ip, voiceReady.port);
        conn.voiceGateway->selectProtocol(externalIp, externalPort);

        VoiceGatewayClient::SessionInfo session;
        if (!conn.voiceGateway->waitForSessionDescription(session, 10000))
        {
            logLine("[App] Timed out waiting for session description.");
            return false;
        }

        conn.udp.setSecretKey(session.secretKey, voiceReady.ssrc);

        logLine("[App] Waiting for DAVE encryption handshake to complete...");
        if (!conn.voiceGateway->waitForDaveReady(15000))
        {
            logLine("[App] Timed out waiting for DAVE handshake.");
            return false;
        }

        conn.voiceGateway->sendSpeaking(voiceReady.ssrc, true);
        conn.connected = true;
        logLine("[App] Connected and streaming to Discord.");
        return true;
    }

    void disconnectFromDiscord(DiscordConnection& conn, const juce::String& guildId)
    {
        if (!conn.connected)
            return;
        conn.gateway->requestJoinVoiceChannel(guildId, "");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        conn.voiceGateway->disconnect();
        conn.gateway->disconnect();
        conn.connected = false;
    }

    juce::StringArray registerSoundboardFolder(SoundboardEngine& soundboard,
                                                juce::AudioFormatManager& formatManager,
                                                const juce::File& folder)
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
}

int main(int argc, char* argv[])
{
    juce::ignoreUnused(argc, argv);

#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    if (sodium_init() < 0)
    {
        std::cout << "sodium_init failed" << std::endl;
        return 1;
    }
    ix::initNetSystem();

    auto botToken = juce::SystemStats::getEnvironmentVariable("DISCORD_BOT_TOKEN", "");
    auto guildId = juce::SystemStats::getEnvironmentVariable("DISCORD_GUILD_ID", "");
    auto channelId = juce::SystemStats::getEnvironmentVariable("DISCORD_CHANNEL_ID", "");
    auto playlistFolder = juce::SystemStats::getEnvironmentVariable("PLAYLIST_FOLDER", "");
    auto soundboardFolder = juce::SystemStats::getEnvironmentVariable("SOUNDBOARD_FOLDER", "");

    if (playlistFolder.isEmpty())
    {
        std::cout << "Set PLAYLIST_FOLDER first (SOUNDBOARD_FOLDER is optional). "
                     "DISCORD_BOT_TOKEN/DISCORD_GUILD_ID/DISCORD_CHANNEL_ID are also optional - "
                     "without them this runs in local-monitor-only mode." << std::endl;
        return 1;
    }

    bool haveDiscordCredentials = botToken.isNotEmpty() && guildId.isNotEmpty() && channelId.isNotEmpty();

    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats(); // WAV/AIFF/FLAC/Ogg Vorbis
    formatManager.registerFormat(new Mp3AudioFormat(), false);
    formatManager.registerFormat(new MediaFoundationAudioFormat(), false); // AAC/M4A + WMA

    PlaylistEngine playlist(formatManager);
    SoundboardEngine soundboard(formatManager);
    PluginScanner scanner;
    PluginChain voiceChain;

    auto soundNames = soundboardFolder.isNotEmpty()
                           ? registerSoundboardFolder(soundboard, formatManager, juce::File(soundboardFolder))
                           : juce::StringArray();

    std::cout << "Scanning for VST3 plugins..." << std::endl;
    auto foundPlugins = scanner.scan();
    std::cout << "Found " << foundPlugins.size() << " plugin(s). Use 'p' to list them." << std::endl;

    MasterEngine masterEngine(playlist, soundboard, voiceChain);

    ControlServer controlServer(playlist, soundboard, masterEngine);
    constexpr int kControlServerPort = 39231; // matches streamdeck-plugin/src/audioAppClient.ts
    if (!controlServer.start(kControlServerPort))
        std::cout << "Warning: failed to start control server on port " << kControlServerPort
                   << " (Stream Deck integration won't work this run)." << std::endl;

    juce::AudioDeviceManager deviceManager;
    auto openError = deviceManager.initialiseWithDefaultDevices(1, 2); // mic in, stereo out
    if (openError.isNotEmpty())
    {
        std::cout << "Failed to open audio device: " << openError.toStdString() << std::endl;
        return 1;
    }
    deviceManager.addAudioCallback(&masterEngine);

    playlist.loadFolder(juce::File(playlistFolder));
    playlist.start();

    DiscordConnection discord;
    std::unique_ptr<DiscordAudioSender> sender;
    if (!haveDiscordCredentials)
    {
        std::cout << "No Discord credentials set - running in local-monitor-only mode." << std::endl;
    }
    else if (connectToDiscord(discord, botToken, guildId, channelId))
    {
        sender = std::make_unique<DiscordAudioSender>(*discord.voiceGateway, discord.udp);
        sender->start();
        masterEngine.setDiscordSender(sender.get());
    }
    else
    {
        std::cout << "Discord connection failed - running in local-monitor-only mode." << std::endl;
    }

    std::cout << "Commands: s = skip track, h = toggle shuffle, t = now playing, m = toggle mic mute, "
                 "p = list VST3 plugins, a <index> = add to voice chain, r <index> = remove from chain, "
                 "l = list chain";
    if (!soundNames.isEmpty())
    {
        std::cout << ", soundboard:";
        for (int i = 0; i < soundNames.size(); ++i)
            std::cout << " " << i << "=" << soundNames[i];
    }
    std::cout << ", q = quit" << std::endl;

    bool shuffle = true;

    std::thread inputThread([&]
    {
        std::string line;
        while (true)
        {
            std::cout << "> ";
            if (!std::getline(std::cin, line) || line == "q")
            {
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            }
            if (line.empty())
                continue;

            if (line == "s")
            {
                juce::MessageManager::callAsync([&playlist] { playlist.skipToNext(); });
            }
            else if (line == "h")
            {
                juce::MessageManager::callAsync([&playlist, &shuffle]
                {
                    shuffle = !shuffle;
                    playlist.setShuffle(shuffle);
                    std::cout << "Shuffle: " << (shuffle ? "on" : "off") << std::endl;
                });
            }
            else if (line == "t")
            {
                juce::MessageManager::callAsync([&playlist]
                {
                    std::cout << "Now playing: " << playlist.getCurrentTrackName()
                               << (playlist.isCrossfading() ? " (crossfading)" : "") << std::endl;
                });
            }
            else if (line == "m")
            {
                juce::MessageManager::callAsync([&masterEngine]
                {
                    masterEngine.setMicMuted(!masterEngine.isMicMuted());
                    std::cout << "Mic: " << (masterEngine.isMicMuted() ? "muted" : "unmuted") << std::endl;
                });
            }
            else if (line == "p")
            {
                juce::MessageManager::callAsync([&foundPlugins]
                {
                    for (int i = 0; i < foundPlugins.size(); ++i)
                        std::cout << "  " << i << ": " << foundPlugins[i].name.toStdString() << std::endl;
                });
            }
            else if (line == "l")
            {
                juce::MessageManager::callAsync([&voiceChain]
                {
                    auto n = voiceChain.getNumPlugins();
                    if (n == 0) { std::cout << "Voice chain is empty." << std::endl; return; }
                    for (int i = 0; i < n; ++i)
                        std::cout << "  " << i << ": " << voiceChain.getPluginName(i).toStdString() << std::endl;
                });
            }
            else if (line.size() > 2 && (line[0] == 'a' || line[0] == 'r') && line[1] == ' ')
            {
                auto index = std::atoi(line.c_str() + 2);
                bool isAdd = (line[0] == 'a');
                juce::MessageManager::callAsync([&voiceChain, &scanner, &foundPlugins, index, isAdd]
                {
                    if (isAdd)
                    {
                        if (index < 0 || index >= foundPlugins.size()) { std::cout << "No such plugin index." << std::endl; return; }
                        juce::String error;
                        if (voiceChain.addPlugin(scanner, foundPlugins[index], error))
                            std::cout << "Added: " << foundPlugins[index].name.toStdString() << std::endl;
                        else
                            std::cout << "Failed: " << error.toStdString() << std::endl;
                    }
                    else
                    {
                        if (index < 0 || index >= voiceChain.getNumPlugins()) { std::cout << "No such chain index." << std::endl; return; }
                        auto name = voiceChain.getPluginName(index);
                        voiceChain.removePlugin(index);
                        std::cout << "Removed: " << name.toStdString() << std::endl;
                    }
                });
            }
            else
            {
                auto index = std::atoi(line.c_str());
                if (index >= 0 && index < soundNames.size())
                {
                    auto name = soundNames[index];
                    juce::MessageManager::callAsync([&soundboard, name] { soundboard.trigger(name); });
                }
                else
                {
                    std::cout << "Unknown command." << std::endl;
                }
            }
        }
    });

    juce::MessageManager::getInstance()->runDispatchLoop();
    inputThread.join();

    if (sender != nullptr)
        sender->stop();
    masterEngine.setDiscordSender(nullptr);
    disconnectFromDiscord(discord, guildId);

    deviceManager.removeAudioCallback(&masterEngine);
    playlist.stop();
    controlServer.stop();

    ix::uninitNetSystem();
    return 0;
}
