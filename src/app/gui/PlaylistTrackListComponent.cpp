#include "PlaylistTrackListComponent.h"

#include "Dialogs.h"
#include "InkwyrdTheme.h"

namespace
{
    constexpr int kRowHeight = 24;
    constexpr int kCaptionHeight = 24;
    constexpr int kButtonHeight = 26;
}

class PlaylistTrackListComponent::Model : public juce::TableListBoxModel
{
public:
    enum ColumnId { title = 1, artist, length };

    explicit Model(PlaylistTrackListComponent& ownerToUse) : owner(ownerToUse) {}

    int getNumRows() override { return owner.resolvedTracks.files.size(); }

    void paintRowBackground(juce::Graphics& g, int row, int, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()))
            return;

        if (selected)
            g.fillAll(inkwyrd::theme::accentSoft.withAlpha(0.35f));

        // The now-playing row gets a bar down its left edge as well as
        // brighter text - the mockup's own way of marking it, and it
        // survives being both playing AND selected, where two shades of
        // green would not. On the row background rather than in a cell,
        // so it spans the row whatever the columns are doing.
        if (isPlaying(row))
        {
            g.setColour(inkwyrd::theme::accent);
            g.fillRect(0, 0, 3, height);
        }
    }

    void paintCell(juce::Graphics& g, int row, int columnId, int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()))
            return;

        auto file = owner.resolvedTracks.files[row];
        auto metadata = owner.trackMetadata.get(file);
        auto playing = isPlaying(row);

        juce::String text;

        if (columnId == title)
            text = (playing ? juce::String::fromUTF8("\xe2\x96\xb6 ") : juce::String("   "))
                    + metadata.displayTitle(file);
        else if (columnId == artist)
            text = metadata.artist;
        else if (columnId == length)
            text = metadata.displayLength();

        g.setColour(playing ? inkwyrd::theme::accent : inkwyrd::theme::text);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(text, juce::Rectangle<int>(6, 0, width - 12, height),
                    columnId == length ? juce::Justification::centredRight
                                       : juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int) override { owner.updateButtons(); }

    void cellClicked(int row, int, const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
            owner.showContextMenuForRow(row);
    }

    void cellDoubleClicked(int row, int, const juce::MouseEvent&) override
    {
        if (juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()) && owner.onPlayTrack)
            owner.onPlayTrack(owner.shownId, owner.resolvedTracks.files[row]);
    }

    void deleteKeyPressed(int) override { owner.removeSelectedTracks(); }

private:
    bool isPlaying(int row) const
    {
        return owner.resolvedTracks.files[row] == owner.engine.getCurrentTrackFile();
    }

    PlaylistTrackListComponent& owner;
};

