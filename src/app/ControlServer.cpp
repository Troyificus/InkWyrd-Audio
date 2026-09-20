#include "ControlServer.h"

#include <iostream>
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXConnectionState.h>

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
        [this](std::shared_ptr<ix::ConnectionState>, ix::WebSocket&, const ix::WebSocketMessagePtr& msg)
        {
            if (msg->type != ix::WebSocketMessageType::Message)
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
    else if (command == "toggleMute")
    {
        masterEngine.setMicMuted(!masterEngine.isMicMuted());
        std::cout << "[ControlServer] mic muted = " << (masterEngine.isMicMuted() ? "true" : "false") << std::endl;
    }
}
