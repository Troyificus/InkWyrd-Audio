#include "DiscordConnector.h"
#include "Log.h"

#include <chrono>
#include <juce_events/juce_events.h>

DiscordConnector::~DiscordConnector()
{
    disconnect();
}

void DiscordConnector::connectAsync(const juce::String& botToken, const juce::String& guildId,
                                     const juce::String& channelId, StatusCallback onStatus,
                                     CompleteCallback onComplete)
{
    guildIdForDisconnect = guildId;

    connectThread = std::make_unique<std::thread>([this, botToken, guildId, channelId, onStatus, onComplete]
    {
        runConnectSequence(botToken, guildId, channelId, onStatus, onComplete);
    });
}

void DiscordConnector::runConnectSequence(juce::String botToken, juce::String guildId, juce::String channelId,
                                           StatusCallback onStatus, CompleteCallback onComplete)
{
    auto reportStatus = [onStatus](juce::String text)
    {
        if (onStatus)
            juce::MessageManager::callAsync([onStatus, text] { onStatus(text); });
    };

    auto fail = [&](const juce::String& reason)
    {
        logLine("[DiscordConnector] " + reason);
        reportStatus(reason);

        if (voiceGateway != nullptr)
            voiceGateway->disconnect();
        if (gateway != nullptr)
            gateway->disconnect();

        if (onComplete)
            juce::MessageManager::callAsync([onComplete] { onComplete(false); });
    };

    reportStatus("Connecting to Discord gateway...");
    gateway = std::make_unique<GatewayClient>(botToken);
    gateway->connect();

    if (!gateway->waitForReady(10000))
        return fail("Timed out waiting for Discord gateway - check the bot token.");

    logLine("[DiscordConnector] Resetting any stale voice state first...");
    gateway->requestJoinVoiceChannel(guildId, "");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    reportStatus("Joining voice channel...");
    gateway->requestJoinVoiceChannel(guildId, channelId);

    GatewayClient::VoiceServerInfo serverInfo;
    if (!gateway->waitForVoiceServerInfo(serverInfo, 10000))
        return fail("Timed out waiting for voice server info - check the server/channel IDs.");

    voiceGateway = std::make_unique<VoiceGatewayClient>(serverInfo.endpoint, serverInfo.guildId, channelId,
                                                          gateway->getBotUserId(), serverInfo.sessionId,
                                                          serverInfo.voiceToken);
    voiceGateway->connect();

    VoiceGatewayClient::ReadyInfo voiceReady;
    if (!voiceGateway->waitForReady(voiceReady, 10000))
        return fail("Timed out waiting for the voice gateway.");

    if (!udp.bindSocket())
        return fail("Failed to bind local UDP socket.");

    juce::String externalIp;
    int externalPort = 0;
    if (!udp.performIpDiscovery(voiceReady.ip, voiceReady.port, voiceReady.ssrc, externalIp, externalPort))
        return fail("Voice IP discovery failed.");

    udp.setDestination(voiceReady.ip, voiceReady.port);
    voiceGateway->selectProtocol(externalIp, externalPort);

    VoiceGatewayClient::SessionInfo session;
    if (!voiceGateway->waitForSessionDescription(session, 10000))
        return fail("Timed out waiting for session description.");

    udp.setSecretKey(session.secretKey, voiceReady.ssrc);

    // From here the bot is genuinely sitting in the voice channel, so it
    // has to be disconnected cleanly on quit even if the handshake below
    // never completes.
    voiceSessionUp = true;

    reportStatus("Waiting for encryption handshake...");
    if (!voiceGateway->waitForDaveReady(15000))
    {
        // Not a failure. DAVE is an MLS *group* key exchange, and
        // Discord only forms the group once there's someone else in the
        // channel - a bot that joins an empty channel simply never gets
        // proposals or a welcome. Observed exactly that in a real log:
        // key package sent, then nothing at all.
        //
        // Hanging up here (which is what used to happen) is the worst
        // possible response, because starting the app before anyone has
        // joined is completely normal for a DM. Stay in the channel and
        // keep waiting instead; audio starts the moment it completes.
        logLine("[DiscordConnector] DAVE not ready yet - staying in the channel and waiting.");
        reportStatus("In the voice channel - waiting for someone to join before audio can start.");

        while (!shouldAbort.load())
            if (voiceGateway->waitForDaveReady(2000))
                break;

        if (shouldAbort.load())
        {
            logLine("[DiscordConnector] Aborted while waiting for DAVE.");
            return;
        }

        logLine("[DiscordConnector] DAVE became ready once the channel had another member.");
    }

    voiceGateway->sendSpeaking(voiceReady.ssrc, true);
    connected = true;

    logLine("[DiscordConnector] Connected and streaming to Discord.");
    reportStatus("Connected to Discord.");

    if (onComplete)
        juce::MessageManager::callAsync([onComplete] { onComplete(true); });
}

void DiscordConnector::disconnect()
{
    // Stop the connect thread first - it may be parked in the
    // wait-for-DAVE loop, and tearing the sockets out from under it
    // would race. The join costs at most one 2s poll interval.
    shouldAbort = true;
    if (connectThread != nullptr && connectThread->joinable())
        connectThread->join();
    connectThread.reset();

    // voiceSessionUp, not connected: the bot can be sitting in the
    // channel waiting for DAVE, and still needs to leave properly.
    if (!voiceSessionUp.exchange(false))
        return;

    gateway->requestJoinVoiceChannel(guildIdForDisconnect, "");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    voiceGateway->disconnect();
    gateway->disconnect();
    connected = false;
}
