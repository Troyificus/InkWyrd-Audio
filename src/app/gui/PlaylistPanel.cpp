#include "PlaylistPanel.h"

#include "Dialogs.h"
#include "InkwyrdTheme.h"
#include "TrackSearch.h"
#include "VolumeCallout.h"

namespace
{
    // The per-track volume bar lives at the right-hand end of a row.
    // Same idea as the soundboard buttons: a readout you can see at a
    // glance, that opens a slider when you click it.
    constexpr int kTrackVolumeBarWidth = 44;
    constexpr int kTrackVolumeBarHeight = 5;
    constexpr int kTrackVolumeRightInset = 8;

    juce::Rectangle<int> trackVolumeBarBounds(int rowWidth, int rowHeight)
    {
        return { rowWidth - kTrackVolumeBarWidth - kTrackVolumeRightInset,
                  (rowHeight - kTrackVolumeBarHeight) / 2,
                  kTrackVolumeBarWidth,
                  kTrackVolumeBarHeight };
    }

    float trackGainFraction(float gainDb)
    {
        return juce::jlimit(0.0f, 1.0f,
                             (gainDb - TrackSettingsStore::kMinDb)
                              / (TrackSettingsStore::kMaxDb - TrackSettingsStore::kMinDb));
    }

    void drawTrackGainBar(juce::Graphics& g, juce::Rectangle<float> bar, float gainDb)
    {
        g.setColour(inkwyrd::theme::background);
        g.fillRoundedRectangle(bar, 2.0f);

        // Boost stays a warning colour rather than becoming another
        // green: it's the one state on this bar that can clip, and
        // making it match everything else would hide that.
        auto untouched = juce::approximatelyEqual(gainDb, 0.0f);
        g.setColour(gainDb > 0.0f ? inkwyrd::theme::warning.withAlpha(0.9f)
                                   : inkwyrd::theme::accent.withAlpha(untouched ? 0.35f : 0.9f));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * trackGainFraction(gainDb)), 2.0f);

        // Unity tick - see the matching comment in the soundboard grid.
        auto tickX = bar.getX() + bar.getWidth() * trackGainFraction(0.0f);
        g.setColour(inkwyrd::theme::text.withAlpha(0.55f));
        g.fillRect(juce::Rectangle<float>(tickX - 0.5f, bar.getY() - 1.0f, 1.0f, bar.getHeight() + 2.0f));

        g.setColour(inkwyrd::theme::outline);
        g.drawRoundedRectangle(bar, 2.0f, 1.0f);
    }

    constexpr int kRowHeight = 24;
    constexpr int kCaptionHeight = 22;
    constexpr int kButtonHeight = 26;
    constexpr int kButtonGap = 4;
}

//==============================================================================
// The master track list. A plain TableListBox: the drag OUT of it is
// driven by PlaylistPanel as a mouse listener, not from here - see
// PlaylistPanel::mouseDrag for why an override on this class could never
// work.
class PlaylistPanel::DraggableTrackTable : public juce::TableListBox
{
};

//==============================================================================
class PlaylistPanel::PlaylistListModel : public juce::ListBoxModel
{
public:
    explicit PlaylistListModel(PlaylistPanel& ownerToUse) : owner(ownerToUse) {}

