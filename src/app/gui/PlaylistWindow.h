#pragma once

#include <functional>

#include "AppSettings.h"
#include "DetachableWindow.h"
#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"
#include "PlaylistTrackListComponent.h"
#include "TrackMetadataStore.h"

// Hosts PlaylistTrackListComponent - the tracks of whichever playlist is
// selected in the Library window, and the place songs get dragged INTO.
//
// No close-to-hide behaviour beyond DetachableWindow's own default, and
// no minimise button: only the master Player window has one, and
// minimising it takes this window down too.
class PlaylistWindow : public DetachableWindow
{
public:
    PlaylistWindow(AppSettings& settingsToUse,
                    TrackMetadataStore& trackMetadata,
                    PlaylistLibrary& library,
                    PlaylistEngine& playlist,
                    std::function<void(const juce::Uuid&)> onPlaylistEdited,
                    std::function<void(const juce::Uuid&, const juce::File&)> onPlayTrack);

    PlaylistTrackListComponent& getTrackList() { return *trackList; }

private:
    PlaylistTrackListComponent* trackList = nullptr; // owned via setContentOwned
};