PlaylistTrackListComponent::PlaylistTrackListComponent(PlaylistLibrary& libraryToUse,
                                                        TrackMetadataStore& trackMetadataToUse,
                                                        PlaylistEngine& engineToUse,
                                                        std::function<void(const juce::Uuid&)> onPlaylistEditedToUse,
                                                        std::function<void(const juce::Uuid&, const juce::File&)> onPlayTrackToUse,
                                                        std::function<void(const juce::File&)> onPreviewTrackToUse,
                                                        std::function<void(const juce::Array<juce::File>&)> onEditTagsToUse)
    : library(libraryToUse),
      trackMetadata(trackMetadataToUse),
      engine(engineToUse),
      onPlaylistEdited(std::move(onPlaylistEditedToUse)),
      onPlayTrack(std::move(onPlayTrackToUse)),
      onPreviewTrack(std::move(onPreviewTrackToUse)),
      onEditTags(std::move(onEditTagsToUse))
{
    captionLabel.setText("No playlist selected", juce::dontSendNotification);
    captionLabel.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    addAndMakeVisible(captionLabel);

    model = std::make_unique<Model>(*this);
    trackTable.setModel(model.get());
    trackTable.setRowHeight(kRowHeight);

    // visible | resizable only: no sortable flag (see the header comment -
    // the playlist's order IS the play order) and no dragging columns
    // about, which is noise on a two-column table.
    constexpr int kColumnFlags = juce::TableHeaderComponent::visible
                                  | juce::TableHeaderComponent::resizable;

    // Ctrl+click picks tracks one by one, Shift+click picks everything
    // between, Ctrl+A picks the lot - all JUCE's own, once this is on.
    // Click-and-drag is added below, in mouseDrag.
    trackTable.setMultipleSelectionEnabled(true);
    trackTable.addMouseListener(this, true);

    // Before the table's own handling, which would move the SELECTION on
    // Up/Down instead of the tracks.
    trackTable.addKeyListener(this);

    auto& header = trackTable.getHeader();
    header.addColumn("Title",  Model::title,  240, 120, -1, kColumnFlags);
    header.addColumn("Artist", Model::artist, 140, 70,  -1, kColumnFlags);
    header.addColumn("Length", Model::length, 62,  56,  80, kColumnFlags);
    header.setStretchToFitActive(true);

    addAndMakeVisible(trackTable);

    removeButton.onClick = [this] { removeSelectedTracks(); };
    addAndMakeVisible(removeButton);
    updateButtons();

    startTimer(500); // just to keep the playing marker current
}

PlaylistTrackListComponent::~PlaylistTrackListComponent()
{
    // The model outlives the table otherwise.
    trackTable.setModel(nullptr);
}

void PlaylistTrackListComponent::setPlaylist(const juce::Uuid& id)
{
    shownId = id;
    refresh();
}

void PlaylistTrackListComponent::refresh()
{
    auto* playlist = library.findById(shownId);

    if (playlist == nullptr)
    {
        resolvedTracks = {};
        captionLabel.setText("No playlist selected", juce::dontSendNotification);
    }
    else
    {
        resolvedTracks = library.resolve(*playlist);
        captionLabel.setText(playlist->name + "  (" + juce::String(resolvedTracks.files.size()) + ")",
                              juce::dontSendNotification);
    }

    trackTable.updateContent();

    // TableListBox only fits its columns to the width in resized(). A
    // playlist long enough to scroll brings in the vertical scrollbar
    // AFTER that, narrowing the space - and the columns, still sized for
    // the wider area, spilled under it with a horizontal scrollbar. So
    // re-fit whenever the rows change.
    trackTable.getHeader().resizeAllColumnsToFit(trackTable.getVisibleContentWidth());
    trackTable.setMinimumContentWidth(trackTable.getHeader().getTotalWidth());

    trackTable.repaint();
    updateButtons();
}

void PlaylistTrackListComponent::updateButtons()
{
    auto count = trackTable.getNumSelectedRows();
    removeButton.setEnabled(count > 0 && library.findById(shownId) != nullptr);

    // Says how many, so a Ctrl+A that caught more than meant is visible
    // before it's acted on.
    removeButton.setButtonText(count > 1 ? "Remove " + juce::String(count) + " from playlist"
                                         : juce::String("Remove from playlist"));
}

juce::Array<juce::File> PlaylistTrackListComponent::getSelectedFiles() const
{
    juce::Array<juce::File> files;

    for (int i = 0; i < trackTable.getNumSelectedRows(); ++i)
    {
        auto row = trackTable.getSelectedRow(i);
        if (juce::isPositiveAndBelow(row, resolvedTracks.files.size()))
            files.add(resolvedTracks.files[row]);
    }

    return files;
}

bool PlaylistTrackListComponent::isFromTable(const juce::MouseEvent& e) const
{
    return e.eventComponent == &trackTable || trackTable.isParentOf(e.eventComponent);
}