    int getNumRows() override { return owner.library.getNumPlaylists(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        auto* playlist = owner.library.getPlaylist(row);
        if (playlist == nullptr)
            return;

        auto playing = playlist->id == owner.playingId;

        if (selected)
            g.fillAll(inkwyrd::theme::accentSoft.withAlpha(0.35f));

        if (playing)
        {
            g.setColour(inkwyrd::theme::accent);
            g.fillRect(0, 0, 3, height);
        }

        g.setColour(playing ? inkwyrd::theme::accent : inkwyrd::theme::text);
        g.drawText((playing ? juce::String::fromUTF8("\xe2\x96\xb6 ") : juce::String("   ")) + playlist->name,
                    6, 0, width - 12, height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int row) override { owner.selectPlaylist(row); }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { owner.activateSelected(); }

private:
    PlaylistPanel& owner;
};

//==============================================================================
// The master track list: every track the app knows about, independent of
// which playlists happen to reference it.
class PlaylistPanel::LibraryTrackTableModel : public juce::TableListBoxModel
{
public:
    // Column ids. Stable numbers, not indices - juce::TableHeaderComponent
    // identifies columns by these, and they end up in the sort state.
    enum ColumnId { title = 1, artist, album, genre, volume };

    explicit LibraryTrackTableModel(PlaylistPanel& ownerToUse) : owner(ownerToUse) {}

    int getNumRows() override { return owner.libraryTracks.size(); }

    void paintRowBackground(juce::Graphics& g, int row, int, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow(row, owner.libraryTracks.size()))
            return;

        if (selected)
            g.fillAll(inkwyrd::theme::accentSoft.withAlpha(0.35f));

        // The now-playing marker is a bar on the row background rather
        // than a glyph in the Title cell: sorting moves columns around,
        // and a marker that lives in one of them would vanish the moment
        // you sorted by something else.
        if (owner.libraryTracks[row] == owner.engine.getCurrentTrackFile())
        {
            g.setColour(inkwyrd::theme::accent);
            g.fillRect(0, 0, 3, height);
        }
    }

    void paintCell(juce::Graphics& g, int row, int columnId, int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow(row, owner.libraryTracks.size()))
            return;

        auto file = owner.libraryTracks[row];

        if (columnId == volume)
        {
            drawTrackGainBar(g, trackVolumeBarBounds(width, height).toFloat(),
                              owner.trackGains.getGainDb(file));
            return;
        }

        auto metadata = owner.trackMetadata.get(file);
        auto playing = file == owner.engine.getCurrentTrackFile();
        auto missing = ! file.existsAsFile();

        juce::String text;
        switch (columnId)
        {
            case title:  text = metadata.displayTitle(file); break;
            case artist: text = metadata.artist; break;
            case album:  text = metadata.album; break;
            case genre:  text = metadata.genre; break;
            default: break;
        }

        auto area = juce::Rectangle<int>(6, 0, width - 12, height);

        if (columnId == title)
        {
            // The preview control lives at the left of the Title cell: a
            // play symbol on the row under the mouse, a stop symbol in a
            // pulsing ring on the one that is previewing. The same
            // control the folder view draws - see drawPreviewGlyph.
            auto glyph = inkwyrd::previewGlyphBounds(height);
            auto previewing = file == owner.previewFile;

            if (previewing || row == owner.hoveredRow)
                inkwyrd::drawPreviewGlyph(g, glyph, previewing, owner.previewPulse);

            area.removeFromLeft(glyph.getRight() - area.getX() + 2);

            if (missing)
                text += "   (missing)";

            // A custom fade only matters next to the track's name, and
            // only the Title column is guaranteed wide enough to say it.
            auto fadeSeconds = owner.trackGains.getFadeSeconds(file);
            if (fadeSeconds > 0.0)
            {
                auto suffixArea = area.removeFromRight(62);
                g.setColour(inkwyrd::theme::textDim);
                g.setFont(juce::Font(juce::FontOptions(11.0f)));
                g.drawText(juce::String(fadeSeconds, 1) + "s fade", suffixArea,
                            juce::Justification::centredRight, false);
            }
        }

        // Tags that haven't been scanned yet are dimmed rather than left
        // blank, so a library still filling in reads as "working" rather
        // than "these files have no tags".
        auto unscanned = ! metadata.scanned && columnId != title;

        g.setColour(missing ? inkwyrd::theme::warning
                            : (playing ? inkwyrd::theme::accent
                                       : (unscanned ? inkwyrd::theme::textDim : inkwyrd::theme::text)));
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(text, area, juce::Justification::centredLeft, true);
    }

    void cellClicked(int row, int columnId, const juce::MouseEvent& event) override
    {
        if (! juce::isPositiveAndBelow(row, owner.libraryTracks.size()))
            return;

        // The volume column IS the control - clicking anywhere in it
        // opens the slider. Much easier to hit than the bar was when it
        // floated at the right-hand end of a full-width row.
        if (event.mods.isPopupMenu())
        {
            // Right-click used to open the volume callout directly. It is
            // one item on the menu now - a track has more than one thing
            // you might want to do to it.
            auto tracks = owner.getSelectedLibraryTracks();
            if (tracks.isEmpty() && juce::isPositiveAndBelow(row, owner.libraryTracks.size()))
                tracks.add(owner.libraryTracks[row]);

            owner.showTracksContextMenu(tracks);
            return;
        }

        if (columnId == volume)
        {
            owner.showTrackVolumeCallout(row);
            return;
        }

        // A click on the play/stop symbol starts or stops the preview.
        // The event arrives relative to the ROW component (see
        // performSelection in juce_TableListBox.cpp), not the table - so
        // the symbol is placed by the column's x within the row, y 0.
        // Measuring from the table's top-left only ever matched the
        // first row, which is why beta.22.1's table symbol did nothing.
        if (columnId == title && juce::isPositiveAndBelow(row, owner.libraryTracks.size()))
        {
            auto& header = owner.trackTable->getHeader();
            auto column = header.getColumnPosition(header.getIndexOfColumnId(columnId, true));
            auto glyph = inkwyrd::previewGlyphBounds(owner.trackTable->getRowHeight()).translated(column.getX(), 0);

            if (glyph.expanded(4).contains(event.getPosition()))
                owner.togglePreviewFor(owner.libraryTracks[row]);
        }
    }

    void cellDoubleClicked(int, int, const juce::MouseEvent&) override
    {
        owner.addSelectedTracksToPlaylist();
    }

    void selectedRowsChanged(int) override { owner.updateButtonEnablement(); }
    void deleteKeyPressed(int) override { owner.removeSelectedTracksFromLibrary(); }

    void sortOrderChanged(int newSortColumnId, bool isForwards) override
    {
        owner.sortColumnId = newSortColumnId;
        owner.sortForwards = isForwards;
        owner.sortLibraryTracks();
    }

private:
    PlaylistPanel& owner;
};

