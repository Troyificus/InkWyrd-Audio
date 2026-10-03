#pragma once

#include <juce_core/juce_core.h>

// Sanity checks on what someone pasted into the Discord fields in
// Settings, so an obviously wrong value is caught at the moment it is
// entered rather than surfacing minutes later as a failed connection.
//
// This exists because of a real report. A clean setup produced a bot
// that sat offline and never joined its voice channel, and the cause was
// that the token, the server ID and the channel ID had every one of them
// been stored TWICE over - the same value repeated end to end with no
// separator. The token field is password-masked, so there was nothing to
// see; the IDs just looked like long numbers.
//
// The mechanism, worth knowing because it is not a typo somebody made:
// Settings loads the saved values into its fields when it opens, JUCE
// puts the caret where you click rather than selecting what is there, so
// pasting into a field that already held the value INSERTED a second
// copy beside the first. Discord then closed the gateway with 4004 and
// the app reported a timeout.
//
// Two fixes went in together: the fields now select their contents when
// focused (so a paste replaces), and these checks refuse to save a value
// that cannot possibly be right. The doubled case gets its own message,
// because "pasted twice" is a thing somebody can act on and "invalid
// token" is not.
//
// Everything here returns an EMPTY string to mean "no problem found",
// and otherwise a sentence to put in front of the user.
namespace inkwyrd
{

// Is this value exactly some shorter value repeated twice, end to end?
inline bool isDoubledValue(const juce::String& value)
{
    auto length = value.length();
    if (length < 2 || length % 2 != 0)
        return false;

    return value.substring(0, length / 2) == value.substring(length / 2);
}

// Recovers the original from a value that was pasted twice: returns one
// copy if the value is exactly itself repeated, and the value unchanged
// otherwise.
//
// Used to REPAIR rather than refuse. Catching the mistake and making the
// user fix it by hand sounds tidier than it is, because the token box is
// password-masked - there is nothing to see, so "clear it and paste it
// again" is a blind operation they have already got wrong once. Halving
// a doubled value cannot damage anything: it reconstructs exactly what
// was pasted.
//
// It cannot misfire on a good value either. Two halves of a real token
// being byte-identical does not happen by chance, and a doubled Discord
// ID lands at 34 to 40 digits, well outside the 17 to 20 a real one has.
inline juce::String collapseDoubledValue(const juce::String& value)
{
    auto trimmed = value.trim();
    return isDoubledValue(trimmed) ? trimmed.substring(0, trimmed.length() / 2) : value;
}

// A Discord bot token is three base64url segments separated by dots.
// The lengths vary between token generations, so only the structure is
// checked, not how long each piece is - a length rule would start
// rejecting valid tokens the next time Discord changes the format.
inline juce::String describeBotTokenProblem(const juce::String& token)
{
    auto trimmed = token.trim();

    // Empty is fine: Discord is optional, and leaving it blank is how
    // somebody runs Inkwyrd for local playback only.
    if (trimmed.isEmpty())
        return {};

    if (isDoubledValue(trimmed))
        return "That bot token looks like it was pasted twice - the same token appears in it "
               "end to end. Clear the box (click in it and press Ctrl+A, then Delete) and paste "
               "it once.";

    if (trimmed.containsAnyOf(" \t\r\n"))
        return "That bot token has a space in it. Copy it again from the Developer Portal, "
               "taking care not to pick up anything either side of it.";

    juce::StringArray parts;
    parts.addTokens(trimmed, ".", {});

    if (parts.size() != 3)
        return "That doesn't look like a bot token. A bot token is three pieces separated by "
               "full stops, and this has " + juce::String(parts.size())
                + (parts.size() == 1 ? " piece." : " pieces.")
                + " Check you copied the token from the Bot tab, rather than the Application ID "
                  "or the Public Key.";

    for (const auto& part : parts)
        if (part.isEmpty())
            return "That bot token has an empty piece in it, so something was lost copying it. "
                   "Copy it again from the Developer Portal.";

    return {};
}

// Guild and channel IDs are Discord snowflakes: 17 to 20 digits.
// whatItIs names the field, so the message reads naturally for either.
inline juce::String describeDiscordIdProblem(const juce::String& id, const juce::String& whatItIs)
{
    auto trimmed = id.trim();

    if (trimmed.isEmpty())
        return {};

    if (isDoubledValue(trimmed))
        return "That " + whatItIs + " looks like it was pasted twice - the same number appears "
               "in it end to end. Clear the box (click in it and press Ctrl+A, then Delete) and "
               "paste it once.";

    if (! trimmed.containsOnly("0123456789"))
        return "That " + whatItIs + " should be digits only. Turn on Developer Mode in Discord "
               "(User Settings -> Advanced), then right-click and Copy ID - copying the name "
               "gives you text rather than an ID.";

    if (trimmed.length() < 17 || trimmed.length() > 20)
        return "That " + whatItIs + " is " + juce::String(trimmed.length())
                + " digits long, and a Discord ID is 17 to 20. Right-click in Discord and use "
                  "Copy ID to get the whole thing.";

    return {};
}

} // namespace inkwyrd
