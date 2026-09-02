#include "VoiceGatewayClient.h"
#include "Log.h"

#include <ixwebsocket/IXWebSocket.h>
#include <dave/dave.h>

namespace
{
    // Voice Gateway opcodes (Discord docs: Voice Opcodes table) - these
    // are a DIFFERENT numbering from the main Gateway's opcodes above.
    constexpr int OP_IDENTIFY = 0;
    constexpr int OP_SELECT_PROTOCOL = 1;
    constexpr int OP_READY = 2;
    constexpr int OP_HEARTBEAT = 3;
    constexpr int OP_SESSION_DESCRIPTION = 4;
    constexpr int OP_SPEAKING = 5;
    constexpr int OP_HEARTBEAT_ACK = 6;
    constexpr int OP_HELLO = 8;
    constexpr int OP_CLIENTS_CONNECT = 11;
    constexpr int OP_CLIENT_DISCONNECT = 13;
    constexpr int OP_DAVE_PREPARE_TRANSITION = 21;
    constexpr int OP_DAVE_EXECUTE_TRANSITION = 22;
    constexpr int OP_DAVE_READY_FOR_TRANSITION = 23;
    constexpr int OP_DAVE_PREPARE_EPOCH = 24;
    constexpr int OP_DAVE_MLS_INVALID_COMMIT_WELCOME = 31;

    // Binary (non-JSON) opcodes - server->client ones all share a
    // 3-byte header: uint16 sequence_number, uint8 opcode, then payload.
    // See docs/dave-protocol-notes.md for the literal spec text this is
    // transcribed from (discord/dave-protocol, protocol.md).
    constexpr uint8_t BOP_MLS_EXTERNAL_SENDER_PACKAGE = 25;
    constexpr uint8_t BOP_MLS_KEY_PACKAGE = 26; // client->server, no seq prefix
    constexpr uint8_t BOP_MLS_PROPOSALS = 27;
    constexpr uint8_t BOP_MLS_COMMIT_WELCOME = 28; // client->server, no seq prefix
    constexpr uint8_t BOP_MLS_ANNOUNCE_COMMIT_TRANSITION = 29;
    constexpr uint8_t BOP_MLS_WELCOME = 30;

    // Discord requires this mode (aead_xchacha20_poly1305_rtpsize) to be
    // supported; aead_aes256_gcm_rtpsize is preferred when offered but
    // this spike always requests the mandatory one for simplicity.
    const juce::String kEncryptionMode = "aead_xchacha20_poly1305_rtpsize";

    uint16_t readBigEndianU16(const std::string& bytes, size_t offset)
    {
        return (uint16_t) (((uint8_t) bytes[offset] << 8) | (uint8_t) bytes[offset + 1]);
    }
}

VoiceGatewayClient::VoiceGatewayClient(juce::String endpointIn,
                                        juce::String guildIdIn,
                                        juce::String channelIdIn,
                                        juce::String userIdIn,
                                        juce::String sessionIdIn,
                                        juce::String voiceTokenIn)
    : endpoint(std::move(endpointIn)), guildId(std::move(guildIdIn)), channelId(std::move(channelIdIn)),
      userId(std::move(userIdIn)), sessionId(std::move(sessionIdIn)), voiceToken(std::move(voiceTokenIn))
{
    dave = std::make_unique<DaveSession>(userId);
}

VoiceGatewayClient::~VoiceGatewayClient()
{
    disconnect();
}