//==============================================================================
PlaylistPanel::PlaylistPanel(PlaylistLibrary& libraryToUse,
                              TrackLibrary& trackLibraryToUse,
                              PlaylistEngine& engineToUse,
                              TrackSettingsStore& trackGainsToUse,
                              TrackMetadataStore& trackMetadataToUse,
                              std::function<void(const juce::Uuid&)> onActivatePlaylistToUse,
                              std::function<void(const juce::Uuid&)> onPlaylistEditedToUse,
                              std::function<void(const juce::Uuid&)> onPlaylistSelectedToUse,
                              bool startInFolderView,
                              std::function<void(bool)> onTrackViewChangedToUse,
                              std::function<void(const juce::File&)> onPreviewTrackToUse,
                              std::function<void()> onStopPreviewToUse,
                              std::function<void(const juce::Array<juce::File>&)> onEditTagsToUse)
    : library(libraryToUse),
      trackLibrary(trackLibraryToUse),
      engine(engineToUse),
      trackGains(trackGainsToUse),
      trackMetadata(trackMetadataToUse),
      onActivatePlaylist(std::move(onActivatePlaylistToUse)),
      onPlaylistEdited(std::move(onPlaylistEditedToUse)),
      onPlaylistSelected(std::move(onPlaylistSelectedToUse)),
      onTrackViewChanged(std::move(onTrackViewChangedToUse)),
      onPreviewTrack(std::move(onPreviewTrackToUse)),
      onStopPreview(std::move(onStopPreviewToUse)),
      onEditTags(std::move(onEditTagsToUse))
{
    playlistModel = std::make_unique<PlaylistListModel>(*this);
    trackModel = std::make_unique<LibraryTrackTableModel>(*this);
    trackTable = std::make_unique<DraggableTrackTable>();

    addAndMakeVisible(playlistCaption);
    playlistListBox.setModel(playlistModel.get());
    playlistListBox.setRowHeight(kRowHeight);
    addAndMakeVisible(playlistListBox);

    for (auto* button : { &newButton, &playButton, &renameButton, &deleteButton,
                           &refreshButton,
                           &addFilesButton, &addFolderButton,
                           &addToPlaylistButton, &removeFromLibraryButton })
        addAndMakeVisible(button);

    newButton.onClick = [this] { createNewPlaylist(); };
    playButton.onClick = [this] { activateSelected(); };
    renameButton.onClick = [this] { renameSelected(); };
    deleteButton.onClick = [this] { deleteSelected(); };
    refreshButton.onClick = [this]
    {
        // Refresh exists to pick up files added to a LINKED folder since
        // the playlist was loaded, so it has to reach the engine too -
        // otherwise re-scanning updates what's on screen while the thing
        // actually playing carries on with the old files.
        refresh();
        notifyEdited(playingId);
    };

    addFilesButton.onClick = [this] { addFilesToLibrary(); };
    addFolderButton.onClick = [this] { addFolderToLibrary(); };
    addToPlaylistButton.onClick = [this] { addSelectedTracksToPlaylist(); };
    removeFromLibraryButton.onClick = [this] { removeSelectedTracksFromLibrary(); };

    addAndMakeVisible(trackCaption);
    trackTable->setModel(trackModel.get());
    trackTable->setRowHeight(kRowHeight);
    trackTable->setMultipleSelectionEnabled(true);

    auto& header = trackTable->getHeader();
    using Column = LibraryTrackTableModel::ColumnId;

    // Proportional widths: the last argument to addColumn is a minimum,
    // and setStretchToFitActiveColumns below shares the real width out
    // in proportion to these.
    header.addColumn("Title",  Column::title,  260, 120);
    header.addColumn("Artist", Column::artist, 150, 70);
    header.addColumn("Album",  Column::album,  150, 70);
    header.addColumn("Genre",  Column::genre,  110, 60);

    // Not sortable, and deliberately: it's a control, not a value, and
    // clicking its header to sort by loudness is not a thing anyone
    // wants. notResizable keeps it exactly as wide as the bar needs.
    header.addColumn("Vol", Column::volume,
                      kTrackVolumeBarWidth + kTrackVolumeRightInset * 2,
                      kTrackVolumeBarWidth + kTrackVolumeRightInset * 2,
                      kTrackVolumeBarWidth + kTrackVolumeRightInset * 2,
                      juce::TableHeaderComponent::visible);

    header.setStretchToFitActive(true);
    header.setSortColumnId(sortColumnId, sortForwards);

    // true: events from the row components too, which is the whole point
    // - see mouseDrag().
    trackTable->addMouseListener(this, true);
    addAndMakeVisible(trackTable.get());

    folderTree = std::make_unique<LibraryFolderTree>(trackMetadata, engine);
    folderTree->onSelectionChanged = [this] { updateButtonEnablement(); };
    folderTree->onTracksDoubleClicked = [this] { addSelectedTracksToPlaylist(); };
    folderTree->onContextMenuRequested = [this] { showTracksContextMenu(folderTree->getSelectedTracks()); };
    folderTree->onPreviewGlyphClicked = [this](const juce::File& file) { togglePreviewFor(file); };
    addChildComponent(folderTree.get());

    searchBox.setTextToShowWhenEmpty("Search title, artist, album...", inkwyrd::theme::textDim);
    searchBox.setEscapeAndReturnKeysConsumed(false);
    searchBox.onTextChange = [this]
    {
        auto typed = searchBox.getText().trim();
        if (typed == searchText)
            return;

        searchText = typed;
        clearSearchButton.setEnabled(searchText.isNotEmpty());
        refreshLibraryTracks();
    };

    // Escape clears rather than only unfocusing: it is the key people
    // already press to abandon a search.
    searchBox.onEscapeKey = [this] { searchBox.setText({}, juce::sendNotificationSync); };
    addAndMakeVisible(searchBox);

    clearSearchButton.setEnabled(false);
    clearSearchButton.onClick = [this] { searchBox.setText({}, juce::sendNotificationSync); };
    addAndMakeVisible(clearSearchButton);

    for (auto* button : { &tableViewButton, &folderViewButton })
    {
        button->setClickingTogglesState(false);
        addAndMakeVisible(button);
    }

    tableViewButton.onClick = [this] { setFolderView(false, true); };
    folderViewButton.onClick = [this] { setFolderView(true, true); };

    setFolderView(startInFolderView, false);

    refresh();
}

