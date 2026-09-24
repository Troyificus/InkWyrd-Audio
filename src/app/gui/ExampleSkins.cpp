#include "ExampleSkins.h"

namespace inkwyrd
{
    namespace
    {
        constexpr const char* kSkinJson = "skin.json";

        // A fingerprint, not security: it only has to tell "the file the app
        // wrote" from "a file someone has since changed". 64-bit FNV-1a plus
        // the size is plenty for that, and needs nothing beyond juce_core.
        juce::String fingerprint(const void* data, size_t size)
        {
            juce::uint64 hash = 14695981039346656037ull;
            auto* bytes = static_cast<const juce::uint8*>(data);

            for (size_t i = 0; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }

            return juce::String::toHexString((juce::int64) hash) + ":" + juce::String((juce::int64) size);
        }

        juce::String fingerprint(const juce::File& file)
        {
            juce::MemoryBlock data;
            if (! file.loadFileAsData(data))
                return {};
            return fingerprint(data.getData(), data.getSize());
        }

        // Same value, however it was written: key order, 6 vs 6.0, and
        // spacing don't count. The top-level "version" is ignored when asked,
        // so a copy from before versions existed still matches.
        bool sameValue(const juce::var& a, const juce::var& b)
        {
            auto isNumber = [](const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); };

            if (isNumber(a) && isNumber(b))
                return juce::approximatelyEqual((double) a, (double) b);

            if (auto* oa = a.getDynamicObject())
            {
                auto* ob = b.getDynamicObject();
                if (ob == nullptr || oa->getProperties().size() != ob->getProperties().size())
                    return false;

                for (const auto& entry : oa->getProperties())
                    if (! ob->hasProperty(entry.name) || ! sameValue(entry.value, ob->getProperty(entry.name)))
                        return false;

                return true;
            }

            if (auto* arrayA = a.getArray())
            {
                auto* arrayB = b.getArray();
                if (arrayB == nullptr || arrayA->size() != arrayB->size())
                    return false;

                for (int i = 0; i < arrayA->size(); ++i)
                    if (! sameValue(arrayA->getReference(i), arrayB->getReference(i)))
                        return false;

                return true;
            }

            return a.toString() == b.toString() && a.isString() == b.isString();
        }

        juce::var withoutVersion(const juce::var& json)
        {
            auto copy = json.clone();
            if (auto* object = copy.getDynamicObject())
                object->removeProperty("version");
            return copy;
        }

        bool sameSkinJson(const juce::String& a, const juce::String& b)
        {
            juce::var parsedA, parsedB;
            if (! juce::JSON::parse(a, parsedA).wasOk() || ! juce::JSON::parse(b, parsedB).wasOk())
                return false;

            return sameValue(withoutVersion(parsedA), withoutVersion(parsedB));
        }

        // The version of the untouched copy in `folder`, or -1 if any file of
        // ours in it has been changed or removed.
        int untouchedVersion(const ExampleSkin& skin, const juce::File& folder, bool& hasManifest)
        {
            auto manifestFile = folder.getChildFile(ExampleSkins::kManifestName);
            juce::var manifest;
            hasManifest = manifestFile.existsAsFile()
                           && juce::JSON::parse(manifestFile.loadFileAsString(), manifest).wasOk()
                           && manifest.getDynamicObject() != nullptr;

            if (hasManifest)
            {
                auto* files = manifest.getProperty("files", {}).getDynamicObject();
                if (files == nullptr)
                    return -1;

                for (const auto& entry : files->getProperties())
                    if (fingerprint(folder.getChildFile(entry.name.toString())) != entry.value.toString())
                        return -1;

                return (int) manifest.getProperty("version", 0);
            }

            // No manifest: installed before manifests existed. Ours only if
            // every file matches what ships now - skin.json apart from its
            // version, everything else byte for byte.
            for (const auto& [name, data] : skin.files)
            {
                auto file = folder.getChildFile(name);
                if (! file.existsAsFile())
                    return -1;

                if (name == kSkinJson)
                {
                    if (! sameSkinJson(file.loadFileAsString(), data.toString()))
                        return -1;
                }
                else
                {
                    juce::MemoryBlock onDisk;
                    if (! file.loadFileAsData(onDisk) || onDisk != data)
                        return -1;
                }
            }

            return ExampleSkins::versionOf(folder);
        }