int PlaylistTrackListComponent::rowAt(const juce::MouseEvent& e)
{
    auto position = e.getEventRelativeTo(&trackTable).getPosition();

    // Past the last row, but still in the list: the last row. That's
    // where a drag ends up when someone pulls it down past the end of a
    // short playlist, and it should still reach the bottom track.
    auto row = trackTable.getRowContainingPosition(position.x, position.y);
    if (row < 0 && position.y > 0 && resolvedTracks.files.size() > 0
        && trackTable.getLocalBounds().contains(position.withY(0)))
        row = resolvedTracks.files.size() - 1;

    return row;
}

void PlaylistTrackListComponent::mouseDown(const juce::MouseEvent& e)
{
    dragSelecting = false;
    dragMoving = false;
    dragSelectAnchorRow = -1;
    dropIndicatorRow = -1;

    // A plain left press only. Ctrl and Shift already mean something to
    // the list, and a right press opens the menu.
    if (! isFromTable(e) || e.mods.isPopupMenu() || e.mods.isCommandDown() || e.mods.isShiftDown())
        return;

    dragSelectAnchorRow = rowAt(e);

    // Pressing a row that is already selected means "pick these up":
    // dragging from here moves them. Anywhere else starts a new
    // selection. Same rule Explorer uses, and it lets both live on the
    // left button.
    dragMoving = dragSelectAnchorRow >= 0 && trackTable.isRowSelected(dragSelectAnchorRow);
}

void PlaylistTrackListComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragSelectAnchorRow < 0 || ! isFromTable(e))
        return;

    // A few pixels first, so a slightly shaky click is still a click.
    if (e.getDistanceFromDragStart() < 4)
        return;

    if (dragMoving)
    {
        // Between rows, not on one: the line shows where the tracks will
        // land, and the nearest boundary is what the mouse means.
        auto inTable = e.getEventRelativeTo(&trackTable).getPosition();
        if (auto* viewport = trackTable.getViewport())
            viewport->autoScroll(inTable.x, inTable.y - trackTable.getHeaderHeight(), 20, 8);

        auto row = rowAt(e);
        auto boundary = row < 0 ? resolvedTracks.files.size() : row;

        // Past the middle of a row means after it.
        if (row >= 0)
        {
            auto rowArea = trackTable.getRowPosition(row, true);
            if (inTable.y > rowArea.getCentreY())
                ++boundary;
        }

        if (boundary != dropIndicatorRow)
        {
            dropIndicatorRow = boundary;
            repaint();
        }

        return;
    }

    dragSelecting = true;

    // Pulling past the top or bottom of the list scrolls it, so a
    // selection can run longer than what's on screen.
    auto inTable = e.getEventRelativeTo(&trackTable).getPosition();
    if (auto* viewport = trackTable.getViewport())
        viewport->autoScroll(inTable.x, inTable.y - trackTable.getHeaderHeight(), 20, 8);

    auto row = rowAt(e);
    if (row < 0 || row == dragSelectLastRow)
        return;

    dragSelectLastRow = row;
    trackTable.selectRangeOfRows(dragSelectAnchorRow, row, true);
    updateButtons();
}

void PlaylistTrackListComponent::mouseUp(const juce::MouseEvent&)
{
    if (dragMoving)
    {
        auto target = dropIndicatorRow;

        dragMoving = false;
        dropIndicatorRow = -1;
        dragSelectAnchorRow = -1;
        repaint();

        if (target >= 0)
            moveSelectedTracksTo(target);

        return;
    }

    // Pressing on a row that was ALREADY selected makes the list select
    // that single row when the button comes up, collapsing the range
    // just dragged out. This listener hears the release after the row
    // does, so putting the range back here wins.
    if (dragSelecting && dragSelectAnchorRow >= 0 && dragSelectLastRow >= 0)
    {
        trackTable.selectRangeOfRows(dragSelectAnchorRow, dragSelectLastRow, true);
        updateButtons();
    }

    dragSelecting = false;
    dragSelectAnchorRow = -1;
    dragSelectLastRow = -1;
}