PlaylistPanel::~PlaylistPanel()
{
    // Models outlive the ListBoxes they're attached to otherwise.
    playlistListBox.setModel(nullptr);
    trackTable->setModel(nullptr);
}

void PlaylistPanel::setPlayingPlaylistId(const juce::Uuid& id)
{
    if (playingId == id)
        return;

    playingId = id;
    playlistListBox.repaint();
}

Playlist* PlaylistPanel::getSelectedPlaylist()
{
    return library.findById(selectedId);
}

void PlaylistPanel::selectPlaylist(int row)
{
    if (auto* playlist = library.getPlaylist(row))
        selectedId = playlist->id;

    updateButtonEnablement();

    // Browsing only - this tells the Playlist window what to display and
    // never touches playback.
    if (onPlaylistSelected)
        onPlaylistSelected(selectedId);
}

void PlaylistPanel::mouseMove(const juce::MouseEvent&) { updateHoveredRow(); }
void PlaylistPanel::mouseExit(const juce::MouseEvent&) { updateHoveredRow(); }

void PlaylistPanel::updateHoveredRow()
{
    // From the current mouse position rather than the event: mouseExit
    // also fires moving between two rows' components, and only where the
    // mouse is now says which row that ended on.
    auto row = -1;

    if (trackTable != nullptr && trackTable->isMouseOver(true))
    {
        auto position = trackTable->getMouseXYRelative();
        row = trackTable->getRowContainingPosition(position.x, position.y);
    }

    if (row != hoveredRow)
    {
        hoveredRow = row;
        trackTable->repaint();
    }
}

void PlaylistPanel::mouseDrag(const juce::MouseEvent& e)
{
    // WHY THIS LIVES HERE RATHER THAN IN THE TABLE. A TableListBox never
    // sees a drag that starts on one of its ROWS: JUCE's own row
    // components handle mouseDrag themselves and start an INTERNAL drag
    // from ListBoxModel::getDragSourceDescription (see
    // RowComponent::mouseDrag in juce_ListBox.cpp), never passing the
    // event up. An override on the table only ever fired on the empty
    // space below the last row, which is why dragging a track to the
    // Playlist window silently did nothing for four releases.
    //
    // Registering as a mouse listener on the table AND its children
    // (addMouseListener(table, true)) does get those row drags.
    //
    // It stays an OS FILE drag rather than JUCE's internal one because
    // the Library and Playlist are separate desktop windows, which
    // JUCE's drag-and-drop doesn't span - and because a file drag means
    // a drop from here and a drop from Explorer arrive by one path.
    if (dragInProgress || e.getDistanceFromDragStart() <= 8)
        return;

    auto* source = e.eventComponent;
    auto fromTable = source != nullptr && trackTable != nullptr
                      && (source == trackTable.get() || trackTable->isParentOf(source));

    if (! fromTable)
        return;

    juce::StringArray paths;
    for (const auto& file : getSelectedLibraryTracks())
        paths.add(file.getFullPathName());

    if (paths.isEmpty())
        return;

    dragInProgress = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles(
        paths, false, trackTable.get(),
        [safeThis = juce::Component::SafePointer<PlaylistPanel>(this)]
        {
            if (safeThis != nullptr)
                safeThis->dragInProgress = false;
        });
}

juce::Array<juce::File> PlaylistPanel::getSelectedLibraryTracks() const
{
    // A selected FOLDER means every track under it, which is what makes
    // "add this album to a playlist" one click.
    if (folderView)
        return folderTree->getSelectedTracks();

    juce::Array<juce::File> selected;

    auto rows = trackTable->getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
    {
        auto row = rows[i];
        if (juce::isPositiveAndBelow(row, libraryTracks.size()))
            selected.add(libraryTracks[row]);
    }

    return selected;
}

int PlaylistPanel::trackRowWidth()
{
    // Once the list is long enough to scroll, rows are narrower than the
    // ListBox by the width of the scrollbar - and the volume bar is drawn
    // relative to the ROW. Hit-testing against the ListBox width instead
    // would put the clickable area a scrollbar's width to the right of
    // the bar you can actually see.
    auto& scrollBar = trackTable->getVerticalScrollBar();
    return trackTable->getWidth() - (scrollBar.isVisible() ? scrollBar.getWidth() : 0);
}

