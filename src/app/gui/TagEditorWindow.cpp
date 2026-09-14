#include "TagEditorWindow.h"

#include "Dialogs.h"
#include "InkwyrdTheme.h"

namespace
{
    constexpr int kRowHeight = 26;
    constexpr int kRowGap = 6;
    constexpr int kLabelWidth = 96;

    // Shown in a field whose value differs across the selection. Typing
    // replaces it; leaving it alone leaves every file's own value.
    const juce::String kKeepPlaceholder = "<keep>";

    juce::String describeSize(juce::int64 bytes)
    {
        return juce::File::descriptionOfSizeInBytes(bytes);
    }
}

//==============================================================================
class TagEditorWindow::Content : public juce::Component
{
public:
    Content(const juce::Array<juce::File>& filesToEdit,
             std::function<juce::String(const juce::Array<juce::File>&)> onPrepareForWriteToUse,
             std::function<void(const juce::Array<juce::File>&)> onSavedToUse)
        : files(filesToEdit),
          onPrepareForWrite(std::move(onPrepareForWriteToUse)),
          onSaved(std::move(onSavedToUse))
    {
        headingLabel.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        headingLabel.setText(files.size() == 1
                                 ? files[0].getFileName()
                                 : juce::String(files.size()) + " tracks selected",
                              juce::dontSendNotification);
        addAndMakeVisible(headingLabel);

        for (auto* field : { &titleField, &artistField, &albumField, &albumArtistField,
                              &yearField, &trackNumberField, &trackTotalField,
                              &discNumberField, &discTotalField, &bpmField,
                              &composerField, &publisherField })
        {
            field->onTextChange = [field, this] { markDirty(field); };
            addAndMakeVisible(field);
        }

        commentField.setMultiLine(true, true);
        commentField.setReturnKeyStartsNewLine(true);
        commentField.onTextChange = [this] { markDirty(&commentField); };
        addAndMakeVisible(commentField);

        genreBox.setEditableText(true);
        genreBox.addItemList(inkwyrd::TagEditor::standardGenres(), 1);
        genreBox.onChange = [this] { genreDirty = true; };
        addAndMakeVisible(genreBox);

        for (auto* caption : { &titleCaption, &artistCaption, &albumCaption, &albumArtistCaption,
                                &yearCaption, &genreCaption, &trackCaption, &discCaption,
                                &bpmCaption, &commentCaption, &composerCaption, &publisherCaption,
                                &artworkCaption, &formatCaption })
            addAndMakeVisible(caption);

        artworkPreview.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(artworkPreview);

        replaceArtworkButton.onClick = [this] { chooseArtwork(); };
        addAndMakeVisible(replaceArtworkButton);

        removeArtworkButton.onClick = [this]
        {
            artworkAction = inkwyrd::TagChanges::ArtworkAction::clear;
            artworkBytes.reset();
            artworkImage = {};
            updateArtworkDisplay();
        };
        addAndMakeVisible(removeArtworkButton);

        formatLabel.setJustificationType(juce::Justification::topLeft);
        formatLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(formatLabel);

        statusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(statusLabel);

        saveButton.onClick = [this] { save(); };
        addAndMakeVisible(saveButton);

        loadFromFiles();
        lookAndFeelChanged();
        setSize(560, 620);
    }