void PlaylistTrackListComponent::showContextMenuForRow(int row)
{
    if (! juce::isPositiveAndBelow(row, resolvedTracks.files.size()))
        return;

    // Right-clicking a row that isn't selected acts on THAT row, like
    // every file manager - otherwise the menu would quietly apply to
    // whatever was selected before.
    if (! trackTable.isRowSelected(row))
        trackTable.selectRow(row);

    // Whatever is selected - which, after the check above, includes the
    // row that was right-clicked.
    auto files = getSelectedFiles();
    auto count = files.size();

    // No Preview here: previewing lives in the Library window, on the
    // row itself, so there is one obvious way to start one and to stop it.
    enum MenuId { editTagsItem = 1, removeItem };

    juce::PopupMenu menu;
    menu.addItem(editTagsItem, count > 1 ? "Edit tags for " + juce::String(count) + " tracks..."
                                         : juce::String("Edit tags..."),
                  onEditTags != nullptr);
    menu.addSeparator();
    menu.addItem(removeItem, count > 1 ? "Remove " + juce::String(count) + " from playlist"
                                       : juce::String("Remove from playlist"),
                  library.findById(shownId) != nullptr);

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
                        [this, safeThis = juce::Component::SafePointer<PlaylistTrackListComponent>(this),
                         files](int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == editTagsItem && onEditTags)
            onEditTags(files);
        else if (result == removeItem)
            removeSelectedTracks();
    });
}

juce::Array<int> PlaylistTrackListComponent::selectedEntryIndices(bool& anyFromLinkedFolder) const
{
    anyFromLinkedFolder = false;
    juce::Array<int> indices;

    auto* playlist = library.findById(shownId);
    if (playlist == nullptr)
        return indices;

    for (int i = 0; i < trackTable.getNumSelectedRows(); ++i)
    {
        auto row = trackTable.getSelectedRow(i);
        if (! juce::isPositiveAndBelow(row, resolvedTracks.sourceEntryIndex.size()))
            continue;

        auto entryIndex = resolvedTracks.sourceEntryIndex[row];
        if (! juce::isPositiveAndBelow(entryIndex, playlist->entries.size()))
            continue;

        if (playlist->entries[entryIndex].kind == PlaylistEntry::Kind::folder)
            anyFromLinkedFolder = true;
        else
            indices.addIfNotAlreadyThere(entryIndex);
    }

    return indices;
}

void PlaylistTrackListComponent::explainLinkedFolderOrder()
{
    inkwyrd::showMessage(this, juce::MessageBoxIconType::InfoIcon,
                          "Those tracks come from a linked folder",
                          "They're in this playlist because the folder is, so they play in the "
                          "folder's own order and can't be moved one at a time.\n\n"
                          "Add tracks individually if you want to arrange them yourself.");
}

void PlaylistTrackListComponent::reselectFiles(const juce::Array<juce::File>& files)
{
    trackTable.deselectAllRows();

    auto first = -1;
    for (int row = 0; row < resolvedTracks.files.size(); ++row)
    {
        if (! files.contains(resolvedTracks.files[row]))
            continue;

        trackTable.selectRow(row, true, first < 0);
        if (first < 0)
            first = row;
    }

    // Keeps a track in view as it is walked up or down the list.
    if (first >= 0)
        trackTable.scrollToEnsureRowIsOnscreen(first);

    updateButtons();
}

void PlaylistTrackListComponent::moveSelectedTracksBy(int delta)
{
    auto files = getSelectedFiles();
    if (files.isEmpty() || library.findById(shownId) == nullptr)
        return;

    auto anyFromLinkedFolder = false;
    auto indices = selectedEntryIndices(anyFromLinkedFolder);

    if (indices.isEmpty())
    {
        if (anyFromLinkedFolder)
            explainLinkedFolderOrder();

        return;
    }

    // A selection that mixes hand-added tracks with folder ones would
    // move only half of itself, which reads as the list mangling the
    // selection. Say what's going on instead.
    if (anyFromLinkedFolder)
    {
        explainLinkedFolderOrder();
        return;
    }

    if (! library.moveEntriesBy(shownId, indices, delta))
        return; // already at the end it was heading for

    refresh();
    reselectFiles(files);

    if (onPlaylistEdited)
        onPlaylistEdited(shownId);
}

