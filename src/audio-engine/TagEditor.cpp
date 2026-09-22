#include "TagEditor.h"

#include <taglib/fileref.h>
#include <taglib/tfile.h>
#include <taglib/tpropertymap.h>
#include <taglib/tvariant.h>
#include <taglib/audioproperties.h>
#include <taglib/id3v1genres.h>

namespace inkwyrd
{
    namespace
    {
        // TagLib's own names for the fields. One table, so reading and
        // writing can't drift apart.
        constexpr const char* kTitleKey       = "TITLE";
        constexpr const char* kArtistKey      = "ARTIST";
        constexpr const char* kAlbumKey       = "ALBUM";
        constexpr const char* kAlbumArtistKey = "ALBUMARTIST";
        constexpr const char* kGenreKey       = "GENRE";
        constexpr const char* kCommentKey     = "COMMENT";
        constexpr const char* kComposerKey    = "COMPOSER";
        constexpr const char* kPublisherKey   = "LABEL";
        constexpr const char* kYearKey        = "DATE";
        constexpr const char* kTrackKey       = "TRACKNUMBER";
        constexpr const char* kDiscKey        = "DISCNUMBER";
        constexpr const char* kBpmKey         = "BPM";
        constexpr const char* kPictureKey     = "PICTURE";

        juce::String fromTagLib(const TagLib::String& text)
        {
            return juce::String(text.toCWString());
        }

        TagLib::String toTagLib(const juce::String& text)
        {
            return TagLib::String(text.toWideCharPointer());
        }

        TagLib::FileName fileNameFor(const juce::File& file)
        {
            return TagLib::FileName(file.getFullPathName().toWideCharPointer());
        }

        juce::String firstValue(const TagLib::PropertyMap& properties, const char* key)
        {
            auto found = properties.find(key);
            if (found == properties.end() || found->second.isEmpty())
                return {};

            return fromTagLib(found->second.front());
        }

        // "3/12" is one tag carrying both the number and the total, which
        // is how ID3 and Vorbis both do it.
        void splitNumberAndTotal(const juce::String& combined, juce::String& number, juce::String& total)
        {
            auto slash = combined.indexOfChar('/');

            if (slash < 0)
            {
                number = combined.trim();
                total = {};
                return;
            }

            number = combined.substring(0, slash).trim();
            total = combined.substring(slash + 1).trim();
        }

        juce::String joinNumberAndTotal(const juce::String& number, const juce::String& total)
        {
            if (number.isEmpty())
                return {};

            return total.isNotEmpty() ? number + "/" + total : number;
        }

        void applyField(TagLib::PropertyMap& properties, const char* key, const TagField& field)
        {
            if (field.isLeave())
                return;

            properties.erase(key);

            if (field.action == TagField::Action::set && field.value.isNotEmpty())
                properties.insert(key, TagLib::StringList(toTagLib(field.value)));
        }

        // The number/total pair share one tag, so a change to either has
        // to be written together with whatever the other one is now.
        void applyPair(TagLib::PropertyMap& properties, const char* key,
                        const TagField& numberField, const TagField& totalField,
                        const juce::String& currentNumber, const juce::String& currentTotal)
        {
            if (numberField.isLeave() && totalField.isLeave())
                return;

            auto resolve = [](const TagField& field, const juce::String& current)
            {
                if (field.isLeave())
                    return current;

                return field.action == TagField::Action::set ? field.value : juce::String();
            };

            auto combined = joinNumberAndTotal(resolve(numberField, currentNumber).trim(),
                                                resolve(totalField, currentTotal).trim());

            properties.erase(key);

            if (combined.isNotEmpty())
                properties.insert(key, TagLib::StringList(toTagLib(combined)));
        }

        bool readArtwork(TagLib::FileRef& fileRef, juce::MemoryBlock& data, juce::String& mimeType)
        {
            auto pictures = fileRef.complexProperties(kPictureKey);
            if (pictures.isEmpty())
                return false;

            auto picture = pictures.front();

            auto dataEntry = picture.find("data");
            if (dataEntry == picture.end())
                return false;

            auto bytes = dataEntry->second.toByteVector();
            if (bytes.isEmpty())
                return false;

            data.replaceAll(bytes.data(), bytes.size());

            auto mimeEntry = picture.find("mimeType");
            if (mimeEntry != picture.end())
                mimeType = fromTagLib(mimeEntry->second.toString());

            return true;
        }
    }

    bool TagChanges::anythingToDo() const
    {
        for (const auto* field : { &title, &artist, &album, &albumArtist, &genre, &comment,
                                    &composer, &publisher, &year, &trackNumber, &trackTotal,
                                    &discNumber, &discTotal, &bpm })
            if (! field->isLeave())
                return true;

        return artworkAction != ArtworkAction::leave;
    }

    bool TagEditor::canEdit(const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        TagLib::FileRef fileRef(fileNameFor(file), false);
        return ! fileRef.isNull() && fileRef.file() != nullptr;
    }

