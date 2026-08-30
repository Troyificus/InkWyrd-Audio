#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <juce_core/juce_core.h>

namespace juce { class DatagramSocket; }

// UDP leg of the voice connection: IP discovery (finding out what
// address/port Discord sees us as, behind NAT) and sending encrypted
// Opus-in-RTP packets once the voice gateway has handed us a secret key.
class VoiceUdpSocket
{
public:
    VoiceUdpSocket();
    ~VoiceUdpSocket();

    bool bindSocket();

    // Discord's IP Discovery packet (see Voice docs): a 74-byte UDP
    // round trip to learn our own external ip/port for Select Protocol.
    bool performIpDiscovery(const juce::String& serverIp, int serverPort,
                             uint32_t ssrc, juce::String& outExternalIp, int& outExternalPort);

    void setDestination(const juce::String& serverIp, int serverPort);
    void setSecretKey(const std::array<uint8_t, 32>& key, uint32_t ssrcIn);

    // Encodes+encrypts one 20ms Opus frame as an RTP packet and sends it.
    // aead_xchacha20_poly1305_rtpsize: AAD = 12-byte RTP header, nonce =
    // a 32-bit big-endian counter (zero-padded to 24 bytes) appended
    // in the clear as the last 4 bytes of the packet.
    bool sendOpusFrame(const uint8_t* opusData, int opusLen);

private:
    std::unique_ptr<juce::DatagramSocket> socket;
    juce::String destIp;
    int destPort = 0;

    std::array<uint8_t, 32> secretKey {};
    uint32_t ssrc = 0;
    uint16_t rtpSequence = 0;
    uint32_t rtpTimestamp = 0;
    uint32_t nonceCounter = 0;
};
