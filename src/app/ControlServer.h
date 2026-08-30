#pragma once

#include <memory>
#include <juce_core/juce_core.h>

#include "PlaylistEngine.h"
#include "SoundboardEngine.h"
#include "MasterEngine.h"

namespace ix { class WebSocketServer; }

// A tiny local-only control API for external controllers (currently: the
// Stream Deck plugin under streamdeck-plugin/) to drive the running app.
// Loopback-only, no auth - nothing outside this machine can reach it,
// and there's nothing sensitive to protect locally. See
// streamdeck-plugin/src/audioAppClient.ts for the client side.
//
// Commands are plain JSON text frames: {"command": "skipTrack"},
// {"command": "toggleShuffle"}, {"command": "triggerSoundboard", "name": "..."},
// {"command": "toggleMute"}.
class ControlServer
{
public:
    ControlServer(PlaylistEngine& playlistToUse, SoundboardEngine& soundboardToUse, MasterEngine& masterEngineToUse);
    ~ControlServer();

    bool start(int port);
    void stop();

private:
    void handleCommand(const juce::var& parsed);

    PlaylistEngine& playlist;
    SoundboardEngine& soundboard;
    MasterEngine& masterEngine;

    std::unique_ptr<ix::WebSocketServer> server;
};
