#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PlaylistEngine.h"

// The Playlist window's whole content: the tracks of whichever playlist
// is CURRENTLY PLAYING - not whatever's selected for browsing in the
// Library window, which is a separate, independent selection.
//
// Deliberately display-only, per the agreed scope for this window: a
// caption naming the active playlist, the track list with a playing
// marker, and double-click-to-jump (PlaylistEngine::
// crossfadeToTrackInCurrentList). No add/remove/reorder/volume-editing
// here - all playlist management, including the per-track volume/fade
// controls, stays in the Library window's own track list (PlaylistPanel),
// so those aren't duplicated across two windows.
//
// Reads straight from PlaylistEngine (getPlayOrder/getCurrentTrackFile)
// rather than re-resolving the playlist itself, so this always shows
// exactly what's actually loaded and playing - including shuffle order -
// with no risk of drifting from it.
class NowPlayingTrackListComponent : public juce::Component,
                                      private juce::Timer
{
public:
    explicit NowPlayingTrackListComponent(PlaylistEngine& engineToUse);
    ~NowPlayingTrackListComponent() override;

    void resized() override;

    // Set from InkwyrdAudioApplication::activatePlaylist(), which already
    // has the Playlist's name to hand - simpler than giving this
    // component its own PlaylistLibrary reference just to look up one
    // string by id.
    void setPlayingPlaylistName(const juce::String& name);

private:
    class Model;

    void timerCallback() override;

    PlaylistEngine& engine;

    juce::Label captionLabel;
    juce::ListBox trackListBox;
    std::unique_ptr<Model> model;

    juce::Array<juce::File> lastSeenOrder;
    juce::File lastSeenCurrent;
};
