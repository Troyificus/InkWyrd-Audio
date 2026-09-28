#pragma once

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <vector>

// Keeps credentials out of the log file.
//
// This exists because the log is something people are now ASKED to send:
// the bug-report template on GitHub tells them to attach it. A log that
// can carry a bot token is a log that gets a stranger's bot taken over by
// whoever reads the issue.
//
// Nothing currently logs a secret - the Discord code is careful about it
// on purpose, and logs a token's LENGTH rather than the token (see
// GatewayClient::handleVoiceServerUpdate). This is the net under that
// care, so one careless logLine() in some future session can't quietly
// undo it.
//
// Two layers, because either alone has a real gap:
//
//  1. EXACT values, registered at startup from AppSettings. Precise -
//     no false positives, and it catches secrets of any shape
//     (the OAuth client secret and refresh token are plain alphanumeric
//     runs that no pattern could safely single out from a checksum or
//     an ID).
//  2. A SHAPE check for Discord bot tokens, for the case where a secret
//     reaches the log before it was registered, or came from somewhere
//     other than settings. Deliberately narrow - see below.
namespace inkwyrd
{

// Is this the three-segment shape of a Discord bot token
// (base64url user id "." base64url timestamp "." hmac)?
//
// The length floors are set well above anything that turns up in an
// ordinary log line. Version strings ("0.1.0-beta.33"), filenames
// ("orc.battle.mp3") and hostnames ("github.com") all fail on segment
// length or segment count, which is what keeps this from redacting the
// diagnostics the log exists for.
inline bool looksLikeBotToken(const std::string& candidate)
{
    std::vector<std::string> parts;
    std::string current;
    for (char c : candidate)
    {
        if (c == '.')
        {
            parts.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }
    parts.push_back(current);

    if (parts.size() != 3)
        return false;
    if (parts[0].size() < 20 || parts[1].size() < 5 || parts[2].size() < 20)
        return false;

    // Every segment must be base64url, and the whole thing must mix
    // letters and digits - a long run of only one or the other is far
    // likelier to be a path fragment than a credential.
    bool sawLetter = false, sawDigit = false;
    for (const auto& part : parts)
        for (unsigned char c : part)
        {
            if (!(std::isalnum(c) || c == '_' || c == '-'))
                return false;
            if (std::isalpha(c)) sawLetter = true;
            if (std::isdigit(c)) sawDigit = true;
        }

    return sawLetter && sawDigit;
}

// Replaces every occurrence of each known secret, then anything
// bot-token-shaped, with "<redacted>".
//
// Secrets shorter than 8 characters are IGNORED. This matters more than
// it looks: an empty or one-character entry (an unset setting, most
// obviously) would otherwise match at every position and turn the whole
// log into redaction markers, destroying exactly the diagnostics this is
// meant to keep usable.
inline std::string redactLine(std::string line, const std::vector<std::string>& knownSecrets)
{
    const std::string marker = "<redacted>";

    for (const auto& secret : knownSecrets)
    {
        if (secret.size() < 8)
            continue;

        for (auto at = line.find(secret); at != std::string::npos; at = line.find(secret, at + marker.size()))
            line.replace(at, secret.size(), marker);
    }

    // Now the shape backstop, over each maximal run of characters a
    // token could be made of. A run is bounded by anything else -
    // spaces, quotes, slashes, backslashes - so a Windows path can't
    // merge with the text around it into one candidate.
    const auto isTokenChar = [](unsigned char c)
    {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.';
    };

    std::string out;
    out.reserve(line.size());
    std::size_t i = 0;
    while (i < line.size())
    {
        if (!isTokenChar(static_cast<unsigned char>(line[i])))
        {
            out += line[i++];
            continue;
        }

        std::size_t start = i;
        while (i < line.size() && isTokenChar(static_cast<unsigned char>(line[i])))
            ++i;

        auto run = line.substr(start, i - start);
        out += looksLikeBotToken(run) ? marker : run;
    }

    return out;
}

// The secrets registered for this process. Set once at startup, and
// again whenever Settings saves new ones.
inline std::mutex& secretsMutex()
{
    static std::mutex m;
    return m;
}

inline std::vector<std::string>& secretsStore()
{
    static std::vector<std::string> secrets;
    return secrets;
}

inline void setSecretsToRedact(std::vector<std::string> secrets)
{
    const std::lock_guard<std::mutex> lock(secretsMutex());
    secretsStore() = std::move(secrets);
}

inline std::string redactSecrets(const std::string& line)
{
    const std::lock_guard<std::mutex> lock(secretsMutex());
    return redactLine(line, secretsStore());
}

} // namespace inkwyrd