void VoiceGatewayClient::connect()
{
    // The endpoint from VOICE_SERVER_UPDATE (e.g. "region.discord.media:8443")
    // is used exactly as given, port included - confirmed against the
    // official @discordjs/voice source (Networking.ts: `wss://${endpoint}?v=4`,
    // endpoint passed through from VOICE_SERVER_UPDATE completely
    // unmodified). Stripping it to the default 443 port was the actual
    // root cause of every "Session is no longer valid" (4006) failure
    // this spike hit: port 443 accepts the handshake up through Hello/
    // Identify but isn't the specific backend that owns the session.
    auto url = "wss://" + endpoint + "/?v=8";

    socket = std::make_unique<ix::WebSocket>();
    socket->disableAutomaticReconnection();
    socket->setUrl(url.toStdString());

    socket->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg)
    {
        if (msg->type == ix::WebSocketMessageType::Message)
        {
            if (msg->binary)
                onBinaryMessage(msg->str);
            else
                onMessage(juce::String(msg->str));
        }
        else if (msg->type == ix::WebSocketMessageType::Error)
            logLine("[VoiceGateway] ws error: " + juce::String(msg->errorInfo.reason));
        else if (msg->type == ix::WebSocketMessageType::Close)
            logLine("[VoiceGateway] closed: code=" + juce::String(msg->closeInfo.code)
                     + " reason=" + juce::String(msg->closeInfo.reason)
                     + " remote=" + juce::String(msg->closeInfo.remote ? "true" : "false"));
    });

    socket->start();
}

void VoiceGatewayClient::disconnect()
{
    stopHeartbeatThread();
    if (socket != nullptr)
    {
        socket->stop();
        socket.reset();
    }
}

void VoiceGatewayClient::sendJson(const juce::var& payload)
{
    if (socket != nullptr)
        socket->send(juce::JSON::toString(payload, true).toStdString());
}

void VoiceGatewayClient::sendBinary(uint8_t opcode, const std::vector<uint8_t>& payload)
{
    if (socket == nullptr)
        return;
    std::string frame;
    frame.reserve(1 + payload.size());
    frame.push_back((char) opcode);
    frame.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    socket->sendBinary(frame);
}