void PlaylistTrackListComponent::moveSelectedTracksTo(int toRow)
{
    auto files = getSelectedFiles();
    if (files.isEmpty() || library.findById(shownId) == nullptr)
        return;

    auto anyFromLinkedFolder = false;
    auto indices = selectedEntryIndices(anyFromLinkedFolder);

    if (indices.isEmpty() || anyFromLinkedFolder)
    {
        if (anyFromLinkedFolder)
            explainLinkedFolderOrder();

        return;
    }

    // The drop is a ROW, and rows become entries: the entry of the row
    // dropped on, or one past the last entry when dropped off the end.
    auto* playlist = library.findById(shownId);
    auto targetEntry = playlist->entries.size();

    if (juce::isPositiveAndBelow(toRow, resolvedTracks.sourceEntryIndex.size()))
        targetEntry = resolvedTracks.sourceEntryIndex[toRow];

    if (! library.moveEntriesTo(shownId, indices, targetEntry))
        return;

    refresh();
    reselectFiles(files);

    if (onPlaylistEdited)
        onPlaylistEdited(shownId);
}

bool PlaylistTrackListComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    // Up and Down move the selected tracks, as asked for. The list's own
    // arrow-key behaviour (moving the SELECTION) is given up for it -
    // selecting is what the mouse is for here, and a playlist is a thing
    // people arrange far more often than they walk through.
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        if (trackTable.getNumSelectedRows() > 0)
        {
            moveSelectedTracksBy(key == juce::KeyPress::upKey ? -1 : 1);
            return true;
        }
    }

    return false;
}

void PlaylistTrackListComponent::removeSelectedTracks()
{
    auto* playlist = library.findById(shownId);
    if (playlist == nullptr || trackTable.getNumSelectedRows() == 0)
        return;

    // Sort the selection into what can go and what can't. A track that
    // came from a LINKED FOLDER has no row of its own to delete - it's
    // there because the folder is. Removing its entry would take every
    // other track from that folder with it, so those stay, and say so.
    juce::Array<int> entriesToRemove;
    juce::Array<juce::File> fromLinkedFolders;
    juce::File linkedFolder;

    for (int i = 0; i < trackTable.getNumSelectedRows(); ++i)
    {
        auto row = trackTable.getSelectedRow(i);
        if (! juce::isPositiveAndBelow(row, resolvedTracks.files.size())
            || ! juce::isPositiveAndBelow(row, resolvedTracks.sourceEntryIndex.size()))
            continue;

        auto entryIndex = resolvedTracks.sourceEntryIndex[row];
        if (! juce::isPositiveAndBelow(entryIndex, playlist->entries.size()))
            continue;

        if (playlist->entries[entryIndex].kind == PlaylistEntry::Kind::folder)
        {
            fromLinkedFolders.add(resolvedTracks.files[row]);
            linkedFolder = playlist->entries[entryIndex].path;
        }
        else
        {
            entriesToRemove.addIfNotAlreadyThere(entryIndex);
        }
    }

    auto explainLinked = [this, playlistName = playlist->name, fromLinkedFolders, linkedFolder,
                          anyRemoved = ! entriesToRemove.isEmpty()]
    {
        if (fromLinkedFolders.isEmpty())
            return;

        auto what = fromLinkedFolders.size() == 1
                        ? "\"" + fromLinkedFolders[0].getFileNameWithoutExtension() + "\" is"
                        : juce::String(fromLinkedFolders.size()) + " of those tracks are";

        inkwyrd::showMessage(this, juce::MessageBoxIconType::InfoIcon,
                              anyRemoved ? "Some tracks were left in" : "Those tracks come from a linked folder",
                              what + " in \"" + playlistName + "\" because the folder\n"
                               + linkedFolder.getFullPathName()
                               + "\nis linked to it, so they can't be removed on their own.\n\n"
                                 "Remove the folder from the playlist to drop all of its tracks, "
                                 "or delete the files themselves.");
    };

    if (entriesToRemove.isEmpty())
    {
        explainLinked();
        return;
    }

    auto doRemove = [this, entriesToRemove, explainLinked]
    {
        library.removeEntries(shownId, entriesToRemove);
        trackTable.deselectAllRows();
        refresh();

        if (onPlaylistEdited)
            onPlaylistEdited(shownId);

        explainLinked();
    };

    // One track goes straight away, as it always has. Several ask first:
    // there's no undo, and Ctrl+A then Delete is an easy way to empty a
    // playlist without meaning to. The files themselves are never touched.
    if (entriesToRemove.size() == 1)
    {
        doRemove();
        return;
    }

    auto options = inkwyrd::dialogOptions(this, juce::MessageBoxIconType::QuestionIcon,
                                           "Remove " + juce::String(entriesToRemove.size()) + " tracks?",
                                           "They'll be taken out of \"" + playlist->name + "\". "
                                           "The files themselves aren't deleted, and stay in your library.")
                        .withButton("Remove")
                        .withButton("Cancel");

    juce::AlertWindow::showAsync(options, [safeThis = juce::Component::SafePointer<PlaylistTrackListComponent>(this),
                                            doRemove](int result)
    {
        if (safeThis != nullptr && result == 1)
            doRemove();
    });
}

