#pragma once

#include <juce_core/juce_core.h>

namespace inkwyrd
{
    // The Library search box's matching rule, kept out of PlaylistPanel
    // so it can be tested without a window - see INKWYRD_SELFTEST.
    //
    // Every WORD of the query has to appear somewhere in the haystack,
    // in any order, rather than the query matching as one run of text:
    // people type "drake blue" for "Blue Drake" and expect to find it.
    // Case-insensitive, and an empty query matches everything, so a
    // cleared box means "no filter" rather than "nothing matches".
    inline bool matchesSearchTerms(const juce::String& haystack, const juce::String& query)
    {
        auto trimmed = query.trim();
        if (trimmed.isEmpty())
            return true;

        auto lowerHaystack = haystack.toLowerCase();

        for (const auto& term : juce::StringArray::fromTokens(trimmed.toLowerCase(), true))
            if (term.isNotEmpty() && ! lowerHaystack.contains(term))
                return false;

        return true;
    }
}