void PlaylistPanel::showTrackVolumeCallout(int row)
{
    if (! juce::isPositiveAndBelow(row, libraryTracks.size()))
        return;

    auto file = libraryTracks[row];

    auto content = std::make_unique<VolumeCallout>(
        file.getFileNameWithoutExtension(),
        trackGains.getGainDb(file),
        TrackSettingsStore::kMinDb,
        TrackSettingsStore::kMaxDb,
        [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this), file](float db)
    {
        if (safeThis == nullptr)
            return;

        trackGains.setGainDb(file, db);

        // Audible straight away if this track happens to be the one
        // playing, rather than only from its next play.
        engine.refreshTrackGains();
        trackTable->repaint();
    });

    content->addFadeControl(trackGains.getFadeSeconds(file),
                             TrackSettingsStore::kMaxFadeSeconds,
                             [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this), file](double seconds)
    {
        if (safeThis == nullptr)
            return;

        // Nothing to poke in the engine: it asks for the fade length at
        // the moment a transition begins, so the next one already uses
        // this. Only the row's readout needs refreshing.
        trackGains.setFadeSeconds(file, seconds);
        trackTable->repaint();
    });

    // Anchored to the row itself, so it is obvious which track is being
    // adjusted when several have been turned down.
    auto rowArea = trackTable->getRowPosition(row, true)
                        .translated(trackTable->getX(), trackTable->getY());

    juce::CallOutBox::launchAsynchronously(std::move(content), rowArea, this);
}

void PlaylistPanel::refresh()
{
    library.loadAll();
    playlistListBox.updateContent();

    // updateContent() is NOT enough on its own. ListBox only repaints a
    // row when its index or its selected state changes (see
    // updateRowAndSelection in juce_ListBox.cpp), so a rename - same row,
    // same selection, different text - left the old name on screen until
    // something else forced a repaint, like clicking a different
    // playlist.
    playlistListBox.repaint();

    // Keep the selection if that playlist still exists, otherwise fall
    // back to the first one so the panel is never left blank.
    if (library.findById(selectedId) == nullptr)
        if (auto* first = library.getPlaylist(0))
            selectedId = first->id;

    selectRowForSelectedId();
    refreshLibraryTracks();
    updateButtonEnablement();
}

bool PlaylistPanel::matchesSearch(const juce::File& file) const
{
    if (searchText.isEmpty())
        return true;

    auto metadata = trackMetadata.get(file);

    // The filename is in there alongside the tags because plenty of
    // libraries have tracks that were never tagged, and searching those
    // by what they're called on disk is the only way to find them.
    auto haystack = (metadata.displayTitle(file) + " " + metadata.artist + " "
                      + metadata.album + " " + metadata.genre + " "
                      + file.getFileNameWithoutExtension());

    // The rule itself lives in TrackSearch.h so the self-test can check
    // it without a window.
    return inkwyrd::matchesSearchTerms(haystack, searchText);
}

void PlaylistPanel::refreshLibraryTracks()
{
    libraryTracks = trackLibrary.getAllTracks();
    unfilteredTrackCount = libraryTracks.size();

    if (searchText.isNotEmpty())
        for (int i = libraryTracks.size(); --i >= 0;)
            if (! matchesSearch(libraryTracks[i]))
                libraryTracks.remove(i);

    updateTrackCaption();
    sortLibraryTracks();

    // The tree does its own grouping and ordering from the same set - it
    // is about where tracks live, not how the table happens to be sorted.
    // Revealed while searching: the matches are the point of the list,
    // and leaving them inside closed folders makes the tree answer
    // "where is this" with more clicking.
    folderTree->setTracks(libraryTracks, searchText.isNotEmpty());
}

void PlaylistPanel::sortLibraryTracks()
{
    using Column = LibraryTrackTableModel::ColumnId;

    // Remember the SELECTION rather than the row numbers - re-sorting
    // moves every row, so restoring indices would leave a different set
    // of tracks selected than the one the user picked.
    auto selected = getSelectedLibraryTracks();

    auto keyFor = [this](const juce::File& file)
    {
        auto metadata = trackMetadata.get(file);

        switch (sortColumnId)
        {
            case Column::artist: return metadata.sortKeyFor(metadata.artist);
            case Column::album:  return metadata.sortKeyFor(metadata.album);
            case Column::genre:  return metadata.sortKeyFor(metadata.genre);
            default:             return metadata.displayTitle(file).toLowerCase();
        }
    };

    // Within an album, track number order is the only sensible
    // tie-break - alphabetical by title scrambles a record.
    auto secondaryFor = [this](const juce::File& file)
    {
        auto metadata = trackMetadata.get(file);
        return sortColumnId == Column::album ? metadata.trackNumber : 0;
    };

    std::stable_sort(libraryTracks.begin(), libraryTracks.end(),
                      [&](const juce::File& a, const juce::File& b)
    {
        auto keyA = keyFor(a), keyB = keyFor(b);

        if (keyA != keyB)
            return sortForwards ? keyA < keyB : keyB < keyA;

        auto secondA = secondaryFor(a), secondB = secondaryFor(b);
        if (secondA != secondB)
            return secondA < secondB;

        // A total order, so equal keys don't shuffle between re-sorts.
        return a.getFullPathName().toLowerCase() < b.getFullPathName().toLowerCase();
    });

    trackTable->updateContent();

    // Re-fit the columns - see the matching comment in
    // PlaylistTrackListComponent::refresh(). Hits here when a library
    // short enough not to scroll has tracks added and starts to.
    trackTable->getHeader().resizeAllColumnsToFit(trackTable->getVisibleContentWidth());
    trackTable->setMinimumContentWidth(trackTable->getHeader().getTotalWidth());

    trackTable->deselectAllRows();
    for (int row = 0; row < libraryTracks.size(); ++row)
        if (selected.contains(libraryTracks[row]))
            trackTable->selectRow(row, true, false);

    trackTable->repaint();
}