        bool writeSkin(const ExampleSkin& skin, const juce::File& folder)
        {
            if (! folder.createDirectory().wasOk())
                return false;

            // Files the previous version had and this one doesn't.
            juce::var oldManifest;
            if (juce::JSON::parse(folder.getChildFile(ExampleSkins::kManifestName).loadFileAsString(), oldManifest).wasOk())
                if (auto* oldFiles = oldManifest.getProperty("files", {}).getDynamicObject())
                    for (const auto& entry : oldFiles->getProperties())
                    {
                        auto stillShipped = false;
                        for (const auto& file : skin.files)
                            stillShipped = stillShipped || file.first == entry.name.toString();
                        if (! stillShipped)
                            folder.getChildFile(entry.name.toString()).deleteFile();
                    }

            auto* files = new juce::DynamicObject();

            for (const auto& [name, data] : skin.files)
            {
                // Atomic, like every other file this app writes.
                auto target = folder.getChildFile(name);
                juce::TemporaryFile temp(target);
                if (! temp.getFile().replaceWithData(data.getData(), data.getSize())
                     || ! temp.overwriteTargetFileWithTemporary())
                {
                    delete files;
                    return false;
                }

                files->setProperty(name, fingerprint(data.getData(), data.getSize()));
            }

            // Written LAST, so an interrupted install has no manifest and is
            // treated as someone else's folder next time - left alone rather
            // than half-trusted.
            auto* manifest = new juce::DynamicObject();
            manifest->setProperty("note", "Written by Inkwyrd Audio so it can update this skin when a new version ships. "
                                          "Edit any file here and the app will leave this skin alone from then on.");
            manifest->setProperty("name", skin.name);
            manifest->setProperty("version", skin.version);
            manifest->setProperty("files", juce::var(files));

            return folder.getChildFile(ExampleSkins::kManifestName)
                       .replaceWithText(juce::JSON::toString(juce::var(manifest), false));
        }
    }

    int ExampleSkins::versionOf(const juce::File& skinFolder)
    {
        juce::var json;
        if (! juce::JSON::parse(skinFolder.getChildFile(kSkinJson).loadFileAsString(), json).wasOk())
            return 0;
        return (int) json.getProperty("version", 0);
    }

    std::vector<ExampleSkin> ExampleSkins::fromZip(const void* data, size_t size)
    {
        std::vector<ExampleSkin> skins;
        juce::ZipFile zip(new juce::MemoryInputStream(data, size, false), true);

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* entry = zip.getEntry(i);
            auto path = entry->filename.replaceCharacter('\\', '/');
            auto slash = path.indexOfChar('/');

            // Only "<Skin>/<file>" - no loose files, no directories.
            if (slash <= 0 || path.endsWithChar('/') || path.substring(slash + 1).containsChar('/'))
                continue;

            auto skinName = path.substring(0, slash);
            auto fileName = path.substring(slash + 1);

            std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(i));
            if (stream == nullptr)
                continue;

            juce::MemoryBlock contents;
            stream->readIntoMemoryBlock(contents);

            auto it = std::find_if(skins.begin(), skins.end(),
                                   [&](const ExampleSkin& s) { return s.name == skinName; });
            if (it == skins.end())
            {
                skins.push_back({ skinName, 0, {} });
                it = std::prev(skins.end());
            }

            if (fileName == kSkinJson)
            {
                juce::var json;
                if (juce::JSON::parse(contents.toString(), json).wasOk())
                    it->version = (int) json.getProperty("version", 0);
            }

            it->files.emplace_back(fileName, std::move(contents));
        }

        std::sort(skins.begin(), skins.end(),
                  [](const ExampleSkin& a, const ExampleSkin& b) { return a.name < b.name; });
        return skins;
    }

    ExampleSkins::Outcome ExampleSkins::install(const ExampleSkin& skin, const juce::File& skinsFolder,
                                                int previouslyInstalledVersion)
    {
        auto folder = skinsFolder.getChildFile(skin.name);

        if (! folder.isDirectory())
        {
            // Installed before and gone now: the user deleted it, and that
            // has to stick. (The flag-based one-time writes this replaced
            // made the same promise.)
            if (previouslyInstalledVersion > 0)
                return Outcome::keptDeleted;

            return writeSkin(skin, folder) ? Outcome::installed : Outcome::failed;
        }

        bool hasManifest = false;
        auto current = untouchedVersion(skin, folder, hasManifest);

        if (current < 0)
            return Outcome::keptEdited;

        if (current >= skin.version && hasManifest)
            return Outcome::alreadyCurrent;

        // Older, or current but from before manifests (writing gives it one).
        if (! writeSkin(skin, folder))
            return Outcome::failed;

        return current < skin.version ? Outcome::updated : Outcome::alreadyCurrent;
    }

    juce::String ExampleSkins::describe(Outcome outcome)
    {
        switch (outcome)
        {
            case Outcome::installed:      return "installed";
            case Outcome::updated:        return "updated";
            case Outcome::alreadyCurrent: return "up to date";
            case Outcome::keptEdited:     return "left alone - it has been edited";
            case Outcome::keptDeleted:    return "left deleted";
            case Outcome::failed:         return "couldn't be written";
        }

        return {};
    }
}
