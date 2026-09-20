#include "UpdateCheck.h"

#include <thread>

#include <juce_events/juce_events.h>

namespace
{
    // Public, unauthenticated, and rate-limited per IP. One call per
    // launch is nowhere near that limit, and a limited response simply
    // fails to parse - which this treats as "say nothing", like any
    // other failure.
    constexpr const char* kReleasesUrl =
        "https://api.github.com/repos/Troyificus/InkWyrd-Audio/releases?per_page=5";

    constexpr int kConnectTimeoutMs = 8000;
}

namespace inkwyrd
{
    ReleaseInfo parseReleasesJson(const juce::String& json)
    {
        auto parsed = juce::JSON::parse(json);

        auto* releases = parsed.getArray();
        if (releases == nullptr)
            return {};

        for (const auto& entry : *releases)
        {
            auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;

            // Drafts are not published to anyone, so treating one as an
            // available update would point users at a 404.
            if (object->getProperty("draft"))
                continue;

            auto tag = object->getProperty("tag_name").toString().trim();
            if (tag.isEmpty())
                continue;

            ReleaseInfo info;
            info.version = tag.startsWithIgnoreCase("v") ? tag.substring(1) : tag;
            info.url = object->getProperty("html_url").toString();
            info.valid = true;
            return info;
        }

        return {};
    }

    void checkForNewerRelease(const juce::String& currentVersion,
                               std::function<void(ReleaseInfo)> onNewerRelease)
    {
        if (! onNewerRelease)
            return;

        // Detached rather than a member thread: it holds nothing but its
        // own copies, and it finishes on its own within the timeout.
        // Anything it has to say is handed back through
        // callAsync, which is a no-op once the app is shutting down.
        std::thread([currentVersion, onNewerRelease = std::move(onNewerRelease)]
        {
            juce::URL url(kReleasesUrl);

            // GitHub rejects requests with no User-Agent outright.
            auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                               .withConnectionTimeoutMs(kConnectTimeoutMs)
                               .withExtraHeaders("User-Agent: InkwyrdAudio\r\n"
                                                  "Accept: application/vnd.github+json");

            auto stream = url.createInputStream(options);
            if (stream == nullptr)
                return; // offline, blocked, or timed out: say nothing

            auto latest = parseReleasesJson(stream->readEntireStreamAsString());

            if (! latest.valid || ! isNewerRelease(currentVersion, latest.version))
                return;

            juce::MessageManager::callAsync([onNewerRelease, latest] { onNewerRelease(latest); });
        }).detach();
    }
}
