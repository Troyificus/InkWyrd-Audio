#include "DaveSession.h"
#include "Log.h"

#include <dave/dave.h>

namespace
{
    void onMlsFailure(const char* source, const char* reason, void* /*userData*/)
    {
        logLine("[Dave] MLS failure in " + juce::String(source) + ": " + juce::String(reason));
    }

    // Every dave*Get* call that returns a byte buffer hands back memory
    // the library owns - copy it into a std::vector and free it via
    // daveFree() immediately, rather than threading raw ownership around.
    std::vector<uint8_t> takeAndFree(uint8_t* data, size_t len)
    {
        std::vector<uint8_t> result(data, data + len);
        daveFree(data);
        return result;
    }
}

DaveSession::DaveSession(juce::String selfUserIdIn) : selfUserId(std::move(selfUserIdIn))
{
    session = daveSessionCreate(nullptr, nullptr, &onMlsFailure, this);

    // Our OWN user id has to be in the recognized set. An MLS Welcome
    // lists every member of the group - us included - and libdave's
    // VerifyWelcomeState rejects the whole Welcome if any listed id
    // isn't recognized ("Welcome message lists unrecognized user ID").
    // clients_connect only ever lists the OTHER members, so without this
    // nothing ever adds us.
    //
    // This only bites on the Welcome path, which is why it went
    // unnoticed: joining a channel that already has people in it gets us
    // added by proposals + announce_commit_transition instead, and never
    // processes a Welcome at all. Joining an EMPTY channel and waiting
    // for someone else takes the Welcome path, and always failed.
    recognizedUserIds.addIfNotAlreadyThere(selfUserId);
}

DaveSession::~DaveSession()
{
    if (encryptor != nullptr)
        daveEncryptorDestroy(encryptor);
    if (ratchet != nullptr)
        daveKeyRatchetDestroy(ratchet);
    if (session != nullptr)
        daveSessionDestroy(session);
}

void DaveSession::init(uint16_t protocolVersion, uint64_t groupId)
{
    daveSessionInit(session, protocolVersion, groupId, selfUserId.toRawUTF8());
}

void DaveSession::reset()
{
    daveSessionReset(session);
    if (encryptor != nullptr)
    {
        daveEncryptorDestroy(encryptor);
        encryptor = nullptr;
    }
}

void DaveSession::setExternalSender(const uint8_t* data, size_t len)
{
    daveSessionSetExternalSender(session, data, len);
}

std::vector<uint8_t> DaveSession::getMarshalledKeyPackage()
{
    uint8_t* bytes = nullptr;
    size_t len = 0;
    daveSessionGetMarshalledKeyPackage(session, &bytes, &len);
    if (bytes == nullptr)
        return {};
    return takeAndFree(bytes, len);
}

void DaveSession::onClientsConnect(const juce::StringArray& userIds)
{
    for (auto& id : userIds)
        recognizedUserIds.addIfNotAlreadyThere(id);
}

void DaveSession::onClientDisconnect(const juce::String& userId)
{
    // Never drop ourselves from the set - see the constructor. Discord
    // shouldn't send our own id here, but losing it would break every
    // later Welcome in a way that's tedious to trace back to this.
    if (userId != selfUserId)
        recognizedUserIds.removeString(userId);
}

std::vector<const char*> DaveSession::recognizedUserIdPtrs() const
{
    std::vector<const char*> ptrs;
    ptrs.reserve((size_t) recognizedUserIds.size());
    for (auto& id : recognizedUserIds)
        ptrs.push_back(id.toRawUTF8());
    return ptrs;
}

std::vector<uint8_t> DaveSession::processProposals(const uint8_t* data, size_t len)
{
    auto ids = recognizedUserIdPtrs();
    uint8_t* outBytes = nullptr;
    size_t outLen = 0;
    daveSessionProcessProposals(session, data, len, ids.data(), ids.size(), &outBytes, &outLen);
    if (outBytes == nullptr || outLen == 0)
        return {};
    return takeAndFree(outBytes, outLen);
}

bool DaveSession::processCommit(const uint8_t* data, size_t len)
{
    auto* result = daveSessionProcessCommit(session, data, len);
    bool ok = result != nullptr && !daveCommitResultIsFailed(result) && !daveCommitResultIsIgnored(result);
    if (result != nullptr)
        daveCommitResultDestroy(result);
    return ok;
}

bool DaveSession::processWelcome(const uint8_t* data, size_t len)
{
    auto ids = recognizedUserIdPtrs();
    auto* result = daveSessionProcessWelcome(session, data, len, ids.data(), ids.size());
    bool ok = result != nullptr;
    if (result != nullptr)
        daveWelcomeResultDestroy(result);
    return ok;
}

bool DaveSession::createEncryptorForSelf(uint32_t ssrc)
{
    auto* newRatchet = daveSessionGetKeyRatchet(session, selfUserId.toRawUTF8());
    if (newRatchet == nullptr)
    {
        logLine("[Dave] No key ratchet available for self - cannot create encryptor");
        return false;
    }

    if (encryptor == nullptr)
        encryptor = daveEncryptorCreate();

    daveEncryptorSetKeyRatchet(encryptor, newRatchet);
    daveEncryptorAssignSsrcToCodec(encryptor, ssrc, DAVE_CODEC_OPUS);
    // Explicit rather than relying on the default: we only get here once
    // DAVE has actually been negotiated, so frames must be encrypted.
    daveEncryptorSetPassthroughMode(encryptor, false);

    // Only safe to free the ratchet the encryptor was PREVIOUSLY using
    // (if any) now that it's been replaced - not the one just set.
    if (ratchet != nullptr)
        daveKeyRatchetDestroy(ratchet);
    ratchet = newRatchet;
    return true;
}

std::vector<uint8_t> DaveSession::encryptOpusFrame(uint32_t ssrc, const uint8_t* opusData, size_t opusLen)
{
    if (encryptor == nullptr)
        return {};

    auto capacity = daveEncryptorGetMaxCiphertextByteSize(encryptor, DAVE_MEDIA_TYPE_AUDIO, opusLen);
    std::vector<uint8_t> out(capacity);
    size_t written = 0;

    auto rc = daveEncryptorEncrypt(encryptor, DAVE_MEDIA_TYPE_AUDIO, ssrc, opusData, opusLen,
                                    out.data(), out.size(), &written);
    if (rc != DAVE_ENCRYPTOR_RESULT_CODE_SUCCESS)
    {
        logLine("[Dave] Encrypt failed, result code " + juce::String((int) rc));
        return {};
    }

    out.resize(written);
    return out;
}
