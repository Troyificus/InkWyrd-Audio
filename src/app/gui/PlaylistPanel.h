#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"
#include "TrackSettingsStore.h"

// Left-hand column: the playlist library on top, the selected playlist's
// tracks underneath.
//
// Selecting a playlist only browses it - activating (double-click, or the
// Play button) is what crossfades the audio over to it. Browsing must
// never interrupt what's playing mid-session.
//
// Also a file drop target for Windows Explorer: audio files and folders
// dragged anywhere onto this panel are added to the playlist row they
// were dropped on, or to the selected playlist otherwise. Dropped
// folders go through the same link-vs-snapshot question the
// "Add folder..." button asks, so a drag is never a second, subtly
// different way of doing the same thing.
class PlaylistPanel : public juce::Component,
                       public juce::FileDragAndDropTarget
{
public:
    PlaylistPanel(PlaylistLibrary& libraryToUse,
                   PlaylistEngine& engineToUse,
                   TrackSettingsStore& trackGainsToUse,
                   std::function<void(const juce::Uuid&)> onActivatePlaylist,
                   // Fired with the id of a playlist whose CONTENTS changed,
                   // so the app can push the edit into the engine if it
                   // happens to be the one currently playing.
                   std::function<void(const juce::Uuid&)> onPlaylistEdited);

    // Defined in the .cpp: the ListBoxModels below are forward-declared
    // here, and destroying a unique_ptr needs the complete type.
    ~PlaylistPanel() override;

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // The playlist currently *playing* (not merely selected), so it can be
    // marked in the list. {} for none.
    void setPlayingPlaylistId(const juce::Uuid& id);

    // Re-read the library and the selected playlist's tracks from disk.
    void refresh();

private:
    class PlaylistListModel;
    class TrackListModel;

    Playlist* getSelectedPlaylist();
    void selectPlaylist(int row);
    void activateSelected();
    void refreshTracks();
    void updateButtonEnablement();
    void selectRowForSelectedId();

    // Refresh the UI after a playlist's contents changed in memory (no
    // disk reload - the library's mutators have already saved) and tell
    // the app about it.
    void finishEdit(const juce::Uuid& id);
    void notifyEdited(const juce::Uuid& id);

    // Shared by the "Add folder..." button and a folder drop: counts what
    // the folders contain, asks link-vs-snapshot ONCE for all of them,
    // then adds them.
    void addFoldersWithPrompt(const juce::Uuid& id, const juce::Array<juce::File>& folders);

    // Opens the volume slider for one track, anchored to its row.
    void showTrackVolumeCallout(int row);

    // The width a track row is actually painted at - see the .cpp.
    int trackRowWidth();

    int playlistRowAt(int x, int y);
    void updateDragTarget(int x, int y);
    void clearDragTarget();

    void createNewPlaylist();
    void addFilesToSelected();
    void addFolderToSelected();
    void renameSelected();
    void deleteSelected();

    PlaylistLibrary& library;
    PlaylistEngine& engine;
    TrackSettingsStore& trackGains;
    std::function<void(const juce::Uuid&)> onActivatePlaylist;
    std::function<void(const juce::Uuid&)> onPlaylistEdited;

    juce::Uuid selectedId;
    juce::Uuid playingId;
    ResolvedPlaylist resolvedTracks;

    juce::Label playlistCaption { {}, "Playlists" };
    juce::ListBox playlistListBox;
    std::unique_ptr<PlaylistListModel> playlistModel;

    juce::TextButton newButton { "New" };
    juce::TextButton playButton { "Play" };
    juce::TextButton addFilesButton { "Add files..." };
    juce::TextButton addFolderButton { "Add folder..." };
    juce::TextButton renameButton { "Rename" };
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton refreshButton { "Refresh" };
    juce::TextButton openFolderButton { "Open folder" };

    juce::Label trackCaption { {}, "Tracks" };
    juce::ListBox trackListBox;
    std::unique_ptr<TrackListModel> trackModel;

    std::unique_ptr<juce::FileChooser> activeChooser;

    bool dragActive = false;
    int dragTargetRow = -1; // playlist row a drop would land on, -1 for "the selected one"
};
