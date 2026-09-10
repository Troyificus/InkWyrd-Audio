#include "DiscordRpcClient.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
    constexpr int kOpHandshake = 0;
    constexpr int kOpFrame = 1;

    // Discord opens up to ten sockets; the first is the usual one, but a
    // second running client (a Canary/PTB install alongside stable, say)
    // takes the next free index rather than failing to start.
    constexpr int kMaxPipeIndex = 10;

    // Must be a redirect URI registered on the user's application. It is
    // never navigated to and nothing is served from it - OAuth just
    // requires the token exchange to name the same URI the authorisation
    // was issued against, and AUTHORIZE silently used the application's
    // first registered one.
    constexpr const char* kRedirectUri = "https://inkwyrd.com/rpc";

    constexpr const char* kTokenUrl = "https://discord.com/api/v10/oauth2/token";

    // Without a real one of these, Cloudflare rejects the token request
    // with 403 "error code: 1010" before Discord ever sees it.
    constexpr const char* kUserAgent = "InkwyrdAudio (https://github.com/Troyificus/InkWyrd-Audio, 0.1)";

    juce::String jsonOf(const juce::var& value)
    {
        return juce::JSON::toString(value, true);
    }
}

DiscordRpcClient::DiscordRpcClient()
    : juce::Thread("Discord RPC")
{
}

DiscordRpcClient::~DiscordRpcClient()
{
    signalThreadShouldExit();
    stopThread(3000);
    closePipe();
}

juce::String DiscordRpcClient::deriveApplicationId(const juce::String& botToken)
{
    auto firstSegment = botToken.upToFirstOccurrenceOf(".", false, false).trim();
    if (firstSegment.isEmpty())
        return {};

    // Base64url, and Discord omits the padding.
    auto standard = firstSegment.replaceCharacter('-', '+').replaceCharacter('_', '/');
    while (standard.length() % 4 != 0)
        standard += "=";

    juce::MemoryOutputStream decoded;
    if (! juce::Base64::convertFromBase64(decoded, standard))
        return {};

    auto text = decoded.toString().trim();

    // A snowflake and nothing else - if the token was malformed, the
    // decode can still "succeed" and produce arbitrary bytes.
    if (text.isEmpty() || ! text.containsOnly("0123456789") || text.length() < 15)
        return {};

    return text;
}

void DiscordRpcClient::configure(const juce::String& applicationIdToUse,
                                  const juce::String& clientSecretToUse,
                                  const juce::String& refreshTokenToUse)
{
    {
        const juce::ScopedLock lock(stateLock);
        applicationId = applicationIdToUse;
        clientSecret = clientSecretToUse;
        refreshToken = refreshTokenToUse;
        accessToken = {};
    }

    ready.store(false);
}

void DiscordRpcClient::authorise(AuthoriseCallback callback)
{
    {
        const juce::ScopedLock lock(stateLock);
        pendingAuthorise = std::move(callback);
    }

    authoriseRequested.store(true);

    if (! isThreadRunning())
        startThread();
}

void DiscordRpcClient::setEnabled(bool shouldBeEnabled)
{
    enabled.store(shouldBeEnabled);

    if (shouldBeEnabled)
    {
        if (! isThreadRunning())
            startThread();
    }
    else
    {
        // Deliberately does NOT stop the thread here: it may still need
        // to put the user's own mute setting back before it goes idle.
        desiredMute.store(false);
    }
}

void DiscordRpcClient::setSelfMuted(bool shouldBeMuted)
{
    desiredMute.store(shouldBeMuted);
}

juce::String DiscordRpcClient::getLastError() const
{
    const juce::ScopedLock lock(stateLock);
    return lastError;
}

void DiscordRpcClient::reportError(const juce::String& message)
{
    const juce::ScopedLock lock(stateLock);
    lastError = message;
}

// ---------------------------------------------------------------------
// Pipe transport
// ---------------------------------------------------------------------

bool DiscordRpcClient::connectPipe()
{
#if JUCE_WINDOWS
    closePipe();

    for (int index = 0; index < kMaxPipeIndex; ++index)
    {
        auto name = "\\\\.\\pipe\\discord-ipc-" + juce::String(index);
        auto handle = CreateFileA(name.toRawUTF8(), GENERIC_READ | GENERIC_WRITE,
                                   0, nullptr, OPEN_EXISTING, 0, nullptr);

        if (handle != INVALID_HANDLE_VALUE)
        {
            pipeHandle = handle;
            return true;
        }
    }

    return false;
#else
    return false;
#endif
}

