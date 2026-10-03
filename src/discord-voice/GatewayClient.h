#pragma once

#include "GuildChannels.h"
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <juce_core/juce_core.h>

namespace ix { class WebSocket; }

// Talks to Discord's main bot Gateway (wss://gateway.discord.gg).
// Only handles what the voice-join spike needs: Identify, heartbeats,
// and correlating the two dispatch events Discord sends after we ask
// to join a voice channel (VOICE_STATE_UPDATE + VOICE_SERVER_UPDATE).
class GatewayClient
{
public:
    struct VoiceServerInfo
    {
        juce::String sessionId;
        juce::String voiceToken;
        juce::String endpoint;
        juce::String guildId;
    };

    explicit GatewayClient(juce::String botToken);
    ~GatewayClient();

    void connect();
    void disconnect();

    // Blocks (short polling loop) until READY has been received, or
    // until Discord closes the connection - a rejected token is answered
    // in well under a second, and waiting out the full timeout before
    // saying so just makes the app look broken rather than misconfigured.
    bool waitForReady(int timeoutMs);

    // Why Discord hung up, when it did. 0 means it hasn't. See
    // GatewayCloseCodes.h for what the numbers mean; 4004 is a bad token.
    int getCloseCode() const { return closeCode.load(); }
    bool isClosed() const { return closeCode.load() != 0; }

    juce::String getBotUserId() const { return botUserId; }

    // The guild's channels, as GUILD_CREATE listed them. Populated before
    // any voice join is attempted, which is what lets a silent join
    // failure be explained rather than reported as a timeout. Empty if
    // GUILD_CREATE never arrived.
    inkwyrd::GuildChannel findChannel(const juce::String& channelId) const;
    bool hasChannelList() const;

    void requestJoinVoiceChannel(const juce::String& guildId, const juce::String& channelId);

    // Blocks until both VOICE_STATE_UPDATE and VOICE_SERVER_UPDATE have
    // arrived for our own bot user, or timeoutMs elapses.
    bool waitForVoiceServerInfo(VoiceServerInfo& outInfo, int timeoutMs);

private:
    void onMessage(const juce::String& text);
    void sendJson(const juce::var& payload);
    void startHeartbeatThread(int intervalMs);
    void stopHeartbeatThread();

    juce::String botToken;
    std::unique_ptr<ix::WebSocket> socket;

    // Written on the websocket thread when GUILD_CREATE arrives, read
    // from the connector's thread after a join times out.
    juce::CriticalSection channelsLock;
    juce::Array<inkwyrd::GuildChannel> guildChannels;

    std::atomic<long long> lastSequence { -1 };
    std::atomic<bool> heartbeatRunning { false };
    std::unique_ptr<std::thread> heartbeatThread;

    std::atomic<bool> ready { false };

    // Set from the websocket's own thread when Discord closes the
    // connection, read by waitForReady on the caller's thread.
    std::atomic<int> closeCode { 0 };
    juce::String botUserId;

    juce::CriticalSection voiceInfoLock;
    juce::String pendingSessionId;
    juce::String pendingVoiceToken;
    juce::String pendingEndpoint;
    juce::String pendingGuildId;
    std::atomic<bool> haveSessionId { false };
    std::atomic<bool> haveServerUpdate { false };
};
