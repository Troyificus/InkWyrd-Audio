#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"

// Left-hand column: the playlist library on top, the selected playlist's
// tracks underneath.
//
// Selecting a playlist only browses it - activating (double-click, or the
// Play button) is what crossfades the audio over to it. Browsing must
// never interrupt what's playing mid-session.
class PlaylistPanel : public juce::Component
{
public:
    PlaylistPanel(PlaylistLibrary& libraryToUse,
                   PlaylistEngine& engineToUse,
                   std::function<void(const juce::Uuid&)> onActivatePlaylist,
                   std::function<void()> onLibraryChanged);

    // Defined in the .cpp: the ListBoxModels below are forward-declared
    // here, and destroying a unique_ptr needs the complete type.
    ~PlaylistPanel() override;

    void resized() override;

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

    void createNewPlaylist();
    void addFilesToSelected();
    void addFolderToSelected();
    void renameSelected();
    void deleteSelected();

    PlaylistLibrary& library;
    PlaylistEngine& engine;
    std::function<void(const juce::Uuid&)> onActivatePlaylist;
    std::function<void()> onLibraryChanged;

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
};