    void lookAndFeelChanged() override
    {
        for (auto* caption : { &titleCaption, &artistCaption, &albumCaption, &albumArtistCaption,
                                &yearCaption, &genreCaption, &trackCaption, &discCaption,
                                &bpmCaption, &commentCaption, &composerCaption, &publisherCaption,
                                &artworkCaption, &formatCaption })
            caption->setColour(juce::Label::textColourId, inkwyrd::theme::textDim);

        formatLabel.setColour(juce::Label::textColourId, inkwyrd::theme::textDim);
        statusLabel.setColour(juce::Label::textColourId, inkwyrd::theme::textDim);
        artworkPreview.setColour(juce::Label::textColourId, inkwyrd::theme::textDim);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(inkwyrd::theme::panel);

        if (artworkImage.isValid())
        {
            auto area = artworkPreview.getBounds().toFloat();
            g.drawImageWithin(artworkImage, (int) area.getX(), (int) area.getY(),
                               (int) area.getWidth(), (int) area.getHeight(),
                               juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);

        headingLabel.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);

        auto right = area.removeFromRight(180);
        area.removeFromRight(12);

        // Artwork and the read-only format panel down the right, the way
        // the fields want the width more than they want company.
        artworkCaption.setBounds(right.removeFromTop(20));
        artworkPreview.setBounds(right.removeFromTop(160));
        right.removeFromTop(6);
        replaceArtworkButton.setBounds(right.removeFromTop(kRowHeight));
        right.removeFromTop(4);
        removeArtworkButton.setBounds(right.removeFromTop(kRowHeight));
        right.removeFromTop(14);
        formatCaption.setBounds(right.removeFromTop(20));
        formatLabel.setBounds(right.removeFromTop(120));

        auto row = [&area](juce::Label& caption, juce::Component& editor)
        {
            auto bounds = area.removeFromTop(kRowHeight);
            caption.setBounds(bounds.removeFromLeft(kLabelWidth));
            editor.setBounds(bounds);
            area.removeFromTop(kRowGap);
        };

        row(titleCaption, titleField);
        row(artistCaption, artistField);
        row(albumCaption, albumField);
        row(albumArtistCaption, albumArtistField);
        row(yearCaption, yearField);
        row(genreCaption, genreBox);

        // Number and total share a row: they are one tag in the file, and
        // "3 of 12" is how anyone thinks of them.
        auto trackRow = area.removeFromTop(kRowHeight);
        trackCaption.setBounds(trackRow.removeFromLeft(kLabelWidth));
        trackNumberField.setBounds(trackRow.removeFromLeft(70));
        trackRow.removeFromLeft(8);
        trackTotalField.setBounds(trackRow.removeFromLeft(70));
        area.removeFromTop(kRowGap);

        auto discRow = area.removeFromTop(kRowHeight);
        discCaption.setBounds(discRow.removeFromLeft(kLabelWidth));
        discNumberField.setBounds(discRow.removeFromLeft(70));
        discRow.removeFromLeft(8);
        discTotalField.setBounds(discRow.removeFromLeft(70));
        area.removeFromTop(kRowGap);

        auto bpmRow = area.removeFromTop(kRowHeight);
        bpmCaption.setBounds(bpmRow.removeFromLeft(kLabelWidth));
        bpmField.setBounds(bpmRow.removeFromLeft(70));
        area.removeFromTop(kRowGap);

        row(composerCaption, composerField);
        row(publisherCaption, publisherField);

        auto commentRow = area.removeFromTop(80);
        commentCaption.setBounds(commentRow.removeFromLeft(kLabelWidth));
        commentField.setBounds(commentRow);

        auto bottom = getLocalBounds().reduced(16).removeFromBottom(kRowHeight);
        saveButton.setBounds(bottom.removeFromRight(120));
        bottom.removeFromRight(12);
        statusLabel.setBounds(bottom);
    }

private:
    void markDirty(juce::TextEditor* field) { dirtyFields.insert(field); }

    // One value across the whole selection, or {} when they differ.
    static juce::String commonValue(const juce::Array<inkwyrd::TagValues>& all,
                                     juce::String inkwyrd::TagValues::* member, bool& differs)
    {
        differs = false;
        if (all.isEmpty())
            return {};

        auto first = all.getReference(0).*member;

        for (int i = 1; i < all.size(); ++i)
            if (all.getReference(i).*member != first)
            {
                differs = true;
                return {};
            }

        return first;
    }

    void fill(juce::TextEditor& field, juce::String inkwyrd::TagValues::* member)
    {
        bool differs = false;
        auto value = commonValue(values, member, differs);

        field.setText(value, juce::dontSendNotification);
        field.setTextToShowWhenEmpty(differs ? kKeepPlaceholder : juce::String(),
                                      inkwyrd::theme::textDim);
    }

    void loadFromFiles()
    {
        values.clear();
        for (const auto& file : files)
            values.add(inkwyrd::TagEditor::read(file));

        fill(titleField, &inkwyrd::TagValues::title);
        fill(artistField, &inkwyrd::TagValues::artist);
        fill(albumField, &inkwyrd::TagValues::album);
        fill(albumArtistField, &inkwyrd::TagValues::albumArtist);
        fill(yearField, &inkwyrd::TagValues::year);
        fill(trackNumberField, &inkwyrd::TagValues::trackNumber);
        fill(trackTotalField, &inkwyrd::TagValues::trackTotal);
        fill(discNumberField, &inkwyrd::TagValues::discNumber);
        fill(discTotalField, &inkwyrd::TagValues::discTotal);
        fill(bpmField, &inkwyrd::TagValues::bpm);
        fill(composerField, &inkwyrd::TagValues::composer);
        fill(publisherField, &inkwyrd::TagValues::publisher);
        fill(commentField, &inkwyrd::TagValues::comment);

        bool genreDiffers = false;
        genreBox.setText(commonValue(values, &inkwyrd::TagValues::genre, genreDiffers),
                          juce::dontSendNotification);
        genreBox.setTextWhenNothingSelected(genreDiffers ? kKeepPlaceholder : juce::String());

        dirtyFields.clear();
        genreDirty = false;
        artworkAction = inkwyrd::TagChanges::ArtworkAction::leave;
        artworkBytes.reset();

        if (! values.isEmpty() && values.getReference(0).artwork.getSize() > 0)
            artworkImage = juce::ImageFileFormat::loadFrom(values.getReference(0).artwork.getData(),
                                                            values.getReference(0).artwork.getSize());
        else
            artworkImage = {};

        updateArtworkDisplay();
        updateFormatPanel();
    }

