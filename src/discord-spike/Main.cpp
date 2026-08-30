#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#endif

#include <juce_core/juce_core.h>
#include <ixwebsocket/IXNetSystem.h>
#include <opus.h>
#include <sodium.h>

#include "GatewayClient.h"
#include "VoiceGatewayClient.h"
#include "VoiceUdpSocket.h"
#include "Log.h"

namespace
{
    constexpr int kSampleRate = 48000;
    constexpr int kFrameSamples = 960; // 20ms at 48kHz
    constexpr int kToneSeconds = 3;

    // Generates a 440Hz sine test tone, Opus-encoded as a sequence of
    // 20ms mono frames, so the spike can prove audio actually arrives
    // in the voice channel without needing a real audio file yet.
    std::vector<std::vector<uint8_t>> generateTestToneOpusFrames()
    {
        int error = 0;
        auto* encoder = opus_encoder_create(kSampleRate, 1, OPUS_APPLICATION_AUDIO, &error);
        if (error != OPUS_OK || encoder == nullptr)
        {
            logLine("[Main] opus_encoder_create failed: " + juce::String(error));
            return {};
        }
        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000));

        std::vector<std::vector<uint8_t>> frames;
        const int totalSamples = kSampleRate * kToneSeconds;

        for (int start = 0; start < totalSamples; start += kFrameSamples)
        {
            int16_t pcm[kFrameSamples];
            for (int i = 0; i < kFrameSamples; ++i)
            {
                double t = (double) (start + i) / (double) kSampleRate;
                pcm[i] = (int16_t) (std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * t) * 12000.0);
            }

            uint8_t encoded[4000];
            int encodedLen = opus_encode(encoder, pcm, kFrameSamples, encoded, (opus_int32) sizeof(encoded));
            if (encodedLen > 0)
                frames.emplace_back(encoded, encoded + encodedLen);
        }

        opus_encoder_destroy(encoder);
        return frames;
    }
}

int main(int argc, char* argv[])
{
    juce::ignoreUnused(argc, argv);

#ifdef _WIN32
    // A crash mid-development shouldn't block on a modal dialog waiting
    // for someone to click through it - let abort()/assert failures just
    // print and exit so a stuck run doesn't need a person at the machine.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    if (sodium_init() < 0)
    {
        logLine("[Main] sodium_init failed");
        return 1;
    }
    ix::initNetSystem();

    auto botToken = juce::SystemStats::getEnvironmentVariable("DISCORD_BOT_TOKEN", "");
    auto guildId = juce::SystemStats::getEnvironmentVariable("DISCORD_GUILD_ID", "");
    auto channelId = juce::SystemStats::getEnvironmentVariable("DISCORD_CHANNEL_ID", "");

    if (botToken.isEmpty() || guildId.isEmpty() || channelId.isEmpty())
    {
        logLine("[Main] Set DISCORD_BOT_TOKEN, DISCORD_GUILD_ID, DISCORD_CHANNEL_ID env vars first.");
        return 1;
    }

    logLine("[Main] Connecting to main gateway...");
    GatewayClient gateway(botToken);
    gateway.connect();

    if (!gateway.waitForReady(10000))
    {
        logLine("[Main] Timed out waiting for READY.");
        return 1;
    }

    // A previous run that crashed/timed out mid-handshake never sent a
    // clean leave, which can leave the bot's server-side voice state
    // stale and cause the *next* Identify to be rejected with "Session
    // is no longer valid" even though everything about it looks correct.
    // Explicitly reset first - harmless no-op if there's nothing to clear.
    logLine("[Main] Resetting any stale voice state first...");
    gateway.requestJoinVoiceChannel(guildId, "");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    logLine("[Main] Requesting to join voice channel " + channelId);
    gateway.requestJoinVoiceChannel(guildId, channelId);

    GatewayClient::VoiceServerInfo serverInfo;
    if (!gateway.waitForVoiceServerInfo(serverInfo, 10000))
    {
        logLine("[Main] Timed out waiting for voice server info.");
        return 1;
    }

    logLine("[Main] Got voice server: " + serverInfo.endpoint);

    VoiceGatewayClient voiceGateway(serverInfo.endpoint, serverInfo.guildId, channelId,
                                     gateway.getBotUserId(), serverInfo.sessionId, serverInfo.voiceToken);
    voiceGateway.connect();

    VoiceGatewayClient::ReadyInfo voiceReady;
    if (!voiceGateway.waitForReady(voiceReady, 10000))
    {
        logLine("[Main] Timed out waiting for voice Ready.");
        return 1;
    }
    logLine("[Main] Voice ready: ssrc=" + juce::String((int) voiceReady.ssrc)
                              + " ip=" + voiceReady.ip + " port=" + juce::String(voiceReady.port));

    VoiceUdpSocket udp;
    if (!udp.bindSocket())
    {
        logLine("[Main] Failed to bind local UDP socket.");
        return 1;
    }

    juce::String externalIp;
    int externalPort = 0;
    if (!udp.performIpDiscovery(voiceReady.ip, voiceReady.port, voiceReady.ssrc, externalIp, externalPort))
    {
        logLine("[Main] IP discovery failed.");
        return 1;
    }
    logLine("[Main] Discovered external address: " + externalIp + ":" + juce::String(externalPort));

    udp.setDestination(voiceReady.ip, voiceReady.port);
    voiceGateway.selectProtocol(externalIp, externalPort);

    VoiceGatewayClient::SessionInfo session;
    if (!voiceGateway.waitForSessionDescription(session, 10000))
    {
        logLine("[Main] Timed out waiting for session description.");
        return 1;
    }
    logLine("[Main] Session established, mode=" + session.mode);

    udp.setSecretKey(session.secretKey, voiceReady.ssrc);

    // DAVE (E2EE) is mandatory as of March 2026 - the MLS handshake
    // (external sender -> key package -> proposals -> commit/welcome ->
    // execute transition) runs in the background inside voiceGateway's
    // message callbacks; this blocks until it's actually finished.
    logLine("[Main] Waiting for DAVE encryption handshake to complete...");
    if (!voiceGateway.waitForDaveReady(15000))
    {
        logLine("[Main] Timed out waiting for DAVE handshake - cannot send audio without it "
                 "(Discord will reject unencrypted media frames).");
        return 1;
    }
    logLine("[Main] DAVE ready - encryption established.");

    voiceGateway.sendSpeaking(voiceReady.ssrc, true);

    logLine("[Main] Encoding test tone...");
    auto frames = generateTestToneOpusFrames();
    logLine("[Main] Sending " + juce::String((int) frames.size()) + " Opus frames...");

    for (auto& frame : frames)
    {
        auto encrypted = voiceGateway.encryptOpusFrame(frame.data(), frame.size());
        if (encrypted.empty())
        {
            logLine("[Main] DAVE encryption failed for a frame - skipping it.");
            continue;
        }
        udp.sendOpusFrame(encrypted.data(), (int) encrypted.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    voiceGateway.sendSpeaking(voiceReady.ssrc, false);
    logLine("[Main] Done. Leaving voice channel and disconnecting.");

    gateway.requestJoinVoiceChannel(guildId, ""); // empty channel_id = leave
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    voiceGateway.disconnect();
    gateway.disconnect();
    ix::uninitNetSystem();

    return 0;
}
