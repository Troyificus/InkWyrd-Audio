#include "DiscordConnector.h"

#include "GatewayCloseCodes.h"
#include "GuildChannels.h"
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
    {
        // Prefer Discord's own reason over "it didn't answer". A
        // rejected token closes the connection almost immediately, and
        // calling that a timeout sent at least one person looking at
        // their network and their channel ID instead of the token.
        auto explanation = inkwyrd::describeGatewayCloseCode(gateway->getCloseCode());
        if (explanation.isNotEmpty())
            return fail(explanation);

        if (gateway->isClosed())
            return fail("Discord closed the connection during sign-in (code "
                         + juce::String(gateway->getCloseCode())
                         + "). The log has the details - Settings, Open log folder.");

        return fail("Timed out waiting for Discord gateway - check the bot token, "
                     "and that you're online.");
    }

    logLine("[DiscordConnector] Resetting any stale voice state first...");
    gateway->requestJoinVoiceChannel(guildId, "");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    reportStatus("Joining voice channel...");
    gateway->requestJoinVoiceChannel(guildId, channelId);

    GatewayClient::VoiceServerInfo serverInfo;
    if (!gateway->waitForVoiceServerInfo(serverInfo, 10000))
    {
        // Discord answers a voice join it won't honour with silence - no
        // error, no close. "Check the server/channel IDs" named two
        // things and helped with neither, so look at the channel list
        // GUILD_CREATE already sent and say which one is actually wrong.
        if (gateway->hasChannelList())
        {
            auto channel = gateway->findChannel(channelId);
            const bool found = channel.id.isNotEmpty();

            logLine("[DiscordConnector] voice join went unanswered; channel "
                     + juce::String(found ? "found, type " + juce::String(channel.type)
                                           + ", name " + channel.name
                                         : "NOT in this guild's channel list"));

            auto problem = inkwyrd::describeVoiceChannelProblem(found, channel.type, channel.name);
            return fail(problem.isNotEmpty() ? problem
                                             : inkwyrd::describeSilentVoiceJoinFailure(channel.name));
        }

        return fail("Discord never answered the request to join that voice channel, and never sent "
                     "the server's channel list either - so this is more likely a connection "
                     "problem than a wrong ID. The log has what arrived: Settings, Open log folder.");
    }

    // From here on, the voice side is SUPERVISED rather than set up once.
    //
    // Discord ends a call whose channel has emptied (close 4022), and
    // moves bots between voice servers of its own accord - both drop the
    // voice websocket, and both are routine rather than errors. Inkwyrd
    // used to set the voice session up a single time: when it went away
    // the app stayed parked in its wait-for-DAVE loop on a dead socket,
    // with Discord offering fresh voice servers that nothing was
    // listening for, and the music playing to nobody. Joining the channel
    // before starting Inkwyrd was the only way through.
    int consecutiveFailures = 0;
    bool firstAttempt = true;

    while (!shouldAbort.load())
    {
        // The first attempt reuses the details the probe above already
        // obtained. Asking again would be a second join for a bot that is
        // already where it was told to go, and Discord has no reason to
        // answer an unchanged voice state with fresh server details.
        const auto* initialInfo = firstAttempt ? &serverInfo : nullptr;
        firstAttempt = false;

        if (!openVoiceSession(channelId, guildId, reportStatus, initialInfo))
        {
            if (shouldAbort.load())
                break;

            // Back off, but never give up: the usual reason to be here is
            // that nobody has joined the channel yet, which fixes itself
            // the moment somebody does.
            ++consecutiveFailures;
            const int waitMs = juce::jmin(30000, 2000 * consecutiveFailures);
            logLine("[DiscordConnector] voice session didn't come up; retrying in "
                     + juce::String(waitMs / 1000) + "s");

            for (int waited = 0; waited < waitMs && !shouldAbort.load(); waited += 250)
                std::this_thread::sleep_for(std::chrono::milliseconds(250));

            continue;
        }

        consecutiveFailures = 0;
        connected = true;
        logLine("[DiscordConnector] Connected and streaming to Discord.");
        reportStatus("Connected - streaming to Discord.");

        if (onComplete)
        {
            auto report = onComplete;
            juce::MessageManager::callAsync([report] { report(true); });
            onComplete = {}; // first success only; later ones are reconnects
        }

        // Sit here for the life of the connection.
        while (!shouldAbort.load() && !voiceGateway->isClosed())
            std::this_thread::sleep_for(std::chrono::milliseconds(250));

        if (shouldAbort.load())
            break;

        connected = false;
        logLine("[DiscordConnector] voice connection closed (code "
                 + juce::String(voiceGateway->getCloseCode()) + ") - rebuilding it.");
        reportStatus("Reconnecting to the voice channel...");
    }
}

