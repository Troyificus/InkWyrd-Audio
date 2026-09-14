#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"
#include "TrackLibrary.h"
#include "TrackMetadataStore.h"
#include "LibraryFolderTree.h"
#include "TrackSettingsStore.h"

// The Library window: created playlists on top, and underneath the MASTER
// LIST of every track added to the app.
//
// That lower list used to show the selected playlist's contents, which
// meant clicking between playlists changed this panel while the separate
// Playlist window showed something else entirely. The split now matches
// what the windows are called: this list is the fixed set of all known
// music and only changes when tracks are added to or removed from it,
// while the Playlist window follows the selection.
//
// Getting music IN happens here ("Add files..." / "Add folder..." add to
// the library, not to any one playlist). Getting music into a PLAYLIST is
// a drag from this list onto the Playlist window, or the "Add to
// playlist" button - which is why this list is a drag source.
//
// Also a file drop target for Windows Explorer: audio files and folders
// dropped anywhere on this panel are added to the library.
class PlaylistPanel : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    PlaylistPanel(PlaylistLibrary& libraryToUse,
                   TrackLibrary& trackLibraryToUse,
                   PlaylistEngine& engineToUse,
                   TrackSettingsStore& trackGainsToUse,
                   TrackMetadataStore& trackMetadataToUse,
                   std::function<void(const juce::Uuid&)> onActivatePlaylist,
                   // Fired with the id of a playlist whose CONTENTS changed,
                   // so the app can push the edit into the engine if it
                   // happens to be the one currently playing.
                   std::function<void(const juce::Uuid&)> onPlaylistEdited,
                   // Fired when the SELECTED playlist changes, so the
                   // Playlist window can show it. Selection is browsing
                   // only - it never interrupts playback.
                   std::function<void(const juce::Uuid&)> onPlaylistSelected,
                   // Which view the track pane opens in, and a way to
                   // remember a change. Passed in rather than read from
                   // AppSettings here, so the headless tests that build
                   // this panel never touch the real settings file.
                   bool startInFolderView = false,
                   std::function<void(bool)> onTrackViewChanged = {},
                   // Audition a track locally. The app owns the policy -
                   // pausing the playlist and resuming afterwards - this
                   // panel only asks.
                   std::function<void(const juce::File&)> onPreviewTrack = {},
                   std::function<void()> onStopPreview = {},
                   // Open the tag editor on these tracks.
                   std::function<void(const juce::Array<juce::File>&)> onEditTags = {});

    // Defined in the .cpp: the ListBoxModels below are forward-declared
    // here, and destroying a unique_ptr needs the complete type.
    ~PlaylistPanel() override;

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;

    // Starts the drag of selected tracks onto the Playlist window. Fired
    // for the track table's rows because this panel listens on the table
    // and its children - see the .cpp for why the table can't do it.
    void mouseDrag(const juce::MouseEvent& e) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // The playlist currently *playing* (not merely selected), so it can be
    // marked in the list. {} for none.
    void setPlayingPlaylistId(const juce::Uuid& id);

    juce::Uuid getSelectedPlaylistId() const { return selectedId; }

    // Re-read the playlists and the master track list from disk.
    void refresh();

    // Repaint the track list without re-reading anything - for when the
    // background tag scan fills in rows that are already on screen.
    void repaintTrackList();

    // Which track is being previewed, {} for none. Drives the button and
    // the caption; the app is the one that knows.
    void setPreviewFile(const juce::File& file);

private:
    class PlaylistListModel;
    class LibraryTrackTableModel;
    class DraggableTrackTable;

    Playlist* getSelectedPlaylist();
    void selectPlaylist(int row);
    void activateSelected();
    void refreshLibraryTracks();
    void togglePreviewFor(const juce::File& file);

    // Drives the ring pulsing around the stop symbol while a preview
    // plays. Only runs while something is previewing.
    void timerCallback() override;

    // Where the play/stop symbol sits inside a Title cell.
    static juce::Rectangle<int> previewGlyphBounds(int cellHeight);

    // The right-click menu for tracks, shared by the table and the
    // folder tree. rowForVolume is -1 when there is no row to anchor the
    // volume callout to (the tree), which drops that one item.
    void showTracksContextMenu(const juce::Array<juce::File>& tracks, int rowForVolume);
    void updateTrackCaption();
    void sortLibraryTracks();
    void setFolderView(bool shouldShowFolders, bool notify);
    void updateButtonEnablement();
    void selectRowForSelectedId();
    void notifyEdited(const juce::Uuid& id);

    // Opens the volume slider for one track, anchored to its row.
    void showTrackVolumeCallout(int row);

    // The width a track row is actually painted at - see the .cpp.
    int trackRowWidth();

    void createNewPlaylist();
    void addFilesToLibrary();
    void addFolderToLibrary();
    void addSelectedTracksToPlaylist();
    void removeSelectedTracksFromLibrary();
    void renameSelected();
    void deleteSelected();

    // Every track currently selected in the master list.
    juce::Array<juce::File> getSelectedLibraryTracks() const;

    PlaylistLibrary& library;
    TrackLibrary& trackLibrary;
    PlaylistEngine& engine;
    TrackSettingsStore& trackGains;
    TrackMetadataStore& trackMetadata;
    std::function<void(const juce::Uuid&)> onActivatePlaylist;
    std::function<void(const juce::Uuid&)> onPlaylistEdited;
    std::function<void(const juce::Uuid&)> onPlaylistSelected;

    juce::Uuid selectedId;
    juce::Uuid playingId;

    // Which column the master list is ordered by, and which way. Held
    // here rather than read back off the header so the order survives the
    // list being rebuilt - a track added, or the background tag scan
    // finishing and changing what half the rows say.
    int sortColumnId = 1;
    bool sortForwards = true;

    // The master list, cached so painting a row doesn't re-sort the whole
    // library on every repaint.
    juce::Array<juce::File> libraryTracks;

    juce::Label playlistCaption { {}, "Playlists" };
    juce::ListBox playlistListBox;
    std::unique_ptr<PlaylistListModel> playlistModel;

    juce::TextButton newButton { "New" };
    juce::TextButton playButton { "Play" };
    juce::TextButton renameButton { "Rename" };
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton refreshButton { "Refresh" };

    juce::Label trackCaption { {}, "All Tracks" };
    std::unique_ptr<DraggableTrackTable> trackTable;
    std::unique_ptr<LibraryTrackTableModel> trackModel;

    // The same tracks, grouped by the folders they live in. Both views
    // exist at once and one is hidden: rebuilding the hidden one on every
    // switch would lose which folders were open.
    std::unique_ptr<LibraryFolderTree> folderTree;
    juce::TextButton tableViewButton { "Table" };
    juce::TextButton folderViewButton { "Folders" };
    bool folderView = false;
    std::function<void(bool)> onTrackViewChanged;

    juce::TextButton addFilesButton { "Add files..." };
    juce::TextButton addFolderButton { "Add folder..." };
    juce::TextButton addToPlaylistButton { "Add to playlist" };
    juce::TextButton removeFromLibraryButton { "Remove" };

    std::function<void(const juce::File&)> onPreviewTrack;
    std::function<void()> onStopPreview;
    std::function<void(const juce::Array<juce::File>&)> onEditTags;
    juce::File previewFile;

    // 0..1 and cycling, for the pulse around the stop symbol.
    float previewPulse = 0.0f;

    std::unique_ptr<juce::FileChooser> activeChooser;

    bool dragActive = false;

    // An OS drag runs its own modal loop; without this a second drag can
    // start from inside the first one's event.
    bool dragInProgress = false;
};