bool PlaylistTrackListComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (library.findById(shownId) == nullptr)
        return false; // nothing selected to drop INTO

    for (const auto& path : files)
        if (library.isPlayableFile(juce::File(path)))
            return true;

    return false;
}

void PlaylistTrackListComponent::fileDragEnter(const juce::StringArray&, int, int)
{
    dragActive = true;
    repaint();
}

void PlaylistTrackListComponent::fileDragExit(const juce::StringArray&)
{
    dragActive = false;
    repaint();
}

void PlaylistTrackListComponent::filesDropped(const juce::StringArray& files, int, int)
{
    dragActive = false;
    repaint();

    if (library.findById(shownId) == nullptr)
        return;

    juce::Array<juce::File> playable;
    for (const auto& path : files)
    {
        juce::File file(path);
        if (library.isPlayableFile(file))
            playable.add(file);
    }

    if (playable.isEmpty())
        return;

    library.addFiles(shownId, playable);
    refresh();

    if (onPlaylistEdited)
        onPlaylistEdited(shownId);
}

void PlaylistTrackListComponent::paintOverChildren(juce::Graphics& g)
{
    // Where a dragged selection would land.
    if (dropIndicatorRow >= 0)
    {
        auto rows = resolvedTracks.files.size();
        auto row = juce::jlimit(0, juce::jmax(0, rows - 1), dropIndicatorRow);
        auto rowArea = trackTable.getRowPosition(row, true);
        auto y = dropIndicatorRow >= rows ? rowArea.getBottom() : rowArea.getY();

        auto line = getLocalArea(&trackTable, juce::Rectangle<int>(0, y - 1, trackTable.getWidth(), 2));
        g.setColour(inkwyrd::theme::accent);
        g.fillRect(line);
    }

    if (! dragActive)
        return;

    g.setColour(inkwyrd::theme::accent.withAlpha(0.7f));
    g.drawRect(getLocalBounds(), 2);
}

void PlaylistTrackListComponent::timerCallback()
{
    auto current = engine.getCurrentTrackFile();
    if (current != lastSeenPlayingTrack)
    {
        lastSeenPlayingTrack = current;
        trackTable.repaint(); // only the marker moved, row count is unchanged
    }
}

void PlaylistTrackListComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    captionLabel.setBounds(area.removeFromTop(kCaptionHeight));
    area.removeFromTop(6);

    removeButton.setBounds(area.removeFromBottom(kButtonHeight));
    area.removeFromBottom(6);

    trackTable.setBounds(area);
}
