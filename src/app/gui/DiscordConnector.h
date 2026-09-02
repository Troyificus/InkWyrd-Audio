#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <juce_core/juce_core.h>

#include "GatewayClient.h"
#include "VoiceGatewayClient.h"
#include "VoiceUdpSocket.h"

// The exact join/handshake sequence that used to live directly in
// Main.cpp's DiscordConnection struct + attemptDiscordConnection()/
// connectToDiscord() free functions, unchanged step-for-step - just
// moved onto a background thread so a GUI "Save & Launch" click doesn't
// freeze the window. Every readiness check on GatewayClient/
// VoiceGatewayClient (waitForReady, waitForVoiceServerInfo,
// waitForSessionDescription, waitForDaveReady) is a blocking poll with
// no async variant, so this MUST run off the message thread.
//
// connectAsync() spins up that background thread; both callbacks are
// marshaled onto the message thread via juce::MessageManager::callAsync
// before they're invoked, so callers (the GUI) never see the background
// thread at all.
class DiscordConnector
{
public:
    using StatusCallback = std::function<void(juce::String)>;
    using CompleteCallback = std::function<void(bool success)>;

    DiscordConnector() = default;
    ~DiscordConnector();

    // Only meant to be called once per connected session in this app -
    // there's no cancel/reconnect-while-connecting support, matching
    // how the console app only ever connected once at startup.
    void connectAsync(const juce::String& botToken, const juce::String& guildId, const juce::String& channelId,
                       StatusCallback onStatus, CompleteCallback onComplete);

    // Message-thread only. Mirrors Main.cpp's disconnectFromDiscord()
    // exactly: reset the voice state, brief pause, then tear down both
    // gateway connections.
    void disconnect();

    bool isConnected() const { return connected.load(); }
    VoiceGatewayClient* getVoiceGateway() const { return voiceGateway.get(); }
    VoiceUdpSocket& getUdpSocket() { return udp; }

private:
    void runConnectSequence(juce::String botToken, juce::String guildId, juce::String channelId,
                             StatusCallback onStatus, CompleteCallback onComplete);

    std::unique_ptr<GatewayClient> gateway;
    std::unique_ptr<VoiceGatewayClient> voiceGateway;
    VoiceUdpSocket udp;

    // connected      = fully ready, DAVE done, audio can be sent.
    // voiceSessionUp = bot is actually sitting in the voice channel.
    // These differ while waiting for the DAVE handshake to become
    // possible (see runConnectSequence) - during that window the bot is
    // in the channel and must still be cleanly disconnected on quit,
    // even though no audio is flowing yet.
    std::atomic<bool> connected { false };
    std::atomic<bool> voiceSessionUp { false };
    std::atomic<bool> shouldAbort { false };
    juce::String guildIdForDisconnect;

    std::unique_ptr<std::thread> connectThread;
};
