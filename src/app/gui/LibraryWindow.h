#pragma once

#include <functional>

#include "AppSettings.h"
#include "DetachableWindow.h"
#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"
#include "PlaylistPanel.h"
#include "TrackLibrary.h"
#include "TrackMetadataStore.h"
#include "TrackSettingsStore.h"

// Hosts PlaylistPanel: the created playlists on top, the master list of
// every track added to the app underneath. Playlist management lives
// here; the tracks OF a playlist are shown in the Playlist window, which
// follows this window's selection.
//
// No close-to-hide behaviour beyond DetachableWindow's own default, and
// no minimise button - only the master Player window has one.
class LibraryWindow : public DetachableWindow
{
public:
    LibraryWindow(AppSettings& settingsToUse,
                   PlaylistLibrary& library,
                   TrackLibrary& trackLibrary,
                   PlaylistEngine& playlist,
                   TrackSettingsStore& trackGains,
                   TrackMetadataStore& trackMetadata,
                   std::function<void(const juce::Uuid&)> onActivatePlaylist,
                   std::function<void(const juce::Uuid&)> onPlaylistEdited,
                   std::function<void(const juce::Uuid&)> onPlaylistSelected,
                   // Auditioning a track: the app pauses the playlist,
                   // plays it locally, and resumes afterwards.
                   std::function<void(const juce::File&)> onPreviewTrack = {},
                   std::function<void()> onStopPreview = {},
                   std::function<void(const juce::Array<juce::File>&)> onEditTags = {});

    // Which track is previewing, {} for none.
    void setPreviewFile(const juce::File& file);

    // The playlist currently PLAYING (not merely selected), so the
    // library list can mark it. Forwarded straight to PlaylistPanel.
    void setPlayingPlaylistId(const juce::Uuid& id);

    PlaylistPanel& getPanel() { return *playlistPanel; }

    // The background tag scan finished a batch - refresh what's shown
    // without re-reading the library from disk.
    void repaintTrackList();

private:
    PlaylistPanel* playlistPanel = nullptr; // owned via setContentOwned
};
