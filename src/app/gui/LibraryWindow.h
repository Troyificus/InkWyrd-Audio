#pragma once

#include <functional>

#include "AppSettings.h"
#include "DetachableWindow.h"
#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"
#include "PlaylistPanel.h"
#include "TrackSettingsStore.h"

// Hosts PlaylistPanel - playlist management (create/rename/delete/add
// files or folders/drag-and-drop) plus browsing a selected playlist's own
// track list. Unchanged from what this panel already did embedded in the
// old single window; it just has a window of its own now. The "master
// list of every track added" panel is a later addition, not part of this
// window yet.
//
// No close-to-hide behaviour, same reasoning as PlaylistWindow - no
// activator button exists for this one either.
class LibraryWindow : public DetachableWindow
{
public:
    LibraryWindow(AppSettings& settingsToUse,
                   PlaylistLibrary& library,
                   PlaylistEngine& playlist,
                   TrackSettingsStore& trackGains,
                   std::function<void(const juce::Uuid&)> onActivatePlaylist,
                   std::function<void(const juce::Uuid&)> onPlaylistEdited);

    // The playlist currently PLAYING (not merely selected), so the
    // library list can mark it. Forwarded straight to PlaylistPanel.
    void setPlayingPlaylistId(const juce::Uuid& id);

private:
    PlaylistPanel* playlistPanel = nullptr; // owned via setContentOwned
};
