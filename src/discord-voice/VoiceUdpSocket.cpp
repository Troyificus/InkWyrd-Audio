#include "VoiceUdpSocket.h"

#include <sodium.h>
#include <vector>

VoiceUdpSocket::VoiceUdpSocket() = default;
VoiceUdpSocket::~VoiceUdpSocket() = default;

bool VoiceUdpSocket::bindSocket()
{
    socket = std::make_unique<juce::DatagramSocket>();
    return socket->bindToPort(0);
}

bool VoiceUdpSocket::performIpDiscovery(const juce::String& serverIp, int serverPort,
                                         uint32_t ssrcIn, juce::String& outExternalIp, int& outExternalPort)
{
    if (socket == nullptr)
        return false;

    uint8_t packet[74] = { 0 };
    // Type = 0x1 (request), big-endian.
    packet[0] = 0x00; packet[1] = 0x01;
    // Length = 70 (bytes after this field: ssrc + address + port).
    packet[2] = 0x00; packet[3] = 70;
    packet[4] = (uint8_t) ((ssrcIn >> 24) & 0xFF);
    packet[5] = (uint8_t) ((ssrcIn >> 16) & 0xFF);
    packet[6] = (uint8_t) ((ssrcIn >> 8) & 0xFF);
    packet[7] = (uint8_t) (ssrcIn & 0xFF);
    // bytes 8..71 (address) and 72..73 (port) stay zero in the request.

    auto written = socket->write(serverIp, serverPort, packet, (int) sizeof(packet));
    if (written != (int) sizeof(packet))
        return false;

    if (socket->waitUntilReady(true, 5000) != 1)
        return false;

    uint8_t response[74] = { 0 };
    auto bytesRead = socket->read(response, (int) sizeof(response), true);
    if (bytesRead < (int) sizeof(response))
        return false;

    outExternalIp = juce::String(juce::CharPointer_UTF8(reinterpret_cast<const char*>(response + 8)));
    outExternalPort = (int) ((uint16_t) (response[72] << 8) | (uint16_t) response[73]);

    ssrc = ssrcIn;
    return true;
}

void VoiceUdpSocket::setDestination(const juce::String& serverIp, int serverPort)
{
    destIp = serverIp;
    destPort = serverPort;
}

void VoiceUdpSocket::setSecretKey(const std::array<uint8_t, 32>& key, uint32_t ssrcIn)
{
    secretKey = key;
    ssrc = ssrcIn;
}

bool VoiceUdpSocket::sendOpusFrame(const uint8_t* opusData, int opusLen)
{
    if (socket == nullptr || destPort == 0)
        return false;

    uint8_t header[12];
    header[0] = 0x80; // version 2, no padding/extension/CSRC
    header[1] = 0x78; // marker 0, payload type 120 (dynamic, Opus by convention)
    header[2] = (uint8_t) ((rtpSequence >> 8) & 0xFF);
    header[3] = (uint8_t) (rtpSequence & 0xFF);
    header[4] = (uint8_t) ((rtpTimestamp >> 24) & 0xFF);
    header[5] = (uint8_t) ((rtpTimestamp >> 16) & 0xFF);
    header[6] = (uint8_t) ((rtpTimestamp >> 8) & 0xFF);
    header[7] = (uint8_t) (rtpTimestamp & 0xFF);
    header[8] = (uint8_t) ((ssrc >> 24) & 0xFF);
    header[9] = (uint8_t) ((ssrc >> 16) & 0xFF);
    header[10] = (uint8_t) ((ssrc >> 8) & 0xFF);
    header[11] = (uint8_t) (ssrc & 0xFF);

    uint8_t nonce[24] = { 0 };
    nonce[0] = (uint8_t) ((nonceCounter >> 24) & 0xFF);
    nonce[1] = (uint8_t) ((nonceCounter >> 16) & 0xFF);
    nonce[2] = (uint8_t) ((nonceCounter >> 8) & 0xFF);
    nonce[3] = (uint8_t) (nonceCounter & 0xFF);

    std::vector<uint8_t> cipherText((size_t) opusLen + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long cipherLen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        cipherText.data(), &cipherLen,
        opusData, (unsigned long long) opusLen,
        header, sizeof(header),
        nullptr,
        nonce,
        secretKey.data());

    std::vector<uint8_t> packet;
    packet.reserve(sizeof(header) + cipherLen + 4);
    packet.insert(packet.end(), header, header + sizeof(header));
    packet.insert(packet.end(), cipherText.begin(), cipherText.begin() + (long) cipherLen);
    packet.push_back(nonce[0]);
    packet.push_back(nonce[1]);
    packet.push_back(nonce[2]);
    packet.push_back(nonce[3]);

    auto written = socket->write(destIp, destPort, packet.data(), (int) packet.size());

    ++rtpSequence;
    rtpTimestamp += 960; // 960 samples = 20ms at 48kHz
    ++nonceCounter;

    return written == (int) packet.size();
}
