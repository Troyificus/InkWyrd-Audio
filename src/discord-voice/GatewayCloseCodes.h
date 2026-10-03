#pragma once

#include <juce_core/juce_core.h>

// Turns a Discord gateway close code into something worth showing a
// user.
//
// Why this exists: a real report had the bot sitting offline and never
// joining its voice channel. The log said exactly what was wrong -
// "[Gateway] closed: Authentication failed." - while the app told the
// user "Timed out waiting for Discord gateway". A timeout reads like a
// network problem and sends people to check their connection, their
// firewall and their channel ID, none of which were the cause. Discord
// had said plainly that the token was wrong.
//
// Only the codes a user can actually do something about get their own
// wording. The rest fall through to a generic line that still carries
// the number, so an issue report can say which one it was.
namespace inkwyrd
{

// Empty means "no useful explanation for this code" - the caller should
// fall back to whatever it would have said anyway.
inline juce::String describeGatewayCloseCode(int code)
{
    switch (code)
    {
        case 4004:
            return "Discord rejected the bot token. Check it in Settings - copy it again from "
                   "the Bot tab of the Developer Portal, and make sure it hasn't been reset "
                   "since you last copied it.";

        case 4014:
            return "Discord refused the connection because this bot is asking for privileged "
                   "intents it hasn't been granted. Inkwyrd doesn't need any, so turn them off "
                   "under Bot -> Privileged Gateway Intents in the Developer Portal.";

        case 4013:
            return "Discord rejected the set of gateway intents Inkwyrd asked for. That's a bug "
                   "in Inkwyrd rather than anything you've configured - please report it.";

        case 4008:
            return "Discord is rate-limiting this bot. Wait a minute or two before trying again.";

        case 4003:
        case 4005:
            return "Inkwyrd and Discord got out of step during sign-in. Restarting Inkwyrd should "
                   "clear it; please report it if it keeps happening.";

        case 4010:
        case 4011:
            return "Discord says this bot needs sharding, which happens when a bot is in a very "
                   "large number of servers. Inkwyrd doesn't support that.";

        case 4012:
            return "Discord rejected the gateway version Inkwyrd uses. That needs a new version "
                   "of Inkwyrd - please report it.";

        default:
            return {};
    }
}

} // namespace inkwyrd