    void updateArtworkDisplay()
    {
        artworkPreview.setText(artworkImage.isValid() ? juce::String()
                                                       : (files.size() > 1 ? "First track has no artwork"
                                                                           : "No artwork"),
                                juce::dontSendNotification);
        repaint();
    }

    void updateFormatPanel()
    {
        if (files.size() != 1)
        {
            formatLabel.setText(juce::String(files.size()) + " files selected", juce::dontSendNotification);
            return;
        }

        auto info = inkwyrd::TagEditor::readAudioInfo(files[0]);

        if (! info.ok)
        {
            formatLabel.setText("Couldn't read this file's details.", juce::dontSendNotification);
            return;
        }

        juce::StringArray lines;
        lines.add(info.format);
        lines.add(juce::String(info.lengthSeconds / 60) + ":"
                   + juce::String(info.lengthSeconds % 60).paddedLeft('0', 2));
        lines.add(juce::String(info.bitrateKbps) + " kbps");
        lines.add(juce::String(info.sampleRate) + " Hz, "
                   + (info.channels == 1 ? "mono" : info.channels == 2 ? "stereo"
                                                                        : juce::String(info.channels) + " channels"));
        lines.add(describeSize(info.fileSizeBytes));

        formatLabel.setText(lines.joinIntoString("\n"), juce::dontSendNotification);
    }

    void chooseArtwork()
    {
        chooser = std::make_unique<juce::FileChooser>("Choose cover art",
                                                       juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
                                                       "*.jpg;*.jpeg;*.png");

        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, safeThis = juce::Component::SafePointer<Content>(this)]
                              (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr)
                return;

            auto file = fc.getResult();
            if (! file.existsAsFile())
                return;

            juce::MemoryBlock bytes;
            if (! file.loadFileAsData(bytes) || bytes.getSize() == 0)
                return;

            artworkBytes = std::make_unique<juce::MemoryBlock>(std::move(bytes));
            artworkMime = file.hasFileExtension("png") ? "image/png" : "image/jpeg";
            artworkAction = inkwyrd::TagChanges::ArtworkAction::set;
            artworkImage = juce::ImageFileFormat::loadFrom(artworkBytes->getData(), artworkBytes->getSize());

