#include "PlaylistWindow.h"

#include "WindowLayoutStore.h"

namespace
{
    juce::Rectangle<int> defaultPlaylistBounds()
    {
        auto area = WindowLayoutStore::primaryDisplayArea();
        return juce::Rectangle<int>(320, 480).withPosition(area.getX() + 700, area.getY() + 40);
    }
}

PlaylistWindow::PlaylistWindow(AppSettings& settingsToUse,
                                PlaylistLibrary& library,
                                PlaylistEngine& playlist,
                                std::function<void(const juce::Uuid&)> onPlaylistEdited,
                                std::function<void(const juce::Uuid&, const juce::File&)> onPlayTrack)
    : DetachableWindow("Playlist", "playlist", "Playlists", settingsToUse, defaultPlaylistBounds())
{
    auto* component = new PlaylistTrackListComponent(library, playlist,
                                                      std::move(onPlaylistEdited),
                                                      std::move(onPlayTrack));
    component->setSize(320, 480);
    trackList = component;

    // false - see PlayerWindow.cpp for why: the window's own (restored
    // or default) bounds must not be overridden by the content's fixed
    // setSize() above.
    setContentOwned(component, false);
    setVisible(true);
}
