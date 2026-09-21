#pragma once

#include <functional>
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
// {"command": "toggleMute"}, {"command": "stopAllSounds"} (the Soundboard
// Killswitch - the wire name predates that label and is kept so an older
// plugin build keeps working), {"command": "fadeOutMusic"},
// {"command": "activateScene", "name": "..."}.
class ControlServer
{
public:
    ControlServer(PlaylistEngine& playlistToUse, SoundboardEngine& soundboardToUse, MasterEngine& masterEngineToUse);
    ~ControlServer();

    bool start(int port);
    void stop();

    // How long a fade-out from a controller takes. Asked for at the moment
    // of the press rather than copied in once, so it always matches the
    // Player's own Fade out slider - one setting, not two that drift.
    std::function<double()> getFadeOutSeconds;

    // {"command": "activateScene", "name": "..."} - a Stream Deck Scene
    // key. A callback rather than a reference, because a scene touches
    // playlists, loops and the volume glide, which all live in the app.
    std::function<void(const juce::String& sceneName)> onActivateScene;

private:
    void handleCommand(const juce::var& parsed);

    PlaylistEngine& playlist;
    SoundboardEngine& soundboard;
    MasterEngine& masterEngine;

    std::unique_ptr<ix::WebSocketServer> server;
};