            updateArtworkDisplay();
        });
    }

    inkwyrd::TagField fieldFor(juce::TextEditor& editor)
    {
        // Untouched means LEAVE, which is what makes editing several
        // tracks at once safe: only what you actually typed is written.
        if (dirtyFields.find(&editor) == dirtyFields.end())
            return inkwyrd::TagField::leaveAlone();

        auto text = editor.getText().trim();
        return text.isEmpty() ? inkwyrd::TagField::cleared() : inkwyrd::TagField::setTo(text);
    }

    void save()
    {
        inkwyrd::TagChanges changes;
        changes.title = fieldFor(titleField);
        changes.artist = fieldFor(artistField);
        changes.album = fieldFor(albumField);
        changes.albumArtist = fieldFor(albumArtistField);
        changes.year = fieldFor(yearField);
        changes.trackNumber = fieldFor(trackNumberField);
        changes.trackTotal = fieldFor(trackTotalField);
        changes.discNumber = fieldFor(discNumberField);
        changes.discTotal = fieldFor(discTotalField);
        changes.bpm = fieldFor(bpmField);
        changes.composer = fieldFor(composerField);
        changes.publisher = fieldFor(publisherField);
        changes.comment = fieldFor(commentField);

        if (genreDirty)
        {
            auto genre = genreBox.getText().trim();
            changes.genre = genre.isEmpty() ? inkwyrd::TagField::cleared()
                                             : inkwyrd::TagField::setTo(genre);
        }

        changes.artworkAction = artworkAction;
        if (artworkAction == inkwyrd::TagChanges::ArtworkAction::set && artworkBytes != nullptr)
        {
            changes.artwork = *artworkBytes;
            changes.artworkMimeType = artworkMime;
        }

        if (! changes.anythingToDo())
        {
            statusLabel.setText("Nothing changed.", juce::dontSendNotification);
            return;
        }

        // The app stops a preview of these files and tells us about any
        // that are playing - a file open for reading can't be replaced.
        if (onPrepareForWrite != nullptr)
        {
            auto problem = onPrepareForWrite(files);

            if (problem.isNotEmpty())
            {
                statusLabel.setColour(juce::Label::textColourId, inkwyrd::theme::warning);
                statusLabel.setText(problem, juce::dontSendNotification);
                return;
            }
        }

        juce::StringArray failures;
        juce::Array<juce::File> written;

        for (const auto& file : files)
        {
            juce::String error;
            if (inkwyrd::TagEditor::write(file, changes, error))
                written.add(file);
            else
                failures.add(file.getFileName() + ": " + error);
        }

        if (! written.isEmpty() && onSaved != nullptr)
            onSaved(written);

        statusLabel.setColour(juce::Label::textColourId,
                               failures.isEmpty() ? inkwyrd::theme::textDim : inkwyrd::theme::warning);
        statusLabel.setText(failures.isEmpty()
                                ? (written.size() == 1 ? "Saved." : "Saved " + juce::String(written.size()) + " tracks.")
                                : failures[0],
                             juce::dontSendNotification);

        // Re-read, so the fields show what the files now actually say
        // rather than what was typed at them.
        loadFromFiles();
    }

    juce::Array<juce::File> files;
    juce::Array<inkwyrd::TagValues> values;

    std::function<juce::String(const juce::Array<juce::File>&)> onPrepareForWrite;
    std::function<void(const juce::Array<juce::File>&)> onSaved;

    juce::Label headingLabel;

    juce::Label titleCaption { {}, "Title" }, artistCaption { {}, "Artist" },
                albumCaption { {}, "Album" }, albumArtistCaption { {}, "Album artist" },
                yearCaption { {}, "Year" }, genreCaption { {}, "Genre" },
                trackCaption { {}, "Track / of" }, discCaption { {}, "Disc / of" },
                bpmCaption { {}, "BPM" }, commentCaption { {}, "Comment" },
                composerCaption { {}, "Composer" }, publisherCaption { {}, "Publisher" },
                artworkCaption { {}, "Artwork" }, formatCaption { {}, "Format" };

    juce::TextEditor titleField, artistField, albumField, albumArtistField, yearField,
                     trackNumberField, trackTotalField, discNumberField, discTotalField,
                     bpmField, composerField, publisherField, commentField;
    juce::ComboBox genreBox;

    juce::Label artworkPreview, formatLabel, statusLabel;
    juce::Image artworkImage;
    juce::TextButton replaceArtworkButton { "Replace..." }, removeArtworkButton { "Remove" };
    juce::TextButton saveButton { "Save" };

    std::set<juce::TextEditor*> dirtyFields;
    bool genreDirty = false;

    inkwyrd::TagChanges::ArtworkAction artworkAction = inkwyrd::TagChanges::ArtworkAction::leave;
    std::unique_ptr<juce::MemoryBlock> artworkBytes;
    juce::String artworkMime;

    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
TagEditorWindow::TagEditorWindow(const juce::Array<juce::File>& files,
                                  std::function<juce::String(const juce::Array<juce::File>&)> onPrepareForWrite,
                                  std::function<void(const juce::Array<juce::File>&)> onSaved,
                                  std::function<void(TagEditorWindow*)> onCloseToUse)
    : juce::DocumentWindow("Edit tags",
                            juce::Desktop::getInstance().getDefaultLookAndFeel()
                                .findColour(juce::ResizableWindow::backgroundColourId),
                            juce::DocumentWindow::closeButton),
      onClose(std::move(onCloseToUse))
{
    // Native title bar, like the plugin editor windows: this is opened
    // for a job and closed again, not arranged in the snap layout.
    setUsingNativeTitleBar(true);
    setContentOwned(new Content(files, std::move(onPrepareForWrite), std::move(onSaved)), true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void TagEditorWindow::closeButtonPressed()
{
    if (onClose)
        onClose(this); // the owner deletes us
}
