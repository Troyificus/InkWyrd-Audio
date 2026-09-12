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
    std::unique_ptr<inkwyrd::FolderNode> model;
    std::unique_ptr<FolderItem> rootItem;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryFolderTree)
};
