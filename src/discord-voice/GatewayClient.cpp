#include "GatewayClient.h"
#include "Log.h"

#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXNetSystem.h>

namespace
{
    // Main Gateway opcodes (Discord docs: Gateway Opcodes table).
    constexpr int OP_DISPATCH = 0;
    constexpr int OP_HEARTBEAT = 1;
    constexpr int OP_IDENTIFY = 2;
    constexpr int OP_VOICE_STATE_UPDATE = 4;
    constexpr int OP_HELLO = 10;
    constexpr int OP_HEARTBEAT_ACK = 11;

    // GUILDS | GUILD_VOICE_STATES
    constexpr int64_t INTENTS = (1 << 0) | (1 << 7);
}

GatewayClient::GatewayClient(juce::String tokenIn) : botToken(std::move(tokenIn))
{
}

GatewayClient::~GatewayClient()
{
    disconnect();
}

void GatewayClient::connect()
{
    socket = std::make_unique<ix::WebSocket>();
    // Discord's Resume protocol needs its own opcode sequence (op 6) that
    // this spike doesn't implement - ixwebsocket's raw auto-reconnect
    // would otherwise just replay a now-stale Identify on every drop,
    // masking real failures behind a silent retry loop.
    socket->disableAutomaticReconnection();
    socket->setUrl("wss://gateway.discord.gg/?v=10&encoding=json");

    socket->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg)
    {
        if (msg->type == ix::WebSocketMessageType::Message)
            onMessage(juce::String(msg->str));
        else if (msg->type == ix::WebSocketMessageType::Error)
            logLine("[Gateway] ws error: " + juce::String(msg->errorInfo.reason));
        else if (msg->type == ix::WebSocketMessageType::Close)
            logLine("[Gateway] closed: " + juce::String(msg->closeInfo.reason));
    });

    socket->start();
}

void GatewayClient::disconnect()
{
    stopHeartbeatThread();
    if (socket != nullptr)
    {
        socket->stop();
        socket.reset();
    }
}

void GatewayClient::sendJson(const juce::var& payload)
{
    if (socket != nullptr)
        socket->send(juce::JSON::toString(payload, true).toStdString());
}

