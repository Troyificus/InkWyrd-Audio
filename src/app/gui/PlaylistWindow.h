#pragma once

#include "AppSettings.h"
#include "DetachableWindow.h"
#include "NowPlayingTrackListComponent.h"
#include "PlaylistEngine.h"

// Hosts NowPlayingTrackListComponent - the tracks of whatever's currently
// playing. No close-to-hide behaviour yet: there's no activator button
// for this window (only Voice FX/Soundboard were asked to be hideable),
// so its close button is left as JUCE's harmless do-nothing default
// rather than hiding it with no way to bring it back.
class PlaylistWindow : public DetachableWindow
{
public:
    PlaylistWindow(AppSettings& settingsToUse, PlaylistEngine& playlist);

    NowPlayingTrackListComponent& getTrackList() { return *trackList; }

private:
    NowPlayingTrackListComponent* trackList = nullptr; // owned via setContentOwned
};
