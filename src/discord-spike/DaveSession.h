#pragma once

#include <cstdint>
#include <vector>
#include <juce_core/juce_core.h>

typedef struct DAVESessionHandle_s* DAVESessionHandle;
typedef struct DAVEEncryptorHandle_s* DAVEEncryptorHandle;
typedef struct DAVEKeyRatchetHandle_s* DAVEKeyRatchetHandle;

// Wraps discord/libdave's C API (dave.h) for one voice connection's MLS
// group state. Discord requires DAVE (end-to-end media encryption) for
// every voice connection as of March 2026 - this isn't optional, and it
// sits as an extra encryption layer *underneath* the existing RTP/AEAD
// transport encryption in VoiceUdpSocket: DAVE encrypts the Opus payload
// itself before it ever reaches the RTP layer, so VoiceUdpSocket doesn't
// need to know DAVE exists at all.
//
// See docs/dave-protocol-notes.md for the wire protocol this drives.
class DaveSession
{
public:
    explicit DaveSession(juce::String selfUserId);
    ~DaveSession();

    void init(uint16_t protocolVersion, uint64_t groupId);

    // dave_protocol_prepare_epoch with epoch==1 means "start over" - the
    // client must generate and send a fresh key package after this.
    void reset();

    void setExternalSender(const uint8_t* data, size_t len);

    // Bytes to send verbatim as the payload of a dave_mls_key_package
    // (opcode 26) message (after the 1-byte opcode prefix).
    std::vector<uint8_t> getMarshalledKeyPackage();

    // Roster tracking: proposals/welcomes must be rejected for any user
    // ID libdave doesn't recognize as an active participant, per the
    // protocol's own anti-injection requirement.
    void onClientsConnect(const juce::StringArray& userIds);
    void onClientDisconnect(const juce::String& userId);

    // Returns commit+welcome bytes to send as dave_mls_commit_welcome
    // (opcode 28), or empty if nothing needs to be sent back.
    std::vector<uint8_t> processProposals(const uint8_t* data, size_t len);

    // Returns false if the commit/welcome was invalid and recovery
    // (dave_mls_invalid_commit_welcome, opcode 31) should be requested.
    bool processCommit(const uint8_t* data, size_t len);
    bool processWelcome(const uint8_t* data, size_t len);

    // Call once the server has executed the transition (opcode 22) -
    // sets up encryption using this session's own current epoch key.
    bool createEncryptorForSelf(uint32_t ssrc);

    // Returns the DAVE-encrypted frame to send in place of the raw Opus
    // payload, or empty on failure (caller should fall back/skip).
    std::vector<uint8_t> encryptOpusFrame(uint32_t ssrc, const uint8_t* opusData, size_t opusLen);

    bool hasEncryptor() const { return encryptor != nullptr; }

private:
    std::vector<const char*> recognizedUserIdPtrs() const;

    juce::String selfUserId;
    juce::StringArray recognizedUserIds;

    DAVESessionHandle session = nullptr;
    DAVEEncryptorHandle encryptor = nullptr;
    // daveEncryptorSetKeyRatchet() does NOT take ownership (per dave.h) -
    // the caller must keep the ratchet alive for as long as the
    // encryptor uses it, so this is held for the encryptor's lifetime
    // rather than destroyed right after being set.
    DAVEKeyRatchetHandle ratchet = nullptr;
};