void PlaylistPanel::showTracksContextMenu(const juce::Array<juce::File>& tracks)
{
    if (tracks.isEmpty())
        return;

    // The same menu from both views. Volume and fade stays on the table's
    // Vol column, which already is that control.
    enum MenuId { editTagsItem = 1, addToPlaylistItem, removeItem };

    juce::PopupMenu menu;
    menu.addItem(editTagsItem,
                  tracks.size() == 1 ? "Edit tags..."
                                     : "Edit tags for " + juce::String(tracks.size()) + " tracks...",
                  onEditTags != nullptr);
    menu.addSeparator();
    menu.addItem(addToPlaylistItem,
                  tracks.size() == 1 ? "Add to playlist"
                                     : "Add " + juce::String(tracks.size()) + " to playlist",
                  getSelectedPlaylist() != nullptr);

    menu.addSeparator();
    menu.addItem(removeItem, tracks.size() == 1 ? "Remove from library"
                                                : "Remove " + juce::String(tracks.size()) + " from library");

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
                        [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this),
                         tracks](int result)
    {
        if (safeThis == nullptr)
            return;

        switch (result)
        {
            case editTagsItem:
                if (onEditTags)
                    onEditTags(tracks);
                break;

            case addToPlaylistItem:
                addSelectedTracksToPlaylist();
                break;

            case removeItem:
                removeSelectedTracksFromLibrary();
                break;

            default:
                break;
        }
    });
}

void PlaylistPanel::togglePreviewFor(const juce::File& file)
{
    if (file == previewFile)
    {
        if (onStopPreview)
            onStopPreview();

        return;
    }

    if (onPreviewTrack)
        onPreviewTrack(file);
}

void PlaylistPanel::timerCallback()
{
    // A slow cycle - it should read as "this is playing", not blink for
    // attention.
    previewPulse += 0.06f;
    if (previewPulse > 1.0f)
        previewPulse -= 1.0f;

    trackTable->repaint();
    folderTree->setPreview(previewFile, previewPulse);
}

void PlaylistPanel::setPreviewFile(const juce::File& file)
{
    previewFile = file;

    if (file != juce::File())
        startTimerHz(20);
    else
        stopTimer();

    previewPulse = 0.0f;
    folderTree->setPreview(previewFile, previewPulse);
    updateTrackCaption();
    updateButtonEnablement();
    trackTable->repaint();
}

void PlaylistPanel::updateTrackCaption()
{
    // "12 of 84" while filtering, so a short list reads as a search
    // result rather than as a library that lost most of its music.
    auto caption = "All Tracks (" + juce::String(libraryTracks.size())
                    + (searchText.isNotEmpty() ? " of " + juce::String(unfilteredTrackCount) : juce::String())
                    + ")";

    // On the caption rather than a label of its own: previewing is a
    // passing state, and a permanently empty line would cost the list
    // height it needs more.
    if (previewFile != juce::File())
        caption += "   -   previewing " + trackMetadata.get(previewFile).displayTitle(previewFile);

    trackCaption.setText(caption, juce::dontSendNotification);
}

void PlaylistPanel::setFolderView(bool shouldShowFolders, bool notify)
{
    folderView = shouldShowFolders;

    trackTable->setVisible(! folderView);
    folderTree->setVisible(folderView);

    // Which view you're in is shown by which button is lit, so the pair
    // reads as one switch rather than two buttons that do nothing
    // visible.
    tableViewButton.setToggleState(! folderView, juce::dontSendNotification);
    folderViewButton.setToggleState(folderView, juce::dontSendNotification);

    // The two views have their own selections, and the buttons act on
    // whichever view is showing.
    updateButtonEnablement();

    if (notify && onTrackViewChanged != nullptr)
        onTrackViewChanged(folderView);
}

void PlaylistPanel::repaintTrackList()
{
    // Tags arriving can change what the list is ordered BY, not just what
    // the rows say - so this re-sorts rather than only repainting. The
    // tree's order is the folders' own, so it only needs repainting.
    sortLibraryTracks();
    folderTree->refreshLabels();
}

void PlaylistPanel::selectRowForSelectedId()
{
    for (int i = 0; i < library.getNumPlaylists(); ++i)
        if (library.getPlaylist(i)->id == selectedId)
            playlistListBox.selectRow(i, true, true);
}

void PlaylistPanel::notifyEdited(const juce::Uuid& id)
{
    if (onPlaylistEdited && ! id.isNull())
        onPlaylistEdited(id);
}

