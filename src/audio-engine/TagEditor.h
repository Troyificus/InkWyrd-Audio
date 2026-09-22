#pragma once

#include <juce_core/juce_core.h>

namespace inkwyrd
{
    // Reading and WRITING a track's tags, through TagLib.
    //
    // Separate from TrackMetadataStore, which is a cache built for
    // painting rows fast. This is the editor's side: every field the tag
    // window offers, artwork included, and the write path.
    //
    // WRITING TOUCHES SOMEONE'S MUSIC FILES, so write() never edits the
    // original in place. It copies the file, tags the copy, re-reads it
    // to check the result, and only then swaps it in. A crash or a
    // failure part-way leaves the original exactly as it was. The cost
    // is a file copy per save, which is nothing next to corrupting an
    // album nobody has a second copy of.
    //
    // No window, no JUCE GUI - so the headless self-test drives the real
    // write path, including the check that matters most: that tagging a
    // file doesn't change a single audio sample.

    // What to do with one field. Three states rather than two, because
    // editing SEVERAL tracks at once needs "leave this one alone" to be
    // different from "set it to empty".
    struct TagField
    {
        enum class Action { leave, set, clear };

        Action action = Action::leave;
        juce::String value;

        static TagField leaveAlone()                      { return {}; }
        static TagField setTo(const juce::String& text)   { return { Action::set, text }; }
        static TagField cleared()                          { return { Action::clear, {} }; }

        bool isLeave() const { return action == Action::leave; }
    };

    // What a file currently holds. Numbers are text so an absent tag and
    // a zero stay distinguishable - "0" is a real (if odd) track number,
    // an empty string is no tag at all.
    struct TagValues
    {
        bool ok = false;

        juce::String title, artist, album, albumArtist, genre, comment,
                     composer, publisher, year,
                     trackNumber, trackTotal, discNumber, discTotal, bpm;

        // Front cover, empty when the file carries none.
        juce::MemoryBlock artwork;
        juce::String artworkMimeType;

        // How long the track plays for, from the same open as the tags.
        // 0 when TagLib couldn't work it out.
        int lengthMilliseconds = 0;
    };

    struct TagChanges
    {
        TagField title, artist, album, albumArtist, genre, comment,
                 composer, publisher, year,
                 trackNumber, trackTotal, discNumber, discTotal, bpm;

        enum class ArtworkAction { leave, set, clear };
        ArtworkAction artworkAction = ArtworkAction::leave;
        juce::MemoryBlock artwork;
        juce::String artworkMimeType;

        bool anythingToDo() const;
    };

    // The read-only panel in the editor.
    struct AudioInfo
    {
        bool ok = false;
        juce::String format;      // "MP3", "FLAC", ...
        int lengthSeconds = 0;
        int bitrateKbps = 0;
        int sampleRate = 0;
        int channels = 0;
        juce::int64 fileSizeBytes = 0;
    };

    class TagEditor
    {
    public:
        // Whether TagLib recognises this file at all. A format it can't
        // open is reported rather than silently doing nothing.
        static bool canEdit(const juce::File& file);

        static TagValues read(const juce::File& file);
        static AudioInfo readAudioInfo(const juce::File& file);

        // Copy, tag the copy, verify, swap in. false + errorMessage on
        // any failure, with the original untouched.
        static bool write(const juce::File& file, const TagChanges& changes,
                           juce::String& errorMessage);

        // For the editor's genre box. The ID3v1 list everything still
        // agrees on, which is what other taggers offer.
        static juce::StringArray standardGenres();
    };
}
