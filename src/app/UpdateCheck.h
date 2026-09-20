#pragma once

#include <functional>

#include <juce_core/juce_core.h>

namespace inkwyrd
{
    struct ReleaseInfo
    {
        juce::String version; // as tagged, e.g. "0.1.0-beta.24" (any leading "v" removed)
        juce::String url;     // where to go and read about it
        bool valid = false;
    };

    // Splits "v0.1.0-beta.22.3" into its numbers and words. Inline and
    // pure so the self-test can check the comparison without going near
    // the network - see isNewerRelease.
    inline juce::StringArray releaseVersionParts(const juce::String& version)
    {
        auto trimmed = version.trim();
        if (trimmed.startsWithIgnoreCase("v"))
            trimmed = trimmed.substring(1);

        // The hyphen in "0.1.0-beta.23" is just another separator here:
        // what matters is the sequence 0, 1, 0, beta, 23.
        return juce::StringArray::fromTokens(trimmed.replaceCharacter('-', '.'), ".", {});
    }

    // Whether `candidate` is a later release than `current`.
    //
    // The rules this project's own versions need, and no more: numbers
    // compare as NUMBERS (so beta.23 beats beta.9, which a plain string
    // comparison gets backwards), words compare alphabetically, and a
    // version that has run out of parts is the earlier one - so
    // 0.1.0-beta.22.3 beats 0.1.0-beta.22, and a finished 0.1.0 beats
    // any 0.1.0-beta, because "beta" is a part 0.1.0 doesn't have.
    inline bool isNewerRelease(const juce::String& current, const juce::String& candidate)
    {
        auto a = releaseVersionParts(current);
        auto b = releaseVersionParts(candidate);

        // A release with a qualifier is EARLIER than the same version
        // without one, which is the opposite of "more parts wins", so
        // that case is decided before length is.
        auto numberOrWord = [](const juce::String& part) { return part.containsOnly("0123456789"); };

        for (int i = 0; i < juce::jmax(a.size(), b.size()); ++i)
        {
            if (i >= a.size())
                return numberOrWord(b[i]); // 1.2 vs 1.2.1 -> newer; 1.2 vs 1.2-beta -> not
            if (i >= b.size())
                return ! numberOrWord(a[i]); // 1.2-beta vs 1.2 -> newer

            const auto& left = a[i];
            const auto& right = b[i];

            if (left == right)
                continue;

            if (numberOrWord(left) && numberOrWord(right))
                return right.getLargeIntValue() > left.getLargeIntValue();

            // A number and a word at the same position can't be compared
            // meaningfully. Treat it as "not newer" rather than guessing:
            // the cost of missing an update notice is far lower than the
            // cost of nagging about one that doesn't exist.
            if (numberOrWord(left) != numberOrWord(right))
                return false;

            return right.compareIgnoreCase(left) > 0;
        }

        return false; // identical
    }

    // Pulls the version and link out of GitHub's releases JSON. Returns
    // an invalid ReleaseInfo for anything unexpected - a rate-limit
    // message, an error body, or HTML from a captive portal.
    //
    // Reads a LIST (/releases) rather than /releases/latest, because
    // GitHub's "latest" deliberately skips pre-releases and every
    // release of this app so far is one. The first entry is the newest.
    ReleaseInfo parseReleasesJson(const juce::String& json);

    // Asks GitHub, once, whether anything newer than this build exists,
    // on a background thread.
    //
    // Deliberately never downloads or runs anything: it reports, and the
    // user goes to the page themselves. An app that fetched and launched
    // an executable would be doing exactly what this project's Defender
    // false-positive history says not to do.
    //
    // onNewerRelease is called on the MESSAGE thread, and only when
    // there is genuinely something newer. Silence is the result of
    // being offline, being rate-limited, or being up to date - none of
    // which is worth telling anyone about.
    void checkForNewerRelease(const juce::String& currentVersion,
                               std::function<void(ReleaseInfo)> onNewerRelease);
}