void PlaylistPanel::updateButtonEnablement()
{
    auto hasPlaylist = getSelectedPlaylist() != nullptr;
    playButton.setEnabled(hasPlaylist);
    renameButton.setEnabled(hasPlaylist);
    deleteButton.setEnabled(hasPlaylist);

    auto hasTracks = ! getSelectedLibraryTracks().isEmpty();
    addToPlaylistButton.setEnabled(hasTracks && hasPlaylist);
    removeFromLibraryButton.setEnabled(hasTracks);
}

void PlaylistPanel::activateSelected()
{
    if (auto* playlist = getSelectedPlaylist())
        if (onActivatePlaylist)
            onActivatePlaylist(playlist->id);
}

void PlaylistPanel::createNewPlaylist()
{
    auto& created = library.createPlaylist("New playlist");
    selectedId = created.id;
    refresh();

    if (onPlaylistSelected)
        onPlaylistSelected(selectedId);

    // Straight into naming it. A playlist called "New playlist" that you
    // then have to find Rename for is two steps where one will do.
    renameSelected();
}

void PlaylistPanel::addFilesToLibrary()
{
    activeChooser = std::make_unique<juce::FileChooser>(
        "Add music to your library",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    activeChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectMultipleItems,
                                [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this)]
                                (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        juce::Array<juce::File> playable;
        for (const auto& file : chooser.getResults())
            if (library.isPlayableFile(file))
                playable.add(file);

        if (playable.isEmpty())
            return;

        trackLibrary.registerTracks(playable);
        refreshLibraryTracks();
    });
}

void PlaylistPanel::addFolderToLibrary()
{
    activeChooser = std::make_unique<juce::FileChooser>(
        "Add a folder of music to your library",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    activeChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectDirectories,
                                [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this)]
                                (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        auto folder = chooser.getResult();
        if (! folder.isDirectory())
            return;

        // Recursive, and through the library's own scanner so this can
        // never disagree with the engine about what counts as playable.
        auto found = library.scanFolder(folder, true);

        if (found.isEmpty())
        {
            inkwyrd::showMessage(safeThis, juce::MessageBoxIconType::InfoIcon,
                                  "Nothing to add",
                                  "No playable audio files were found in that folder.");
            return;
        }

        auto added = trackLibrary.registerTracks(found);
        refreshLibraryTracks();

        if (added == 0)
            inkwyrd::showMessage(safeThis, juce::MessageBoxIconType::InfoIcon,
                                  "Already in your library",
                                  "Every playable file in that folder was already here.");
    });
}

void PlaylistPanel::addSelectedTracksToPlaylist()
{
    auto* playlist = getSelectedPlaylist();
    auto tracks = getSelectedLibraryTracks();

    if (playlist == nullptr || tracks.isEmpty())
        return;

    auto id = playlist->id;
    library.addFiles(id, tracks);
    notifyEdited(id);

    // The Playlist window is showing this list, so it needs re-reading.
    if (onPlaylistSelected)
        onPlaylistSelected(id);
}

void PlaylistPanel::removeSelectedTracksFromLibrary()
{
    auto tracks = getSelectedLibraryTracks();
    if (tracks.isEmpty())
        return;

    auto message = tracks.size() == 1
                        ? "Remove \"" + tracks[0].getFileNameWithoutExtension() + "\" from your library?"
                        : "Remove " + juce::String(tracks.size()) + " tracks from your library?";

    auto options = inkwyrd::dialogOptions(this, juce::MessageBoxIconType::QuestionIcon,
                                           "Remove from library",
                                           message + "\n\n"
                                           "The files themselves aren't touched, and any playlist already "
                                           "using them keeps them - this only takes them off this list.")
                        .withButton("Remove")
                        .withButton("Cancel");

    juce::AlertWindow::showAsync(options,
                                  [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this), tracks]
                                  (int result)
    {
        if (result != 1 || safeThis == nullptr)
            return;

        for (const auto& file : tracks)
            trackLibrary.removeTrack(file);

        refreshLibraryTracks();
        updateButtonEnablement();
    });
}

void PlaylistPanel::renameSelected()
{
    auto* playlist = getSelectedPlaylist();
    if (playlist == nullptr)
        return;

    auto id = playlist->id;
    auto* window = new juce::AlertWindow("Rename playlist", "New name:",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", playlist->name);
    window->addButton("Rename", 1);
    window->addButton("Cancel", 0);

    if (auto* editor = window->getTextEditor("name"))
    {
        // Enter commits, which is what everyone expects of a one-field
        // dialog, and the existing name starts selected so typing simply
        // replaces it.
        editor->onReturnKey = [window] { window->exitModalState(1); };
        editor->selectAll();
    }

    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this), id, window](int result)
    {
        std::unique_ptr<juce::AlertWindow> owned(window); // deleted however we leave here
        if (result != 1 || safeThis == nullptr)
            return;

        auto newName = owned->getTextEditorContents("name");
        if (! library.renamePlaylist(id, newName))
        {
            inkwyrd::showMessage(safeThis, juce::MessageBoxIconType::WarningIcon,
                                  "Couldn't rename",
                                  "That name is either empty or already used by another playlist.");
            return;
        }

        refresh();

        if (onPlaylistSelected)
            onPlaylistSelected(selectedId);
    }));
}

