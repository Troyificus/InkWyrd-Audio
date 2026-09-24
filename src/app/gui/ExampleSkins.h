#pragma once

#include <vector>

#include <juce_core/juce_core.h>

namespace inkwyrd
{
    // The skins that ship inside the app (src/app/skins, packed into the exe
    // as one zip), and keeping the user's copies of them current.
    //
    // THE RULE: a shipped skin in the user's skins folder is replaced by a
    // newer version only when every file in it is still exactly what the
    // app wrote. An edited copy is never touched, and a deleted one stays
    // deleted. Without this, a skin could never be improved for anyone who
    // already had it - which is what happened to Pixel Phosphor between
    // beta.31 and its Silkscreen update.
    //
    // How "exactly what the app wrote" is known: each install leaves a small
    // manifest in the skin's folder (kManifestName) with the version and a
    // fingerprint of every file. Copies from before manifests existed (the
    // beta.30 flat examples, beta.31's Pixel Phosphor) are recognised by
    // their content instead: skin.json matching apart from its version, and
    // every other file byte for byte.
    //
    // Pure over files - no settings, no window - so the self-test covers
    // every case.
    struct ExampleSkin
    {
        juce::String name;   // also the folder name
        int version = 0;     // skin.json's "version"
        std::vector<std::pair<juce::String, juce::MemoryBlock>> files;
    };

    class ExampleSkins
    {
    public:
        static constexpr const char* kManifestName = ".inkwyrd-example.json";

        enum class Outcome
        {
            installed,       // wasn't there, now is
            updated,         // an untouched older copy was replaced
            alreadyCurrent,  // nothing to do
            keptEdited,      // the user changed it, so it was left alone
            keptDeleted,     // installed before and since deleted - stays gone
            failed           // couldn't write
        };

        // Every skin in a zip whose entries are "<Skin name>/<file>". The
        // version comes from each skin.json.
        static std::vector<ExampleSkin> fromZip(const void* data, size_t size);

        // `previouslyInstalledVersion` is what the app last recorded for
        // this skin (0 = never) - it's how a deleted skin is told apart from
        // one that was never installed.
        static Outcome install(const ExampleSkin& skin, const juce::File& skinsFolder,
                               int previouslyInstalledVersion);

        static juce::String describe(Outcome outcome);

        // The version a skin folder's own skin.json claims (0 if none).
        static int versionOf(const juce::File& skinFolder);
    };
}
