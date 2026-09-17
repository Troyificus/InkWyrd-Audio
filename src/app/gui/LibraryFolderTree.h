#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PlaylistEngine.h"
#include "TrackMetadataStore.h"

namespace inkwyrd
{
    // One folder in the Library window's tree view. Built from the paths
    // of the tracks in the library - nothing here touches the disk, so
    // it costs nothing to rebuild and can be tested headlessly.
    struct FolderNode
    {
        // What the row says. Usually the folder's own name, but a run of
        // folders with nothing else in them is collapsed into one row
        // ("Artist\Album"), so this can span several levels - see
        // buildFolderTree.
        juce::String name;

        // The real folder this row stands for. The tracks live here even
        // when `name` covers several levels.
        juce::File folder;

        // Tracks directly in this folder, sorted by filename - which is
        // what puts "01 ..., 02 ..." in track order. NOT sorted by title
        // tag: this view is about what's on disk, and a folder should
        // read the same whether or not the tag scan has caught up.
        juce::Array<juce::File> files;

        juce::OwnedArray<FolderNode> children;

        // Tracks here AND everywhere below, so a collapsed folder can say
        // how much is inside it.
        int totalTrackCount = 0;
    };

    // Groups tracks by the folder they're in. The returned node is an
    // invisible root: its children are the top-level folders, one per
    // drive or share the library draws from.
    //
    // Folders are matched case-insensitively, since Windows paths are -
    // the same folder reached as "G:\Music" and "g:\music" is one row.
    std::unique_ptr<FolderNode> buildFolderTree(const juce::Array<juce::File>& tracks);

    // The Library's preview control, shared by the table and the tree so
    // both views look the same: a play symbol, or - on the track that is
    // previewing - a stop symbol in a ring that pulses with `pulse`
    // (0..1, cycling).
    void drawPreviewGlyph(juce::Graphics& g, juce::Rectangle<int> bounds, bool previewing, float pulse);

    // Where that control sits in a row: a square at the left edge.
    // Always reserved, drawn or not, so text doesn't jump sideways as the
    // mouse moves down the list.
    juce::Rectangle<int> previewGlyphBounds(int rowHeight);
}

// The Library window's second view of the master track list: the same
// tracks the table shows, grouped by the folders they actually live in.
//
// Why both views: the table answers "where is that track" (sort by artist
// and read down); the tree answers "give me that album" - a folder is
// usually a record, and selecting one selects everything in it, which is
// what makes adding a whole album to a playlist one click rather than a
// rubber-band selection across a sorted list.
class LibraryFolderTree : public juce::Component
{
public:
    LibraryFolderTree(TrackMetadataStore& trackMetadataToUse, PlaylistEngine& engineToUse);
    ~LibraryFolderTree() override;

    void resized() override;

    // Drags the selection out as an OS file drag, for dropping onto the
    // Playlist window. Fired for the tree's own item components because
    // this listens on the TreeView and its children - see the .cpp.
    void mouseDrag(const juce::MouseEvent& e) override;

    // Tracks which track row the mouse is over, for the play symbol.
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // What is previewing, and the pulse phase for its ring. Repaints.
    void setPreview(const juce::File& file, float pulse);

    // A click on a row's play/stop symbol.
    std::function<void(const juce::File&)> onPreviewGlyphClicked;

    // Rebuilds from this set of tracks, keeping which folders were open
    // and which tracks were selected.
    void setTracks(const juce::Array<juce::File>& tracks);

    // Repaint only - for tags arriving, which change what rows say but
    // not which rows exist.
    void refreshLabels() { repaint(); }

    // Every track selected, in tree order. A selected FOLDER contributes
    // everything under it.
    juce::Array<juce::File> getSelectedTracks() const;

    std::function<void()> onSelectionChanged;

    // A right-click on a row, once that row is selected. The panel shows
    // the same menu the table does.
    std::function<void()> onContextMenuRequested;
    std::function<void()> onTracksDoubleClicked;

private:
    class ItemBase;
    class FolderItem;
    class TrackItem;

    // Re-selects these tracks after a rebuild, as far as the restored
    // openness allows - see the .cpp.
    void reselect(const juce::Array<juce::File>& tracks);

    TrackMetadataStore& trackMetadata;
    PlaylistEngine& engine;

    juce::TreeView tree;

    juce::File hoveredFile, previewFile;
    float previewPulse = 0.0f;
    void updateHover();

    // An OS drag runs its own modal loop, so a second must not start
    // from inside the first one's event.
    bool dragInProgress = false;

    std::unique_ptr<inkwyrd::FolderNode> model;
    std::unique_ptr<FolderItem> rootItem;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryFolderTree)
};
