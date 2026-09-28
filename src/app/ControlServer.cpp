#include "ControlServer.h"

#include <iostream>
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXConnectionState.h>

#include "ControlOrigin.h"
#include "Log.h"

ControlServer::ControlServer(PlaylistEngine& playlistToUse, SoundboardEngine& soundboardToUse,
                              MasterEngine& masterEngineToUse)
    : playlist(playlistToUse), soundboard(soundboardToUse), masterEngine(masterEngineToUse)
{
}

ControlServer::~ControlServer()
{
    stop();
}

bool ControlServer::start(int port)
{
    // Explicit loopback host - never bind on all interfaces, even
    // though this has no auth (there's nothing sensitive to protect
    // locally, but the local *network* is a different trust boundary).
    server = std::make_unique<ix::WebSocketServer>(port, "127.0.0.1");

    server->setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> state, ix::WebSocket& socket,
                const ix::WebSocketMessagePtr& msg)
        {
            if (msg->type == ix::WebSocketMessageType::Open)
            {
                // See ControlOrigin.h: a handshake carrying an Origin
                // header came from a web page, and web pages don't get
                // to drive the app.
                if (! inkwyrd::controlHandshakeAllowed(msg->openInfo.headers))
                {
                    const auto origin = msg->openInfo.headers.find("Origin");
                    logLine("[ControlServer] refused a connection from a web page, origin: "
                             + juce::String(origin != msg->openInfo.headers.end() ? origin->second
                                                                                  : std::string("(empty)")));
                    rejectConnection(state->getId());
                    socket.close(ix::WebSocketCloseConstants::kNormalClosureCode,
                                  "Inkwyrd Audio does not accept control connections from web pages");
                }
                return;
            }

            if (msg->type == ix::WebSocketMessageType::Close)
            {
                forgetConnection(state->getId());
                return;
            }

            if (msg->type != ix::WebSocketMessageType::Message)
                return;

            // close() above asks the peer to go away; it does not
            // guarantee nothing else was already in flight behind the
            // handshake. Commands from a refused connection are dropped
            // for as long as it exists, so the refusal can't be raced.
            if (isRejected(state->getId()))
                return;

            auto parsed = juce::JSON::parse(juce::String(msg->str));
            if (!parsed.isObject())
                return;

            // The websocket server's own thread delivers this callback -
            // every engine method here expects to be called from the
            // message thread (see PlaylistEngine.h), so marshal it there
            // rather than touching engine state directly.
            juce::MessageManager::callAsync([this, parsed] { handleCommand(parsed); });
        });

    auto result = server->listenAndStart();
    if (!result)
        server.reset();
    return result;
}

void ControlServer::stop()
{
    if (server != nullptr)
    {
        server->stop();
        server.reset();
    }
}

void ControlServer::handleCommand(const juce::var& parsed)
{
    auto command = parsed.getProperty("command", "").toString();
    std::cout << "[ControlServer] received: " << command.toStdString() << std::endl;

    if (command == "skipTrack")
    {
        playlist.skipToNext();
    }
    else if (command == "toggleShuffle")
    {
        playlist.setShuffle(!playlist.isShuffleEnabled());
    }
    else if (command == "triggerSoundboard")
    {
        auto name = parsed.getProperty("name", "").toString();
        if (name.isNotEmpty())
            soundboard.trigger(name);
    }
    else if (command == "stopAllSounds")
    {
        soundboard.stopAllVoices();
    }
    else if (command == "activateScene")
    {
        auto name = parsed.getProperty("name", "").toString();
        if (name.isNotEmpty() && onActivateScene != nullptr)
            onActivateScene(name);
    }
    else if (command == "fadeOutMusic")
    {
        // The same guard the Player's own button has: nothing to fade if
        // nothing is playing, and a second press mid-fade must not
        // restart the fade from the top and drag it out.
        if (playlist.isPlaying() && ! playlist.isFadingOut())
            playlist.fadeOutAndStop(getFadeOutSeconds != nullptr ? getFadeOutSeconds() : 5.0);
    }
    else if (command == "toggleMute")
    {
        masterEngine.setMicMuted(!masterEngine.isMicMuted());
        std::cout << "[ControlServer] mic muted = " << (masterEngine.isMicMuted() ? "true" : "false") << std::endl;
    }
}

void ControlServer::rejectConnection(const std::string& connectionId)
{
    const std::lock_guard<std::mutex> lock(rejectedMutex);
    rejectedConnections.insert(connectionId);
}

void ControlServer::forgetConnection(const std::string& connectionId)
{
    const std::lock_guard<std::mutex> lock(rejectedMutex);
    rejectedConnections.erase(connectionId);
}

bool ControlServer::isRejected(const std::string& connectionId) const
{
    const std::lock_guard<std::mutex> lock(rejectedMutex);
    return rejectedConnections.count(connectionId) > 0;
}
