#include "PlaylistPanel.h"

#include "Dialogs.h"
#include "InkwyrdTheme.h"
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
// A ListBox that can be dragged OUT of, onto the Playlist window.
//
// This has to be a real OS-level file drag rather than JUCE's own
// DragAndDropContainer: a container only covers its own component
// hierarchy, and the Library and Playlist windows are separate desktop
// windows. Sending the selection as a file drag also means the Playlist
// window handles a drag from here and a drag from Explorer through
// exactly the same path, rather than having two ways in that could
// behave differently.
class PlaylistPanel::DraggableTrackTable : public juce::TableListBox
{
public:
    std::function<juce::StringArray()> getFilesToDrag;

    void mouseDrag(const juce::MouseEvent& e) override
    {
        // The distance check keeps an ordinary click - including a click
        // on the volume bar - from being swallowed as a drag.
        if (! dragInProgress && e.getDistanceFromDragStart() > 8 && getFilesToDrag != nullptr)
        {
            auto files = getFilesToDrag();

            if (! files.isEmpty())
            {
                dragInProgress = true;

                juce::DragAndDropContainer::performExternalDragDropOfFiles(
                    files, false, this,
                    [safeThis = juce::Component::SafePointer<DraggableTrackTable>(this)]
                    {
                        if (safeThis != nullptr)
                            safeThis->dragInProgress = false;
                    });

                return;
            }
        }

        juce::TableListBox::mouseDrag(e);
    }

private:
    bool dragInProgress = false;
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
        if (columnId == volume || event.mods.isPopupMenu())
            owner.showTrackVolumeCallout(row);
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
                              std::function<void(const juce::Uuid&)> onPlaylistSelectedToUse)
    : library(libraryToUse),
      trackLibrary(trackLibraryToUse),
      engine(engineToUse),
      trackGains(trackGainsToUse),
      trackMetadata(trackMetadataToUse),
      onActivatePlaylist(std::move(onActivatePlaylistToUse)),
      onPlaylistEdited(std::move(onPlaylistEditedToUse)),
      onPlaylistSelected(std::move(onPlaylistSelectedToUse))
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

    trackTable->getFilesToDrag = [this]
    {
        juce::StringArray paths;
        for (const auto& file : getSelectedLibraryTracks())
            paths.add(file.getFullPathName());
        return paths;
    };
    addAndMakeVisible(trackTable.get());

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

juce::Array<juce::File> PlaylistPanel::getSelectedLibraryTracks() const
{
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

void PlaylistPanel::refreshLibraryTracks()
{
    libraryTracks = trackLibrary.getAllTracks();
    trackCaption.setText("All Tracks (" + juce::String(libraryTracks.size()) + ")",
                          juce::dontSendNotification);
    sortLibraryTracks();
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

void PlaylistPanel::repaintTrackList()
{
    // Tags arriving can change what the list is ordered BY, not just what
    // the rows say - so this re-sorts rather than only repainting.
    sortLibraryTracks();
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
    trackCaption.setBounds(area.removeFromTop(kCaptionHeight));

    // The master list's own buttons sit directly under it, so they read
    // as belonging to that list rather than to the playlists above.
    auto bottom = area.removeFromBottom(kButtonHeight * 2 + kButtonGap);
    trackTable->setBounds(area);

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