    TagValues TagEditor::read(const juce::File& file)
    {
        TagValues values;

        if (! file.existsAsFile())
            return values;

        // Audio properties read in the same open as the tags, so the
        // library's Length column costs no second pass over every file.
        // Average, like readAudioInfo: accurate for VBR files that carry
        // a header, and it doesn't decode the audio.
        TagLib::FileRef fileRef(fileNameFor(file), true, TagLib::AudioProperties::Average);
        if (fileRef.isNull() || fileRef.file() == nullptr)
            return values;

        if (auto* audio = fileRef.audioProperties())
            values.lengthMilliseconds = juce::jmax(0, audio->lengthInMilliseconds());

        auto properties = fileRef.file()->properties();

        values.title       = firstValue(properties, kTitleKey);
        values.artist      = firstValue(properties, kArtistKey);
        values.album       = firstValue(properties, kAlbumKey);
        values.albumArtist = firstValue(properties, kAlbumArtistKey);
        values.genre       = firstValue(properties, kGenreKey);
        values.comment     = firstValue(properties, kCommentKey);
        values.composer    = firstValue(properties, kComposerKey);
        values.publisher   = firstValue(properties, kPublisherKey);
        values.year        = firstValue(properties, kYearKey);
        values.bpm         = firstValue(properties, kBpmKey);

        splitNumberAndTotal(firstValue(properties, kTrackKey), values.trackNumber, values.trackTotal);
        splitNumberAndTotal(firstValue(properties, kDiscKey), values.discNumber, values.discTotal);

        readArtwork(fileRef, values.artwork, values.artworkMimeType);

        values.ok = true;
        return values;
    }

    AudioInfo TagEditor::readAudioInfo(const juce::File& file)
    {
        AudioInfo info;

        if (! file.existsAsFile())
            return info;

        TagLib::FileRef fileRef(fileNameFor(file), true, TagLib::AudioProperties::Average);
        if (fileRef.isNull() || fileRef.audioProperties() == nullptr)
            return info;

        auto* properties = fileRef.audioProperties();

        info.format = file.getFileExtension().removeCharacters(".").toUpperCase();
        info.lengthSeconds = properties->lengthInSeconds();
        info.bitrateKbps = properties->bitrate();
        info.sampleRate = properties->sampleRate();
        info.channels = properties->channels();
        info.fileSizeBytes = file.getSize();
        info.ok = true;

        return info;
    }

    bool TagEditor::write(const juce::File& file, const TagChanges& changes, juce::String& errorMessage)
    {
        if (! file.existsAsFile())
        {
            errorMessage = "That file no longer exists.";
            return false;
        }

        if (! changes.anythingToDo())
            return true; // saving with nothing changed is not a failure

        if (! canEdit(file))
        {
            errorMessage = "Inkwyrd can't write tags to a " + file.getFileExtension() + " file.";
            return false;
        }

        // Read the current values first: a number/total pair shares one
        // tag, so writing one of them needs the other as it stands.
        auto current = read(file);

        // The copy. TemporaryFile puts it beside the original, so the
        // swap at the end is a rename on the same volume rather than a
        // second copy across drives.
        juce::TemporaryFile temp(file);

        if (! file.copyFileTo(temp.getFile()))
        {
            errorMessage = "Couldn't make a working copy next to the file - is the folder writable?";
            return false;
        }

        {
            TagLib::FileRef fileRef(fileNameFor(temp.getFile()), false);

            if (fileRef.isNull() || fileRef.file() == nullptr)
            {
                errorMessage = "Couldn't open the working copy.";
                return false;
            }

            auto properties = fileRef.file()->properties();

            applyField(properties, kTitleKey, changes.title);
            applyField(properties, kArtistKey, changes.artist);
            applyField(properties, kAlbumKey, changes.album);
            applyField(properties, kAlbumArtistKey, changes.albumArtist);
            applyField(properties, kGenreKey, changes.genre);
            applyField(properties, kCommentKey, changes.comment);
            applyField(properties, kComposerKey, changes.composer);
            applyField(properties, kPublisherKey, changes.publisher);
            applyField(properties, kYearKey, changes.year);
            applyField(properties, kBpmKey, changes.bpm);

            applyPair(properties, kTrackKey, changes.trackNumber, changes.trackTotal,
                       current.trackNumber, current.trackTotal);
            applyPair(properties, kDiscKey, changes.discNumber, changes.discTotal,
                       current.discNumber, current.discTotal);

            fileRef.file()->setProperties(properties);

            if (changes.artworkAction == TagChanges::ArtworkAction::clear)
            {
                fileRef.setComplexProperties(kPictureKey, {});
            }
            else if (changes.artworkAction == TagChanges::ArtworkAction::set
                      && changes.artwork.getSize() > 0)
            {
                TagLib::VariantMap picture;
                picture.insert("data", TagLib::ByteVector((const char*) changes.artwork.getData(),
                                                            (unsigned int) changes.artwork.getSize()));
                picture.insert("mimeType", toTagLib(changes.artworkMimeType.isNotEmpty()
                                                        ? changes.artworkMimeType
                                                        : juce::String("image/jpeg")));
                picture.insert("pictureType", TagLib::String("Front Cover"));

                fileRef.setComplexProperties(kPictureKey, { picture });
            }

            if (! fileRef.file()->save())
            {
                errorMessage = "TagLib couldn't write the tags.";
                return false;
            }
        }

        // Prove the copy is still readable BEFORE it replaces anything.
        // A file that can't be re-opened is exactly what must never reach
        // the user's library.
        if (! read(temp.getFile()).ok)
        {
            errorMessage = "The tagged copy wouldn't read back, so the original was left alone.";
            return false;
        }

        if (! temp.overwriteTargetFileWithTemporary())
        {
            // The usual cause is the file being open elsewhere - playing,
            // or held by another program.
            errorMessage = "Couldn't replace the file. If it's playing, stop it and try again.";
            return false;
        }

        return true;
    }

    juce::StringArray TagEditor::standardGenres()
    {
        juce::StringArray genres;

        for (const auto& genre : TagLib::ID3v1::genreList())
            genres.add(fromTagLib(genre));

        genres.sortNatural();
        return genres;
    }
}