void VoiceGatewayClient::startHeartbeatThread(int intervalMs)
{
    heartbeatRunning = true;
    heartbeatThread = std::make_unique<std::thread>([this, intervalMs]
    {
        while (heartbeatRunning.load())
        {
            auto* d = new juce::DynamicObject();
            d->setProperty("t", (int64_t) juce::Time::currentTimeMillis());

            auto* obj = new juce::DynamicObject();
            obj->setProperty("op", OP_HEARTBEAT);
            obj->setProperty("d", juce::var(d));
            sendJson(juce::var(obj));

            for (int waited = 0; waited < intervalMs && heartbeatRunning.load(); waited += 100)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void VoiceGatewayClient::stopHeartbeatThread()
{
    heartbeatRunning = false;
    if (heartbeatThread != nullptr && heartbeatThread->joinable())
        heartbeatThread->join();
    heartbeatThread.reset();
}

void VoiceGatewayClient::sendKeyPackage()
{
    auto kp = dave->getMarshalledKeyPackage();
    if (kp.empty())
    {
        logLine("[VoiceGateway] getMarshalledKeyPackage returned empty - not sending");
        return;
    }
    logLine("[VoiceGateway] sending dave_mls_key_package (" + juce::String((int) kp.size()) + " bytes)");
    sendBinary(BOP_MLS_KEY_PACKAGE, kp);
}

void VoiceGatewayClient::executeTransition(uint16_t transitionId)
{
    if (dave->createEncryptorForSelf(ssrcForDave))
    {
        daveEncryptorReady = true;
        logLine("[VoiceGateway] transition " + juce::String(transitionId)
                 + " executed - media encryptor ready");
    }
    else
    {
        logLine("[VoiceGateway] transition " + juce::String(transitionId)
                 + " executed but encryptor could not be created");
    }
}

void VoiceGatewayClient::sendReadyForTransition(uint16_t transitionId)
{
    auto* d = new juce::DynamicObject();
    d->setProperty("transition_id", (int) transitionId);
    auto* obj = new juce::DynamicObject();
    obj->setProperty("op", OP_DAVE_READY_FOR_TRANSITION);
    obj->setProperty("d", juce::var(d));
    logLine("[VoiceGateway] sending dave_protocol_ready_for_transition, transition_id=" + juce::String(transitionId));
    sendJson(juce::var(obj));
}

void VoiceGatewayClient::handleDaveExternalSenderPackage(const std::string& bytes)
{
    logLine("[VoiceGateway] dave_mls_external_sender_package received (" + juce::String((int) bytes.size()) + " bytes)");
    if (bytes.size() <= 3)
        return;

    std::vector<uint8_t> payload(bytes.begin() + 3, bytes.end());

    if (!daveInited)
    {
        // Session Description (which carries dave_protocol_version,
        // required before DaveSession::init()) hasn't arrived yet -
        // apply this once it does, instead of touching an uninited session.
        logLine("[VoiceGateway] buffering external sender package until DAVE session is inited");
        pendingExternalSenderBytes = std::move(payload);
        return;
    }

    dave->setExternalSender(payload.data(), payload.size());
    sendKeyPackage();
}

void VoiceGatewayClient::handleDaveProposals(const std::string& bytes)
{
    logLine("[VoiceGateway] dave_mls_proposals received (" + juce::String((int) bytes.size()) + " bytes)");
    if (bytes.size() <= 3)
        return;
    auto* data = reinterpret_cast<const uint8_t*>(bytes.data()) + 3;
    auto len = bytes.size() - 3;
    auto commitWelcome = dave->processProposals(data, len);
    if (!commitWelcome.empty())
    {
        logLine("[VoiceGateway] sending dave_mls_commit_welcome (" + juce::String((int) commitWelcome.size()) + " bytes)");
        sendBinary(BOP_MLS_COMMIT_WELCOME, commitWelcome);
    }
}

void VoiceGatewayClient::handleDaveAnnounceCommitTransition(const std::string& bytes)
{
    if (bytes.size() < 5)
    {
        logLine("[VoiceGateway] dave_mls_announce_commit_transition too short");
        return;
    }
    auto transitionId = readBigEndianU16(bytes, 3);
    logLine("[VoiceGateway] dave_mls_announce_commit_transition, transition_id=" + juce::String(transitionId));

    auto* data = reinterpret_cast<const uint8_t*>(bytes.data()) + 5;
    auto len = bytes.size() - 5;
    if (dave->processCommit(data, len))
    {
        sendReadyForTransition(transitionId);

        // transition_id 0 is (re)initialization: per the spec it "can be
        // executed immediately" and NO dave_protocol_execute_transition
        // (22) follows it. Waiting for one here is a deadlock - which is
        // exactly what the first working run hit.
        if (transitionId == 0)
            executeTransition(transitionId);
    }
    else
    {
        logLine("[VoiceGateway] commit processing failed - requesting recovery");
        auto* d = new juce::DynamicObject();
        d->setProperty("transition_id", (int) transitionId);
        auto* obj = new juce::DynamicObject();
        obj->setProperty("op", OP_DAVE_MLS_INVALID_COMMIT_WELCOME);
        obj->setProperty("d", juce::var(d));
        sendJson(juce::var(obj));
    }
}

void VoiceGatewayClient::handleDaveWelcome(const std::string& bytes)
{
    if (bytes.size() < 5)
    {
        logLine("[VoiceGateway] dave_mls_welcome too short");
        return;
    }
    auto transitionId = readBigEndianU16(bytes, 3);
    logLine("[VoiceGateway] dave_mls_welcome, transition_id=" + juce::String(transitionId));

    auto* data = reinterpret_cast<const uint8_t*>(bytes.data()) + 5;
    auto len = bytes.size() - 5;
    if (dave->processWelcome(data, len))
    {
        sendReadyForTransition(transitionId);
        if (transitionId == 0) // see handleDaveAnnounceCommitTransition
            executeTransition(transitionId);
    }
    else
    {
        logLine("[VoiceGateway] welcome processing failed");
    }
}

void VoiceGatewayClient::onBinaryMessage(const std::string& bytes)
{
    if (bytes.size() < 3)
    {
        logLine("[VoiceGateway] binary message too short: " + juce::String((int) bytes.size()) + " bytes");
        return;
    }

    auto opcode = (uint8_t) bytes[2];
    switch (opcode)
    {
        case BOP_MLS_EXTERNAL_SENDER_PACKAGE: handleDaveExternalSenderPackage(bytes); break;
        case BOP_MLS_PROPOSALS: handleDaveProposals(bytes); break;
        case BOP_MLS_ANNOUNCE_COMMIT_TRANSITION: handleDaveAnnounceCommitTransition(bytes); break;
        case BOP_MLS_WELCOME: handleDaveWelcome(bytes); break;
        default:
            logLine("[VoiceGateway] unhandled binary opcode: " + juce::String((int) opcode));
    }
}

void VoiceGatewayClient::onMessage(const juce::String& text)
{
    auto parsed = juce::JSON::parse(text);
    if (!parsed.isObject())
        return;

    auto op = (int) parsed.getProperty("op", -1);
    auto d = parsed.getProperty("d", juce::var());

    if (op == OP_HELLO)
    {
        logLine("[VoiceGateway] Hello received");
        auto interval = (int) d.getProperty("heartbeat_interval", 5000);

        auto* identifyD = new juce::DynamicObject();
        identifyD->setProperty("server_id", guildId);
        identifyD->setProperty("user_id", userId);
        identifyD->setProperty("session_id", sessionId);
        identifyD->setProperty("token", voiceToken);
        identifyD->setProperty("max_dave_protocol_version", (int) daveMaxSupportedProtocolVersion());

        auto* obj = new juce::DynamicObject();
        obj->setProperty("op", OP_IDENTIFY);
        obj->setProperty("d", juce::var(identifyD));
        logLine("[VoiceGateway] sending Identify (max_dave_protocol_version="
                 + juce::String((int) daveMaxSupportedProtocolVersion()) + ")");
        sendJson(juce::var(obj));

        // Heartbeats start only AFTER Identify is on the wire. The
        // heartbeat thread sends its first beat immediately (send, then
        // sleep), so starting it before this point raced the Identify -
        // and the VOICE gateway closes with 4003 "Not authenticated" if
        // any payload reaches it before Identify. Confirmed from a real
        // beta log: "sending Identify" followed straight by
        // "closed: code=4003 reason=Not authenticated".
        //
        // Intermittent by nature, and the GUI conversion made it far
        // likelier to lose the race: the connect sequence used to run on
        // a console app's idle main thread, and now runs on a background
        // thread while the message thread is busy.
        //
        // NOTE: GatewayClient (the MAIN gateway) deliberately still
        // starts its heartbeat before Identify - Discord's main gateway
        // accepts heartbeats while unauthenticated, unlike the voice one.
        startHeartbeatThread(interval);
        return;
    }

    if (op == OP_HEARTBEAT_ACK)
        return;

    if (op == OP_READY)
    {
        logLine("[VoiceGateway] Ready received");
        const juce::ScopedLock lock(readyLock);
        readyInfo.ssrc = (uint32_t) (int64_t) d.getProperty("ssrc", 0);
        readyInfo.ip = d.getProperty("ip", "").toString();
        readyInfo.port = (int) d.getProperty("port", 0);
        ssrcForDave = readyInfo.ssrc;
        haveReady = true;
        return;
    }

    if (op == OP_SESSION_DESCRIPTION)
    {
        logLine("[VoiceGateway] Session Description received, mode=" + d.getProperty("mode", "").toString());
        const juce::ScopedLock lock(sessionLock);
        sessionInfo.mode = d.getProperty("mode", "").toString();
        sessionInfo.daveProtocolVersion = (uint16_t) (int) d.getProperty("dave_protocol_version", 0);

        auto* keyArray = d.getProperty("secret_key", juce::var()).getArray();
        if (keyArray != nullptr)
        {
            for (int i = 0; i < 32 && i < keyArray->size(); ++i)
                sessionInfo.secretKey[(size_t) i] = (uint8_t) (int) (*keyArray)[i];
        }
        haveSession = true;

        logLine("[VoiceGateway] dave_protocol_version=" + juce::String(sessionInfo.daveProtocolVersion));
        if (sessionInfo.daveProtocolVersion == 0)
        {
            logLine("[VoiceGateway] WARNING: server negotiated DAVE protocol version 0 (no E2EE) - "
                     "voice may be rejected given DAVE is mandatory as of March 2026");
        }

        dave->init(sessionInfo.daveProtocolVersion, (uint64_t) channelId.getLargeIntValue());
        daveInited = true;

        if (!pendingExternalSenderBytes.empty())
        {
            dave->setExternalSender(pendingExternalSenderBytes.data(), pendingExternalSenderBytes.size());
            pendingExternalSenderBytes.clear();
            sendKeyPackage();
        }
        return;
    }

    if (op == OP_CLIENTS_CONNECT)
    {
        auto* idsVar = d.getProperty("user_ids", juce::var()).getArray();
        juce::StringArray ids;
        if (idsVar != nullptr)
            for (auto& v : *idsVar)
                ids.add(v.toString());
        logLine("[VoiceGateway] clients_connect: " + ids.joinIntoString(","));
        dave->onClientsConnect(ids);
        return;
    }

    if (op == OP_CLIENT_DISCONNECT)
    {
        auto userIdStr = d.getProperty("user_id", "").toString();
        logLine("[VoiceGateway] client_disconnect: " + userIdStr);
        dave->onClientDisconnect(userIdStr);
        return;
    }

    if (op == OP_DAVE_PREPARE_TRANSITION)
    {
        auto transitionId = (uint16_t) (int) d.getProperty("transition_id", 0);
        logLine("[VoiceGateway] dave_protocol_prepare_transition, transition_id=" + juce::String(transitionId));
        // transition_id 0 means (re)initialization and executes immediately -
        // there's no separate execute_transition coming for it.
        if (transitionId == 0)
            executeTransition(transitionId);
        return;
    }

    if (op == OP_DAVE_EXECUTE_TRANSITION)
    {
        auto transitionId = (uint16_t) (int) d.getProperty("transition_id", 0);
        logLine("[VoiceGateway] dave_protocol_execute_transition, transition_id=" + juce::String(transitionId));
        executeTransition(transitionId);
        return;
    }

    if (op == OP_DAVE_PREPARE_EPOCH)
    {
        auto epoch = (int) d.getProperty("epoch", 0);
        logLine("[VoiceGateway] dave_protocol_prepare_epoch, epoch=" + juce::String(epoch));
        if (epoch == 1)
        {
            // Group is being (re)created - reset local state and wait
            // for a fresh external sender package before re-keying.
            dave->reset();
            daveEncryptorReady = false;
        }
        return;
    }

    logLine("[VoiceGateway] other opcode: " + juce::String(op) + " raw: " + text);
}

bool VoiceGatewayClient::waitForReady(ReadyInfo& outInfo, int timeoutMs)
{
    int waited = 0;
    while (waited < timeoutMs)
    {
        if (haveReady.load())
        {
            const juce::ScopedLock lock(readyLock);
            outInfo = readyInfo;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }
    return false;
}

void VoiceGatewayClient::selectProtocol(const juce::String& externalIp, int externalPort)
{
    auto* data = new juce::DynamicObject();
    data->setProperty("address", externalIp);
    data->setProperty("port", externalPort);
    data->setProperty("mode", kEncryptionMode);

    auto* d = new juce::DynamicObject();
    d->setProperty("protocol", "udp");
    d->setProperty("data", juce::var(data));

    auto* obj = new juce::DynamicObject();
    obj->setProperty("op", OP_SELECT_PROTOCOL);
    obj->setProperty("d", juce::var(d));
    sendJson(juce::var(obj));
}

bool VoiceGatewayClient::waitForSessionDescription(SessionInfo& outInfo, int timeoutMs)
{
    int waited = 0;
    while (waited < timeoutMs)
    {
        if (haveSession.load())
        {
            const juce::ScopedLock lock(sessionLock);
            outInfo = sessionInfo;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }
    return false;
}

void VoiceGatewayClient::sendSpeaking(uint32_t ssrc, bool speaking)
{
    auto* d = new juce::DynamicObject();
    d->setProperty("speaking", speaking ? 1 : 0);
    d->setProperty("delay", 0);
    d->setProperty("ssrc", (int) ssrc);

    auto* obj = new juce::DynamicObject();
    obj->setProperty("op", OP_SPEAKING);
    obj->setProperty("d", juce::var(d));
    sendJson(juce::var(obj));
}

bool VoiceGatewayClient::waitForDaveReady(int timeoutMs)
{
    int waited = 0;
    while (waited < timeoutMs)
    {
        if (daveEncryptorReady.load())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }
    return false;
}

std::vector<uint8_t> VoiceGatewayClient::encryptOpusFrame(const uint8_t* opusData, size_t opusLen)
{
    return dave->encryptOpusFrame(ssrcForDave, opusData, opusLen);
}