void PlaylistPanel::deleteSelected()
{
    auto* playlist = getSelectedPlaylist();
    if (playlist == nullptr)
        return;

    auto id = playlist->id;
    auto options = inkwyrd::dialogOptions(this, juce::MessageBoxIconType::QuestionIcon,
                                           "Delete playlist",
                                           "Delete \"" + playlist->name + "\"?\n\n"
                                            "Only the playlist is removed - none of your audio files are "
                                            "touched, they stay in your library, and the playlist file goes "
                                            "to the Recycle Bin.")
                        .withButton("Delete")
                        .withButton("Cancel");

    juce::AlertWindow::showAsync(options,
                                  [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this), id]
                                  (int result)
    {
        if (result != 1 || safeThis == nullptr)
            return;

        library.deletePlaylist(id);
        selectedId = juce::Uuid();
        refresh();

        // Tell the app: if this was the playlist being PLAYED, it has to
        // stop the audio. Nothing else would - the engine holds its own
        // copy of the track list and would happily play a deleted
        // playlist forever.
        notifyEdited(id);

        if (onPlaylistSelected)
            onPlaylistSelected(selectedId);
    });
}

//==============================================================================
// Drag and drop from Windows Explorer - straight into the library.
bool PlaylistPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        juce::File file(path);
        if (file.isDirectory() || library.isPlayableFile(file))
            return true;
    }

    return false;
}

void PlaylistPanel::fileDragEnter(const juce::StringArray&, int, int)
{
    dragActive = true;
    repaint();
}

void PlaylistPanel::fileDragExit(const juce::StringArray&)
{
    dragActive = false;
    repaint();
}

void PlaylistPanel::filesDropped(const juce::StringArray& files, int, int)
{
    dragActive = false;
    repaint();

    juce::Array<juce::File> toAdd;

    for (const auto& path : files)
    {
        juce::File file(path);

        if (file.isDirectory())
            toAdd.addArray(library.scanFolder(file, true));
        else if (library.isPlayableFile(file))
            toAdd.add(file);
    }

    if (toAdd.isEmpty())
        return;

    trackLibrary.registerTracks(toAdd);
    refreshLibraryTracks();
}

void PlaylistPanel::paintOverChildren(juce::Graphics& g)
{
    if (! dragActive)
        return;

    g.setColour(inkwyrd::theme::accent.withAlpha(0.7f));
    g.drawRect(getLocalBounds(), 2);
}

void PlaylistPanel::resized()
{
    auto area = getLocalBounds();

    playlistCaption.setBounds(area.removeFromTop(kCaptionHeight));
    playlistListBox.setBounds(area.removeFromTop(140));
    area.removeFromTop(6);

    auto layoutButtonRow = [&](std::initializer_list<juce::TextButton*> row)
    {
        auto bounds = area.removeFromTop(kButtonHeight);
        auto width = (bounds.getWidth() - kButtonGap * ((int) row.size() - 1)) / (int) row.size();
        for (auto* button : row)
        {
            button->setBounds(bounds.removeFromLeft(width));
            bounds.removeFromLeft(kButtonGap);
        }
        area.removeFromTop(kButtonGap);
    };

    layoutButtonRow({ &newButton, &playButton, &renameButton });
    layoutButtonRow({ &deleteButton, &refreshButton });

    area.removeFromTop(8);

    // The view switch rides on the caption row: it belongs to this list,
    // and a row of its own would cost height the list needs more.
    auto captionRow = area.removeFromTop(kCaptionHeight);
    folderViewButton.setBounds(captionRow.removeFromRight(62));
    tableViewButton.setBounds(captionRow.removeFromRight(62));
    trackCaption.setBounds(captionRow);

    // The master list's own buttons sit directly under it, so they read
    // as belonging to that list rather than to the playlists above.
    auto bottom = area.removeFromBottom(kButtonHeight * 2 + kButtonGap);

    // Directly above both views, because it filters whichever one is
    // showing.
    auto searchRow = area.removeFromTop(kButtonHeight);
    clearSearchButton.setBounds(searchRow.removeFromRight(kButtonHeight));
    searchRow.removeFromRight(kButtonGap);
    searchBox.setBounds(searchRow);
    area.removeFromTop(kButtonGap);

    // Both views get the same bounds; only one is visible at a time.
    trackTable->setBounds(area);
    folderTree->setBounds(area);

    auto rowOne = bottom.removeFromTop(kButtonHeight);
    bottom.removeFromTop(kButtonGap);

    auto halfOne = (rowOne.getWidth() - kButtonGap) / 2;
    addFilesButton.setBounds(rowOne.removeFromLeft(halfOne));
    rowOne.removeFromLeft(kButtonGap);
    addFolderButton.setBounds(rowOne);

    auto halfTwo = (bottom.getWidth() - kButtonGap) / 2;
    addToPlaylistButton.setBounds(bottom.removeFromLeft(halfTwo));
    bottom.removeFromLeft(kButtonGap);
    removeFromLibraryButton.setBounds(bottom);
}
