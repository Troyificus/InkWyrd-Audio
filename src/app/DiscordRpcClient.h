#pragma once

#include <atomic>
#include <functional>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h> // MessageManager::callAsync, for reporting back to the UI

// Mutes the user's OWN Discord client while their microphone is live in
// Inkwyrd, so their voice doesn't arrive twice in the call - once from
// Discord directly and again through the bot.
//
// WHY THIS AND NOT A SERVER MUTE. Server-muting via
// PATCH /guilds/{id}/members/{user} is blocked by role hierarchy and is
// outright impossible when the target is the guild owner. This talks to
// the Discord DESKTOP CLIENT running on the same machine, over its local
// RPC socket, and needs no server permissions at all - it works in any
// guild, including ones the user doesn't administer.
//
// WHAT THE USER HAS TO DO ONCE. Register a redirect URI on their Discord
// application and paste its client secret into Settings. The scopes
// involved (`rpc`, `rpc.voice.write`) are gated for general distribution
// but ARE grantable by an application's own owner - and since every
// Inkwyrd user creates their own Discord application, every user is that
// owner. Proven end to end before any of this was built; see CLAUDE.md.
//
// THE PROTOCOL TRAP, because it will look like an auth failure. AUTHORIZE
// over the pipe must be sent with NO redirect_uri field, while the HTTP
// token exchange REQUIRES one. Opposite rules for the two calls. And the
// token request needs a real User-Agent or Cloudflare - not Discord -
// answers 403 "error code: 1010", which reads exactly like a bad secret.
//
// Everything here happens on a background thread. The pipe reads block,
// the token exchange is network I/O, and the consent dialog waits on a
// human; none of that belongs on the message thread.
class DiscordRpcClient : private juce::Thread
{
public:
    DiscordRpcClient();
    ~DiscordRpcClient() override;

    // Derives the Discord APPLICATION id from a bot token. Bot tokens are
    // `base64url(application id).timestamp.hmac`, so the id the RPC flow
    // needs is already sitting in the token the user configured for the
    // bot - no reason to make them find and paste it a second time.
    // Returns an empty string if the token isn't shaped like one.
    static juce::String deriveApplicationId(const juce::String& botToken);

    // Message thread. `refreshToken` may be empty on first use, in which
    // case the client stays idle until authorise() is called.
    void configure(const juce::String& applicationId,
                    const juce::String& clientSecret,
                    const juce::String& refreshToken);

    // Runs the one-time consent flow: connect, AUTHORIZE (which puts a
    // dialog in front of the user), exchange the code for a token. The
    // callback lands on the message thread; a non-empty refresh token
    // means it worked and should be persisted.
    using AuthoriseCallback = std::function<void(bool success,
                                                  juce::String message,
                                                  juce::String refreshToken)>;
    void authorise(AuthoriseCallback callback);

    // Whether a mute request would actually reach Discord right now.
    bool isReady() const { return ready.load(); }

    // The whole point. Safe to call from any thread and cheap to call
    // repeatedly - the worker only acts when the desired state actually
    // changes. Muting captures whatever the user's own mute setting was
    // first, and unmuting puts exactly that back: someone who was
    // already muted before Inkwyrd touched anything must not find
    // themselves unmuted afterwards.
    void setSelfMuted(bool shouldBeMuted);

    // Master switch. When off, setSelfMuted() is ignored entirely and no
    // connection is attempted.
    void setEnabled(bool shouldBeEnabled);
    bool isEnabled() const { return enabled.load(); }

    // Last thing that went wrong, for the Settings screen to show. Empty
    // when healthy.
    juce::String getLastError() const;

private:
    void run() override;

    // All of these run on the worker thread.
    bool connectPipe();
    void closePipe();
    bool sendFrame(int opcode, const juce::var& payload);
    bool readFrame(int& opcode, juce::var& payload, int timeoutMs);
    bool handshake();
    juce::var sendCommand(const juce::String& command, juce::var args, int timeoutMs);
    bool refreshAccessToken();
    bool authenticate();
    bool applyDesiredMuteState();
    void reportError(const juce::String& message);

    juce::String postToTokenEndpoint(const juce::StringPairArray& formFields);

    void* pipeHandle = nullptr;

    juce::CriticalSection stateLock;
    juce::String applicationId, clientSecret, refreshToken, accessToken, lastError;
    AuthoriseCallback pendingAuthorise;

    std::atomic<bool> enabled { false };
    std::atomic<bool> ready { false };
    std::atomic<bool> desiredMute { false };
    std::atomic<bool> authoriseRequested { false };

    // What the user's own mute setting was before Inkwyrd changed it, and
    // whether we've actually changed it. Worker thread only.
    bool weAreMuting = false;
    bool userMuteBeforeUs = false;

    // Set only while the shutdown unmute is in flight - see readFrame().
    bool ignoreExitSignalForRead = false;
};
