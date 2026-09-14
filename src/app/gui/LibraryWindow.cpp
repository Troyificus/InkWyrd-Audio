#include "LibraryWindow.h"

#include "WindowLayoutStore.h"

namespace
{
    juce::Rectangle<int> defaultLibraryBounds()
    {
        auto area = WindowLayoutStore::primaryDisplayArea();
        return juce::Rectangle<int>(440, 600).withPosition(area.getX() + 40, area.getY() + 400);
    }
}

LibraryWindow::LibraryWindow(AppSettings& settingsToUse,
                              PlaylistLibrary& library,
                              TrackLibrary& trackLibrary,
                              PlaylistEngine& playlist,
                              TrackSettingsStore& trackGains,
                              TrackMetadataStore& trackMetadata,
                              std::function<void(const juce::Uuid&)> onActivatePlaylist,
                              std::function<void(const juce::Uuid&)> onPlaylistEdited,
                              std::function<void(const juce::Uuid&)> onPlaylistSelected,
                              std::function<void(const juce::File&)> onPreviewTrack,
                              std::function<void()> onStopPreview,
                              std::function<void(const juce::Array<juce::File>&)> onEditTags)
    : DetachableWindow("Library", "library", "Audio Library", settingsToUse, defaultLibraryBounds())
{
    auto* panel = new PlaylistPanel(library, trackLibrary, playlist, trackGains, trackMetadata,
                                     std::move(onActivatePlaylist),
                                     std::move(onPlaylistEdited),
                                     std::move(onPlaylistSelected),
                                     settingsToUse.isLibraryFolderView(),
                                     [&settings = settingsToUse](bool folderView)
                                     {
                                         settings.setLibraryFolderView(folderView);
                                         settings.save(); // AppSettings has no autosave
                                     },
                                     std::move(onPreviewTrack),
                                     std::move(onStopPreview),
                                     std::move(onEditTags));
    panel->setSize(440, 620);
    playlistPanel = panel;

    // false - see PlayerWindow.cpp for why: the window's own (restored
    // or default) bounds must not be overridden by the content's fixed
    // setSize() above.
    setContentOwned(panel, false);
    setVisible(true);
}

void LibraryWindow::repaintTrackList()
{
    if (playlistPanel != nullptr)
        playlistPanel->repaintTrackList();
}

void LibraryWindow::setPreviewFile(const juce::File& file)
{
    if (playlistPanel != nullptr)
        playlistPanel->setPreviewFile(file);
}

void LibraryWindow::setPlayingPlaylistId(const juce::Uuid& id)
{
    if (playlistPanel != nullptr)
        playlistPanel->setPlayingPlaylistId(id);
}
