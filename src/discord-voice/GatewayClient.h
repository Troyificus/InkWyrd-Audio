#pragma once

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

    // Blocks (short polling loop) until READY has been received.
    bool waitForReady(int timeoutMs);

    juce::String getBotUserId() const { return botUserId; }

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

    std::atomic<long long> lastSequence { -1 };
    std::atomic<bool> heartbeatRunning { false };
    std::unique_ptr<std::thread> heartbeatThread;

    std::atomic<bool> ready { false };
    juce::String botUserId;

    juce::CriticalSection voiceInfoLock;
    juce::String pendingSessionId;
    juce::String pendingVoiceToken;
    juce::String pendingEndpoint;
    juce::String pendingGuildId;
    std::atomic<bool> haveSessionId { false };
    std::atomic<bool> haveServerUpdate { false };
};