void DiscordRpcClient::closePipe()
{
#if JUCE_WINDOWS
    if (pipeHandle != nullptr)
    {
        CloseHandle((HANDLE) pipeHandle);
        pipeHandle = nullptr;
    }
#endif
    ready.store(false);
}

bool DiscordRpcClient::sendFrame(int opcode, const juce::var& payload)
{
#if JUCE_WINDOWS
    if (pipeHandle == nullptr)
        return false;

    auto body = jsonOf(payload).toRawUTF8();
    auto bodyLength = (juce::uint32) strlen(body);

    juce::MemoryBlock frame;
    frame.setSize(8 + bodyLength);
    auto* raw = (char*) frame.getData();

    // Little-endian opcode then length, then the JSON - Discord's own
    // framing, not a JUCE convention.
    auto op = (juce::uint32) opcode;
    memcpy(raw, &op, 4);
    memcpy(raw + 4, &bodyLength, 4);
    memcpy(raw + 8, body, bodyLength);

    DWORD written = 0;
    return WriteFile((HANDLE) pipeHandle, raw, (DWORD) frame.getSize(), &written, nullptr)
            && written == frame.getSize();
#else
    juce::ignoreUnused(opcode, payload);
    return false;
#endif
}

bool DiscordRpcClient::readFrame(int& opcode, juce::var& payload, int timeoutMs)
{
#if JUCE_WINDOWS
    if (pipeHandle == nullptr)
        return false;

    // Polled with PeekNamedPipe rather than a blocking ReadFile, so that
    // waiting on a consent dialog the user may never click doesn't wedge
    // this thread past shutdown.
    auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    auto waitForBytes = [&](DWORD wanted)
    {
        for (;;)
        {
            // ignoreExitSignalForRead is set only around the final
            // unmute on shutdown. Without it, the exit signal that
            // brought us here would abort the very command whose whole
            // purpose is to leave the user un-muted.
            if (threadShouldExit() && ! ignoreExitSignalForRead)
                return false;

            DWORD available = 0;
            if (! PeekNamedPipe((HANDLE) pipeHandle, nullptr, 0, nullptr, &available, nullptr))
                return false;

            if (available >= wanted)
                return true;

            if (juce::Time::getMillisecondCounter() > deadline)
                return false;

            juce::Thread::sleep(20);
        }
    };

    if (! waitForBytes(8))
        return false;

    char header[8] = {};
    DWORD read = 0;
    if (! ReadFile((HANDLE) pipeHandle, header, 8, &read, nullptr) || read != 8)
        return false;

    juce::uint32 op = 0, length = 0;
    memcpy(&op, header, 4);
    memcpy(&length, header + 4, 4);
    opcode = (int) op;

    if (length == 0)
    {
        payload = juce::var();
        return true;
    }

    if (length > 1024 * 1024)
        return false; // nothing legitimate is this big; treat as a desync

    if (! waitForBytes(length))
        return false;

    juce::MemoryBlock body((size_t) length);
    if (! ReadFile((HANDLE) pipeHandle, body.getData(), length, &read, nullptr) || read != length)
        return false;

    payload = juce::JSON::parse(body.toString());
    return true;
#else
    juce::ignoreUnused(opcode, payload, timeoutMs);
    return false;
#endif
}

// ---------------------------------------------------------------------
// Protocol
// ---------------------------------------------------------------------

bool DiscordRpcClient::handshake()
{
    juce::String appId;
    {
        const juce::ScopedLock lock(stateLock);
        appId = applicationId;
    }

    if (appId.isEmpty())
        return false;

    auto* handshakeObject = new juce::DynamicObject();
    handshakeObject->setProperty("v", 1);
    handshakeObject->setProperty("client_id", appId);

    if (! sendFrame(kOpHandshake, juce::var(handshakeObject)))
        return false;

    int opcode = 0;
    juce::var reply;
    if (! readFrame(opcode, reply, 5000))
        return false;

    return reply.getProperty("evt", {}).toString() == "READY";
}

