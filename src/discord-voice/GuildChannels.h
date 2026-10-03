#pragma once

#include <juce_core/juce_core.h>

// Explains why a voice join went nowhere, using the channel list Discord
// already sent.
//
// Why this exists: a real report had the bot authenticate, appear in the
// server, send its join request, and then nothing. Discord does not
// answer a voice state update it cannot honour - no error, no close, it
// simply never sends VOICE_STATE_UPDATE and VOICE_SERVER_UPDATE back. All
// the app could say was "Timed out waiting for voice server info - check
// the server/channel IDs", which names two things to check and helps with
// neither.
//
// It does not have to be a guess. GUILD_CREATE carries every channel in
// the guild with its id, name and type, and it arrives BEFORE the join is
// attempted. So by the time the join times out, the app already knows
// whether that channel exists, what it is called, and whether it is a
// voice channel at all.
namespace inkwyrd
{

// Discord channel types, the ones worth telling apart here.
// https://discord.com/developers/docs/resources/channel
enum class DiscordChannelType
{
    text          = 0,
    voice         = 2,
    category      = 4,
    announcement  = 5,
    stageVoice    = 13,
    forum         = 15,
    media         = 16,
};

struct GuildChannel
{
    juce::String id;
    juce::String name;
    int type = -1;
};

// Empty means "nothing wrong that this can see" - the channel exists and
// is an ordinary voice channel, so the cause is something the channel
// list cannot show, and the caller should say so in its own words.
//
// found=false covers both "no such channel" and "it is in a different
// server", which look identical from here and have the same fix.
inline juce::String describeVoiceChannelProblem(bool found, int type, const juce::String& name)
{
    if (! found)
        return "No channel with that ID exists in that server. Right-click the voice channel you "
               "want in Discord and choose Copy Channel ID - copying a channel from a different "
               "server, or copying the server's own ID, gives you a number that looks right and "
               "isn't.";

    const auto named = name.isNotEmpty() ? " (\"" + name + "\")" : juce::String();

    switch (static_cast<DiscordChannelType>(type))
    {
        case DiscordChannelType::voice:
            return {};

        case DiscordChannelType::text:
        case DiscordChannelType::announcement:
        case DiscordChannelType::forum:
        case DiscordChannelType::media:
            return "That ID is a TEXT channel" + named + ", not a voice channel. In Discord, "
                   "right-click the voice channel (the one under the speaker icon that people "
                   "join) and choose Copy Channel ID.";

        case DiscordChannelType::category:
            return "That ID is a category" + named + ", which is the heading channels sit under "
                   "rather than a channel itself. Right-click the voice channel inside it and "
                   "choose Copy Channel ID.";

        case DiscordChannelType::stageVoice:
            return "That ID is a Stage channel" + named + ". Inkwyrd can't join those: they need "
                   "a speaker invitation that an ordinary bot can't give itself. Use a normal "
                   "voice channel.";

        default:
            return "That ID is a channel" + named + " of a kind Inkwyrd can't join (Discord type "
                    + juce::String(type) + "). Use an ordinary voice channel.";
    }
}

// When the channel checks out but the join still went unanswered, what is
// left is permissions and capacity, neither of which the channel list can
// show.
//
// The ordering is not arbitrary. A PRIVATE channel is what this actually
// turned out to be in the one real case: a private channel admits only
// the roles and people named in its own permissions, and a bot invited
// five minutes ago is not among them. Nothing about that is obvious from
// the Discord UI, and the server-wide Connect permission from the invite
// URL does not override it.
inline juce::String describeSilentVoiceJoinFailure(const juce::String& channelName)
{
    const auto named = channelName.isNotEmpty() ? " (\"" + channelName + "\")" : juce::String();

    return "Discord ignored the request to join that voice channel" + named + ", which it does "
           "without reporting an error. The ID is right and it is a voice channel, so the bot is "
           "not being allowed in. In order of likelihood:\n\n"
           "1. IS IT A PRIVATE CHANNEL? This is far and away the usual cause. A private channel "
           "only admits the roles and people listed in its own permissions, and a bot you have "
           "just invited is not one of them. Edit the channel in Discord, add your bot's role, "
           "and allow Connect.\n\n"
           "2. Even on a channel that is not private, the bot's role can be denied Connect there "
           "specifically. Check the channel's own permissions, not only the server-wide ones.\n\n"
           "3. The channel may be full. A bot cannot go over a user limit without Move Members.";
}

} // namespace inkwyrd
