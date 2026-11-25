#pragma once

#include <juce_core/juce_core.h>

#include "TrackMetadataStore.h"

// What a track is CALLED, resolved once in one place.
//
// Lifted out of NowPlayingDisplay.cpp when the system media controls
// needed the same answer: what Windows' media panel and any now-playing
// overlay show has to be what the app's own display shows, and two
// copies of this logic would eventually disagree about some track.
namespace inkwyrd
{
    struct TrackName
    {
        juce::String artist, title;
    };

    // The fallback when a file has no title tag, or hasn't been scanned
    // yet: filenames are very often "Artist - Title", so splitting on the
    // first " - " still gets a real artist line a lot of the time.
    inline TrackName splitTrackName(const juce::File& file)
    {
        auto name = file.getFileNameWithoutExtension();
        auto separator = name.indexOf(" - ");

        if (separator > 0)
            return { name.substring(0, separator).trim(), name.substring(separator + 3).trim() };

        return { {}, name };
    }

    // Tags first - the same source as the Library and Playlist windows,
    // so the three never disagree about what a track is called. Each
    // field falls back on its own: a file tagged with an artist but no
    // title still gets its artist.
    inline TrackName trackNameFor(const juce::File& file, const TrackMetadataStore& trackMetadata)
    {
        if (file == juce::File())
            return {};

        auto metadata = trackMetadata.get(file);
        auto fromFilename = splitTrackName(file);

        if (metadata.title.isNotEmpty())
            return { metadata.artist, metadata.title };

        return { metadata.artist.isNotEmpty() ? metadata.artist : fromFilename.artist,
                  fromFilename.title };
    }
}