juce::var DiscordRpcClient::sendCommand(const juce::String& command, juce::var args, int timeoutMs)
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty("cmd", command);
    payload->setProperty("args", args);
    payload->setProperty("nonce", juce::Uuid().toDashedString());

    if (! sendFrame(kOpFrame, juce::var(payload)))
        return {};

    int opcode = 0;
    juce::var reply;
    if (! readFrame(opcode, reply, timeoutMs))
        return {};

    if (reply.getProperty("evt", {}).toString() == "ERROR")
    {
        auto data = reply.getProperty("data", {});
        reportError(command + ": " + data.getProperty("message", {}).toString());
        return {};
    }

    return reply.getProperty("data", {});
}

juce::String DiscordRpcClient::postToTokenEndpoint(const juce::StringPairArray& formFields)
{
    juce::URL url(kTokenUrl);
    url = url.withParameters(formFields);

    // withPOSTData isn't used: withParameters + doPostLikeRequest makes
    // JUCE form-encode the body itself, which is what this endpoint
    // wants and avoids hand-escaping the secret into a string.
    juce::StringPairArray responseHeaders;
    int statusCode = 0;

    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders(juce::String("User-Agent: ") + kUserAgent
                                          + "\r\nAccept: application/json")
                       .withConnectionTimeoutMs(20000)
                       .withResponseHeaders(&responseHeaders)
                       .withStatusCode(&statusCode);

    auto stream = url.createInputStream(options);
    if (stream == nullptr)
    {
        reportError("Could not reach Discord's token endpoint.");
        return {};
    }

    auto body = stream->readEntireStreamAsString();

    if (statusCode < 200 || statusCode >= 300)
    {
        reportError("Token exchange failed (HTTP " + juce::String(statusCode) + "): "
                     + body.substring(0, 200));
        return {};
    }

    return body;
}

bool DiscordRpcClient::refreshAccessToken()
{
    juce::String appId, secret, refresh;
    {
        const juce::ScopedLock lock(stateLock);
        appId = applicationId;
        secret = clientSecret;
        refresh = refreshToken;
    }

    if (appId.isEmpty() || secret.isEmpty() || refresh.isEmpty())
        return false;

    juce::StringPairArray fields;
    fields.set("client_id", appId);
    fields.set("client_secret", secret);
    fields.set("grant_type", "refresh_token");
    fields.set("refresh_token", refresh);

    auto response = postToTokenEndpoint(fields);
    if (response.isEmpty())
        return false;

    auto parsed = juce::JSON::parse(response);
    auto token = parsed.getProperty("access_token", {}).toString();
    auto newRefresh = parsed.getProperty("refresh_token", {}).toString();

    if (token.isEmpty())
        return false;

    const juce::ScopedLock lock(stateLock);
    accessToken = token;
    if (newRefresh.isNotEmpty())
        refreshToken = newRefresh;
    return true;
}

bool DiscordRpcClient::authenticate()
{
    juce::String token;
    {
        const juce::ScopedLock lock(stateLock);
        token = accessToken;
    }

    if (token.isEmpty())
        return false;

    auto* args = new juce::DynamicObject();
    args->setProperty("access_token", token);

    auto result = sendCommand("AUTHENTICATE", juce::var(args), 8000);
    return result.isObject();
}

bool DiscordRpcClient::applyDesiredMuteState()
{
    auto wanted = enabled.load() && desiredMute.load();

    if (wanted == weAreMuting)
        return true;

    if (wanted)
    {
        // Capture the user's own setting BEFORE changing it - this is
        // the value that gets restored, and reading it afterwards would
        // only ever read our own mute back.
        auto current = sendCommand("GET_VOICE_SETTINGS", juce::var(new juce::DynamicObject()), 5000);
        if (! current.isObject())
            return false;

        userMuteBeforeUs = (bool) current.getProperty("mute", false);

        auto* args = new juce::DynamicObject();
        args->setProperty("mute", true);
        if (! sendCommand("SET_VOICE_SETTINGS", juce::var(args), 5000).isObject())
            return false;

        weAreMuting = true;
    }
    else
    {
        auto* args = new juce::DynamicObject();
        args->setProperty("mute", userMuteBeforeUs);
        if (! sendCommand("SET_VOICE_SETTINGS", juce::var(args), 5000).isObject())
            return false;

        weAreMuting = false;
    }

    return true;
}

