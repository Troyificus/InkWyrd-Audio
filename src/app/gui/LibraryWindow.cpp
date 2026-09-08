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
                              PlaylistEngine& playlist,
                              TrackSettingsStore& trackGains,
                              std::function<void(const juce::Uuid&)> onActivatePlaylist,
                              std::function<void(const juce::Uuid&)> onPlaylistEdited)
    : DetachableWindow("Library", "library", settingsToUse, defaultLibraryBounds(), true, DocumentWindow::closeButton)
{
    auto* panel = new PlaylistPanel(library, playlist, trackGains,
                                     std::move(onActivatePlaylist),
                                     std::move(onPlaylistEdited));
    panel->setSize(440, 600);
    playlistPanel = panel;

    // false - see PlayerWindow.cpp for why: the window's own (restored
    // or default) bounds must not be overridden by the content's fixed
    // setSize() above.
    setContentOwned(panel, false);
    setVisible(true);
}

void LibraryWindow::setPlayingPlaylistId(const juce::Uuid& id)
{
    if (playlistPanel != nullptr)
        playlistPanel->setPlayingPlaylistId(id);
}

void LibraryWindow::closeButtonPressed()
{
    setVisible(false);
}
