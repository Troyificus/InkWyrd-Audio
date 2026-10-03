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
                   "right-click the voice channel - the one under the speaker icon that people "
                   "join - and choose Copy Channel ID.";

        case DiscordChannelType::category:
            return "That ID is a category" + named + ", which is the heading channels sit under "
                   "rather than a channel itself. Right-click the voice channel inside it and "
                   "choose Copy Channel ID.";

        case DiscordChannelType::stageVoice:
            return "That ID is a Stage channel" + named + ". Inkwyrd can't join those - they need "
                   "a speaker invitation that an ordinary bot can't give itself. Use a normal "
                   "voice channel.";

        default:
            return "That ID is a channel" + named + " of a kind Inkwyrd can't join (Discord type "
                    + juce::String(type) + "). Use an ordinary voice channel.";
    }
}

// When the channel checks out but the join still went unanswered, the
// remaining causes are permissions and capacity - neither of which is
// visible in the channel list.
inline juce::String describeSilentVoiceJoinFailure(const juce::String& channelName)
{
    const auto named = channelName.isNotEmpty() ? " (\"" + channelName + "\")" : juce::String();

    return "Discord ignored the request to join that voice channel" + named + ", which it does "
           "without reporting an error. The channel is real and is a voice channel, so the usual "
           "causes are:\n\n"
           "- the bot's role isn't allowed to Connect to that particular channel. Check the "
           "channel's own permissions in Discord, not just the server-wide ones;\n"
           "- the channel is full, and the bot can't exceed a user limit without Move Members.";
}

} // namespace inkwyrd