void GatewayClient::startHeartbeatThread(int intervalMs)
{
    heartbeatRunning = true;
    heartbeatThread = std::make_unique<std::thread>([this, intervalMs]
    {
        while (heartbeatRunning.load())
        {
            auto seq = lastSequence.load();
            auto* obj = new juce::DynamicObject();
            obj->setProperty("op", OP_HEARTBEAT);
            obj->setProperty("d", seq < 0 ? juce::var() : juce::var(seq));
            sendJson(juce::var(obj));

            for (int waited = 0; waited < intervalMs && heartbeatRunning.load(); waited += 100)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void GatewayClient::stopHeartbeatThread()
{
    heartbeatRunning = false;
    if (heartbeatThread != nullptr && heartbeatThread->joinable())
        heartbeatThread->join();
    heartbeatThread.reset();
}

void GatewayClient::onMessage(const juce::String& text)
{
    auto parsed = juce::JSON::parse(text);
    if (!parsed.isObject())
        return;

    auto op = (int) parsed.getProperty("op", -1);

    if (auto* seqVar = parsed.getDynamicObject()->getProperties().getVarPointer("s"))
        if (!seqVar->isVoid())
            lastSequence = (long long) *seqVar;

    if (op == OP_HELLO)
    {
        auto interval = (int) parsed.getProperty("d", juce::var()).getProperty("heartbeat_interval", 41250);
        startHeartbeatThread(interval);

        auto* obj = new juce::DynamicObject();
        obj->setProperty("op", OP_IDENTIFY);

        auto* props = new juce::DynamicObject();
        props->setProperty("os", "windows");
        props->setProperty("browser", "inkwyrd-audio");
        props->setProperty("device", "inkwyrd-audio");

        auto* d = new juce::DynamicObject();
        d->setProperty("token", botToken);
        d->setProperty("intents", (int) INTENTS);
        d->setProperty("properties", juce::var(props));

        obj->setProperty("d", juce::var(d));
        sendJson(juce::var(obj));
        return;
    }

    if (op == OP_HEARTBEAT_ACK)
        return;

    if (op != OP_DISPATCH)
    {
        logLine("[Gateway] non-dispatch opcode: " + juce::String(op) + " raw: " + text);
        return;
    }

    auto type = parsed.getProperty("t", juce::var()).toString();
    auto d = parsed.getProperty("d", juce::var());

    if (type == "READY")
    {
        botUserId = d.getProperty("user", juce::var()).getProperty("id", "").toString();
        ready = true;
        logLine("[Gateway] READY, bot user id " + botUserId);

        // READY's guild list confirms which guilds Discord thinks this
        // bot is actually a member of - if the target guild isn't here,
        // the invite didn't take, and a voice join will silently no-op.
        if (auto* guilds = d.getProperty("guilds", juce::var()).getArray())
        {
            for (auto& g : *guilds)
                logLine("[Gateway] READY lists guild: " + g.getProperty("id", "").toString()
                         + " unavailable=" + juce::String((bool) g.getProperty("unavailable", false) ? "true" : "false"));
        }
        return;
    }

    if (type == "VOICE_STATE_UPDATE")
    {
        auto userId = d.getProperty("user_id", "").toString();
        logLine("[Gateway] VOICE_STATE_UPDATE for user_id=" + userId
                 + (userId == botUserId ? " (ours)" : " (other member)"));
        if (userId == botUserId)
        {
            const juce::ScopedLock lock(voiceInfoLock);
            pendingSessionId = d.getProperty("session_id", "").toString();
            haveSessionId = true;
        }
        return;
    }

    if (type == "VOICE_SERVER_UPDATE")
    {
        const juce::ScopedLock lock(voiceInfoLock);
        pendingVoiceToken = d.getProperty("token", "").toString();
        pendingEndpoint = d.getProperty("endpoint", "").toString();
        pendingGuildId = d.getProperty("guild_id", "").toString();
        haveServerUpdate = true;
        // Endpoint is logged in full deliberately - the port matters and
        // getting it wrong is exactly what broke this for a long time.
        // The token is a credential, so only its length is logged.
        logLine("[Gateway] VOICE_SERVER_UPDATE endpoint=" + pendingEndpoint + " guild_id=" + pendingGuildId
                 + " token_len=" + juce::String(pendingVoiceToken.length()));
        return;
    }

    // Anything else that arrives while we're waiting on voice info is
    // exactly what's needed to diagnose a silent join failure (missing
    // permission, wrong IDs, etc.) - log it instead of swallowing it.
    logLine("[Gateway] dispatch: " + type);
}

bool GatewayClient::waitForReady(int timeoutMs)
{
    int waited = 0;
    while (!ready.load() && waited < timeoutMs)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }
    return ready.load();
}

void GatewayClient::requestJoinVoiceChannel(const juce::String& guildId, const juce::String& channelId)
{
    auto* d = new juce::DynamicObject();
    d->setProperty("guild_id", guildId);
    // An empty channelId means "leave" - Discord requires an explicit
    // JSON null here, not an empty string, or the request is ignored.
    d->setProperty("channel_id", channelId.isEmpty() ? juce::var() : juce::var(channelId));
    d->setProperty("self_mute", false);
    d->setProperty("self_deaf", false);

    auto* obj = new juce::DynamicObject();
    obj->setProperty("op", OP_VOICE_STATE_UPDATE);
    obj->setProperty("d", juce::var(d));
    auto payload = juce::var(obj);
    logLine("[Gateway] sending: " + juce::JSON::toString(payload, true));
    sendJson(payload);
}

bool GatewayClient::waitForVoiceServerInfo(VoiceServerInfo& outInfo, int timeoutMs)
{
    int waited = 0;
    while (waited < timeoutMs)
    {
        if (haveSessionId.load() && haveServerUpdate.load())
        {
            const juce::ScopedLock lock(voiceInfoLock);
            outInfo.sessionId = pendingSessionId;
            outInfo.voiceToken = pendingVoiceToken;
            outInfo.endpoint = pendingEndpoint;
            outInfo.guildId = pendingGuildId;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }
    return false;
}