// One full voice session: rejoin, handshake, encryption, DAVE. Everything
// here is torn down and done again on a reconnect, because every piece of
// it is tied to the voice server Discord gave us for that attempt - the
// SSRC, the UDP destination and the secret key are all invalid against a
// different one.
bool DiscordConnector::openVoiceSession(const juce::String& channelId, const juce::String& guildId,
                                         const std::function<void(juce::String)>& reportStatus,
                                         const GatewayClient::VoiceServerInfo* existingInfo)
{
    if (voiceGateway != nullptr)
    {
        voiceGateway->disconnect();
        voiceGateway.reset();
    }

    GatewayClient::VoiceServerInfo serverInfo;

    if (existingInfo != nullptr)
    {
        serverInfo = *existingInfo;
    }
    else
    {
        // Leave, then rejoin. Resending the same voice state for a bot
        // Discord still thinks is in the channel is not reliably answered
        // with fresh server details; leaving first makes the rejoin a
        // real change. This mirrors the "reset any stale voice state"
        // step the initial connect already does, which is known to work.
        gateway->forgetVoiceServerInfo();
        gateway->requestJoinVoiceChannel(guildId, "");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        gateway->requestJoinVoiceChannel(guildId, channelId);

        // Without the forget above, this returns instantly with the
        // details of the connection that just died, and the new session
        // gets built on a stale token.
        if (!gateway->waitForVoiceServerInfo(serverInfo, 10000))
        {
            logLine("[DiscordConnector] no voice server details came back for this attempt.");
            return false;
        }
    }

    voiceGateway = std::make_unique<VoiceGatewayClient>(serverInfo.endpoint, serverInfo.guildId, channelId,
                                                          gateway->getBotUserId(), serverInfo.sessionId,
                                                          serverInfo.voiceToken);
    voiceGateway->connect();

    VoiceGatewayClient::ReadyInfo voiceReady;
    if (!voiceGateway->waitForReady(voiceReady, 10000))
    {
        logLine("[DiscordConnector] timed out waiting for the voice gateway.");
        return false;
    }

    if (!udp.bindSocket())
    {
        logLine("[DiscordConnector] failed to bind local UDP socket.");
        return false;
    }

    juce::String externalIp;
    int externalPort = 0;
    if (!udp.performIpDiscovery(voiceReady.ip, voiceReady.port, voiceReady.ssrc, externalIp, externalPort))
    {
        logLine("[DiscordConnector] voice IP discovery failed.");
        return false;
    }

    udp.setDestination(voiceReady.ip, voiceReady.port);
    voiceGateway->selectProtocol(externalIp, externalPort);

    VoiceGatewayClient::SessionInfo session;
    if (!voiceGateway->waitForSessionDescription(session, 10000))
    {
        logLine("[DiscordConnector] timed out waiting for session description.");
        return false;
    }

    udp.setSecretKey(session.secretKey, voiceReady.ssrc);

    // From here the bot is genuinely sitting in the voice channel, so it
    // has to be disconnected cleanly on quit even if DAVE never finishes.
    voiceSessionUp = true;

    reportStatus("Waiting for encryption handshake...");
    if (!voiceGateway->waitForDaveReady(15000))
    {
        // Not a failure. DAVE is an MLS *group* key exchange, and Discord
        // only forms the group once there's someone else in the channel -
        // a bot that joins an empty channel simply never gets proposals
        // or a welcome.
        //
        // Hanging up here would be the worst response, because starting
        // the app before anyone has joined is completely normal for a DM.
        // Stay in the channel and keep waiting; audio starts the moment
        // it completes.
        logLine("[DiscordConnector] DAVE not ready yet - staying in the channel and waiting.");
        // Careful with this wording: playback has already started by now,
        // it just isn't being transmitted. Saying "audio can't start"
        // reads as a contradiction when the Now Playing line is visibly
        // advancing (real beta feedback).
        reportStatus("In the voice channel - nobody else here yet, so nothing is being sent to Discord.");

        // Watching the socket as well as the handshake is the whole point
        // of this release: waiting on DAVE forever is exactly when
        // Discord terminates an empty call underneath us.
        while (!shouldAbort.load())
        {
            if (voiceGateway->waitForDaveReady(2000))
                break;

            if (voiceGateway->isClosed())
            {
                logLine("[DiscordConnector] Discord ended the call while waiting for the handshake (code "
                         + juce::String(voiceGateway->getCloseCode()) + ").");
                return false;
            }
        }

        if (shouldAbort.load())
        {
            logLine("[DiscordConnector] Aborted while waiting for DAVE.");
            return false;
        }

        logLine("[DiscordConnector] DAVE became ready once the channel had another member.");
    }

    voiceGateway->sendSpeaking(voiceReady.ssrc, true);
    return true;
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
