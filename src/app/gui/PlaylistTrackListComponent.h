#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PlaylistEngine.h"
#include "TrackMetadataStore.h"
#include "PlaylistLibrary.h"

// The Playlist window's whole content: the tracks of whichever playlist
// is currently SELECTED in the Library window.
//
// It used to show whatever was PLAYING, read straight off the engine's
// play order. That was the wrong split: clicking through playlists in the
// Library window changed the Library's own lower pane instead, so the
// Library showed one playlist's contents while this window showed
// another's. Now the Library's lower pane is the fixed master track list
// and THIS window is what follows the selection - which is also what
// makes "drag a song from the library onto your playlist" mean something.
//
// Still marks the playing track, so when the selected and playing
// playlists are the same (the common case) it reads exactly as before.
//
// Title and Artist columns, like the Library's table - but NOT sortable.
// The order here is the playlist's own order, which is the order it plays
// in with shuffle off; a header click that re-ordered the view would make
// the list lie about what plays next.
class PlaylistTrackListComponent : public juce::Component,
                                    public juce::FileDragAndDropTarget,
                                    private juce::Timer
{
public:
    PlaylistTrackListComponent(PlaylistLibrary& libraryToUse,
                                TrackMetadataStore& trackMetadataToUse,
                                PlaylistEngine& engineToUse,
                                // Files dropped in, or a track removed:
                                // the app re-resolves and pushes to the
                                // engine if this is the playing list.
                                std::function<void(const juce::Uuid&)> onPlaylistEdited,
                                // Double-click: play this track, activating
                                // its playlist first if it isn't already.
                                std::function<void(const juce::Uuid&, const juce::File&)> onPlayTrack,
                                // Right-click actions, same as the
                                // Library's: audition a track, or edit
                                // what the file says it is.
                                std::function<void(const juce::File&)> onPreviewTrack = {},
                                std::function<void(const juce::Array<juce::File>&)> onEditTags = {});

    ~PlaylistTrackListComponent() override;

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;

    // Which playlist to show. {} clears the list.
    void setPlaylist(const juce::Uuid& id);
    juce::Uuid getShownPlaylistId() const { return shownId; }

    // Re-read the shown playlist from the library (after an edit made
    // somewhere else, e.g. tracks added from the Library window).
    void refresh();

    // Repaint only - for tags arriving from a scan, which change what the
    // rows say but never which rows there are.
    void repaintTracks() { trackTable.repaint(); }

    // juce::FileDragAndDropTarget - accepts both a drag from the Library
    // window's master list and a plain drag from Explorer; they arrive
    // through the same path because the in-app drag is performed as a
    // real external file drag (JUCE's own drag-and-drop containers don't
    // span separate desktop windows).
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    class Model;

    void timerCallback() override;
    void removeSelectedTrack();
    void showContextMenuForRow(int row);
    void updateButtons();

    PlaylistLibrary& library;
    TrackMetadataStore& trackMetadata;
    PlaylistEngine& engine;
    std::function<void(const juce::Uuid&)> onPlaylistEdited;
    std::function<void(const juce::Uuid&, const juce::File&)> onPlayTrack;
    std::function<void(const juce::File&)> onPreviewTrack;
    std::function<void(const juce::Array<juce::File>&)> onEditTags;

    juce::Uuid shownId;
    ResolvedPlaylist resolvedTracks;

    juce::Label captionLabel;
    juce::TableListBox trackTable;
    std::unique_ptr<Model> model;
    juce::TextButton removeButton { "Remove from playlist" };

    juce::File lastSeenPlayingTrack;
    bool dragActive = false;
};
