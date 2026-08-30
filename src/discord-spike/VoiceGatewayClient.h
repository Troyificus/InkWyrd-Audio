#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <juce_core/juce_core.h>

#include "DaveSession.h"

namespace ix { class WebSocket; }

// Talks to a Discord Voice Gateway (the wss:// endpoint handed back in
// VOICE_SERVER_UPDATE). Handles Identify, Select Protocol, and Session
// Description (transport-level: RTP/AEAD secret key), plus the full
// DAVE handshake (MLS group key exchange, mandatory since March 2026)
// that establishes the *separate*, additional per-frame media
// encryption layer - see DaveSession.h for how the two layers stack.
class VoiceGatewayClient
{
public:
    struct ReadyInfo
    {
        uint32_t ssrc = 0;
        juce::String ip;
        int port = 0;
    };

    struct SessionInfo
    {
        std::array<uint8_t, 32> secretKey {};
        juce::String mode;
        uint16_t daveProtocolVersion = 0;
    };

    VoiceGatewayClient(juce::String endpoint,
                        juce::String guildId,
                        juce::String channelId,
                        juce::String userId,
                        juce::String sessionId,
                        juce::String voiceToken);
    ~VoiceGatewayClient();

    void connect();
    void disconnect();

    bool waitForReady(ReadyInfo& outInfo, int timeoutMs);

    // Called once local UDP IP discovery has completed.
    void selectProtocol(const juce::String& externalIp, int externalPort);

    bool waitForSessionDescription(SessionInfo& outInfo, int timeoutMs);

    void sendSpeaking(uint32_t ssrc, bool speaking);

    // True once the DAVE MLS handshake has completed and outgoing audio
    // can be encrypted - see waitForDaveReady().
    bool waitForDaveReady(int timeoutMs);

    // Returns the DAVE-encrypted frame to send in place of the raw Opus
    // payload. Must only be called after waitForDaveReady() succeeds.
    std::vector<uint8_t> encryptOpusFrame(const uint8_t* opusData, size_t opusLen);

private:
    void onMessage(const juce::String& text);
    void onBinaryMessage(const std::string& bytes);
    void sendJson(const juce::var& payload);
    void sendBinary(uint8_t opcode, const std::vector<uint8_t>& payload);
    void startHeartbeatThread(int intervalMs);
    void stopHeartbeatThread();

    void handleDaveExternalSenderPackage(const std::string& bytes);
    void handleDaveProposals(const std::string& bytes);
    void handleDaveAnnounceCommitTransition(const std::string& bytes);
    void handleDaveWelcome(const std::string& bytes);
    void executeTransition(uint16_t transitionId);
    void sendReadyForTransition(uint16_t transitionId);
    void sendKeyPackage();

    juce::String endpoint, guildId, channelId, userId, sessionId, voiceToken;
    std::unique_ptr<ix::WebSocket> socket;

    std::atomic<bool> heartbeatRunning { false };
    std::unique_ptr<std::thread> heartbeatThread;

    juce::CriticalSection readyLock;
    ReadyInfo readyInfo;
    std::atomic<bool> haveReady { false };

    juce::CriticalSection sessionLock;
    SessionInfo sessionInfo;
    std::atomic<bool> haveSession { false };

    std::unique_ptr<DaveSession> dave;
    std::atomic<bool> daveEncryptorReady { false };
    uint32_t ssrcForDave = 0;

    // dave_mls_external_sender_package (25) can arrive before or after
    // Session Description tells us the negotiated dave_protocol_version
    // needed to call DaveSession::init() - buffer it if it's early.
    bool daveInited = false;
    std::vector<uint8_t> pendingExternalSenderBytes;
};