// ---------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------

void DiscordRpcClient::run()
{
    while (! threadShouldExit())
    {
        // One-time consent flow, requested from Settings.
        if (authoriseRequested.exchange(false))
        {
            AuthoriseCallback callback;
            juce::String appId, secret;
            {
                const juce::ScopedLock lock(stateLock);
                callback = pendingAuthorise;
                pendingAuthorise = nullptr;
                appId = applicationId;
                secret = clientSecret;
            }

            juce::String message;
            juce::String obtainedRefresh;
            bool success = false;

            if (appId.isEmpty())
            {
                message = "No Discord application id - check the bot token in Settings.";
            }
            else if (secret.isEmpty())
            {
                message = "No client secret entered.";
            }
            else if (! connectPipe())
            {
                message = "Discord isn't running, or its local socket isn't reachable.";
            }
            else if (! handshake())
            {
                message = "Discord refused the connection. Is the bot token from the same application?";
            }
            else
            {
                // NO redirect_uri here - see the header. Long timeout:
                // this is waiting on a human clicking a consent dialog.
                auto* args = new juce::DynamicObject();
                args->setProperty("client_id", appId);

                juce::Array<juce::var> scopes;
                scopes.add("rpc");
                scopes.add("rpc.voice.write");
                args->setProperty("scopes", scopes);

                auto data = sendCommand("AUTHORIZE", juce::var(args), 120000);
                auto code = data.getProperty("code", {}).toString();

                if (code.isEmpty())
                {
                    message = getLastError().isNotEmpty() ? getLastError()
                                                           : "Authorisation was declined or timed out.";
                }
                else
                {
                    // ...but the token exchange DOES need it.
                    juce::StringPairArray fields;
                    fields.set("client_id", appId);
                    fields.set("client_secret", secret);
                    fields.set("grant_type", "authorization_code");
                    fields.set("code", code);
                    fields.set("redirect_uri", kRedirectUri);

                    auto response = postToTokenEndpoint(fields);
                    auto parsed = juce::JSON::parse(response);
                    obtainedRefresh = parsed.getProperty("refresh_token", {}).toString();
                    auto token = parsed.getProperty("access_token", {}).toString();

                    if (token.isNotEmpty())
                    {
                        const juce::ScopedLock lock(stateLock);
                        accessToken = token;
                        refreshToken = obtainedRefresh;
                        success = true;
                        message = "Inkwyrd can now mute you in Discord.";
                    }
                    else
                    {
                        message = getLastError().isNotEmpty() ? getLastError()
                                                               : "Discord did not return an access token.";
                    }
                }
            }

            if (callback)
                juce::MessageManager::callAsync([callback, success, message, obtainedRefresh]
                                                 { callback(success, message, obtainedRefresh); });
        }

        auto wantConnection = enabled.load() || weAreMuting;

        if (! wantConnection)
        {
            ready.store(false);
            wait(500);
            continue;
        }

        if (! ready.load())
        {
            juce::String refresh;
            {
                const juce::ScopedLock lock(stateLock);
                refresh = refreshToken;
            }

            if (refresh.isEmpty())
            {
                // Nothing to work with until the user authorises. Idle
                // quietly rather than hammering a socket that can't help.
                wait(2000);
                continue;
            }

            if (connectPipe() && handshake() && refreshAccessToken() && authenticate())
            {
                ready.store(true);
                reportError({});
                weAreMuting = false;
            }
            else
            {
                closePipe();
                wait(5000); // Discord may simply not be running yet
                continue;
            }
        }

        if (! applyDesiredMuteState())
        {
            // A failed command usually means the client went away.
            closePipe();
            weAreMuting = false;
            continue;
        }

        wait(150);
    }

    // Leaving someone muted because the app closed would be a genuinely
    // bad outcome, so this is a real attempt rather than best-effort
    // housekeeping.
    if (weAreMuting && pipeHandle != nullptr)
    {
        const juce::ScopedValueSetter<bool> allowRead(ignoreExitSignalForRead, true);
        auto* args = new juce::DynamicObject();
        args->setProperty("mute", userMuteBeforeUs);
        sendCommand("SET_VOICE_SETTINGS", juce::var(args), 2000);
    }

    closePipe();
}
