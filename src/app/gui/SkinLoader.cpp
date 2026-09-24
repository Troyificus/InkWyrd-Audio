#include "SkinLoader.h"

namespace inkwyrd
{
    namespace
    {
        constexpr const char* kSchemaKey = "schemaVersion";
        constexpr const char* kNameKey = "name";
        constexpr const char* kColoursKey = "colours";
        constexpr const char* kFontsKey = "fonts";
        constexpr const char* kMetricsKey = "metrics";
        constexpr const char* kLogoKey = "logo";
        constexpr const char* kFontFilesKey = "fontFiles";

        // A font file bigger than this is almost certainly not a font - and
        // it would be read into memory whole.
        constexpr juce::int64 kMaxFontFileBytes = 8 * 1024 * 1024;

        // A skin's whole colour vocabulary, in one table, so parsing and
        // exporting can't drift apart and the README has one list to
        // document.
        struct ColourField
        {
            const char* key;
            juce::Colour theme::Palette::* member;
        };

        const ColourField kColourFields[] =
        {
            { "background",     &theme::Palette::background },
            { "panelDeep",      &theme::Palette::panelDeep },
            { "panel",          &theme::Palette::panel },
            { "panelRaised",    &theme::Palette::panelRaised },
            { "titleBar",       &theme::Palette::titleBar },
            { "titleBarText",   &theme::Palette::titleBarText },
            { "titleBarSubtle", &theme::Palette::titleBarSubtle },
            { "text",           &theme::Palette::text },
            { "textDim",        &theme::Palette::textDim },
            { "accent",         &theme::Palette::accent },
            { "accentSoft",     &theme::Palette::accentSoft },
            { "outline",        &theme::Palette::outline },
            { "outlineFaint",   &theme::Palette::outlineFaint },
            { "warning",        &theme::Palette::warning },
            { "danger",         &theme::Palette::danger }
        };

        struct FontField
        {
            const char* key;
            juce::String theme::Palette::* member;
        };

        const FontField kFontFields[] =
        {
            { "title",  &theme::Palette::titleFontName },
            { "label",  &theme::Palette::labelFontName },
            { "digits", &theme::Palette::digitFontName }
        };

        bool isHex(const juce::String& text)
        {
            if (text.isEmpty())
                return false;

            return text.containsOnly("0123456789abcdefABCDEF");
        }
    }

    bool SkinLoader::parseColour(const juce::String& text, juce::Colour& result)
    {
        auto trimmed = text.trim();
        if (trimmed.startsWithChar('#'))
            trimmed = trimmed.substring(1);

        if (! isHex(trimmed))
            return false;

        // Six digits means opaque; eight carries its own alpha. Any other
        // length is a typo, and guessing at it would silently give the
        // author a colour they didn't write.
        if (trimmed.length() == 6)
        {
            result = juce::Colour((juce::uint32) (0xff000000u | trimmed.getHexValue32()));
            return true;
        }

        if (trimmed.length() == 8)
        {
            result = juce::Colour((juce::uint32) trimmed.getHexValue64());
            return true;
        }

        return false;
    }

    juce::String SkinLoader::colourToString(juce::Colour colour)
    {
        // Alpha always written, so an exported skin reads back identically
        // whether or not a colour happened to be translucent.
        return "#" + colour.toDisplayString(true).toLowerCase();
    }

    juce::File SkinLoader::getDefaultFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Inkwyrd Audio")
                   .getChildFile("skins");
    }

    juce::Array<juce::File> SkinLoader::findSkinFolders(const juce::File& skinsFolder)
    {
        juce::Array<juce::File> found;

        if (! skinsFolder.isDirectory())
            return found;

        for (const auto& entry : juce::RangedDirectoryIterator(skinsFolder, false, "*",
                                                                juce::File::findDirectories))
            if (entry.getFile().getChildFile(kSkinFileName).existsAsFile())
                found.add(entry.getFile());

        std::sort(found.begin(), found.end(), [](const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
        });

        return found;
    }

    SkinLoadResult SkinLoader::loadFromFolder(const juce::File& skinFolder)
    {
        SkinLoadResult result;
        result.name = skinFolder.getFileName();

        auto file = skinFolder.getChildFile(kSkinFileName);
        if (! file.existsAsFile())
        {
            result.message = "No " + juce::String(kSkinFileName) + " in " + skinFolder.getFullPathName();
            return result;
        }

        juce::var parsed;
        auto status = juce::JSON::parse(file.loadFileAsString(), parsed);

        if (! status.wasOk())
        {
            result.message = "Couldn't read " + file.getFileName() + ": " + status.getErrorMessage();
            return result;
        }

        return parse(parsed, skinFolder);
    }

    SkinLoadResult SkinLoader::parse(const juce::var& json, const juce::File& skinFolder)
    {
        SkinLoadResult result;
        result.name = skinFolder.getFileName();

        auto* object = json.getDynamicObject();
        if (object == nullptr)
        {
            result.message = "A skin file must be a JSON object.";
            return result;
        }

        auto schema = (int) json.getProperty(kSchemaKey, 1);
        if (schema > kCurrentSchemaVersion)
        {
            result.message = "This skin was written for a newer version of Inkwyrd Audio "
                              "(schemaVersion " + juce::String(schema) + ").";
            return result;
        }

        auto name = json.getProperty(kNameKey, {}).toString().trim();
        if (name.isNotEmpty())
            result.name = name;

        result.version = juce::jmax(0, (int) json.getProperty("version", 0));

        // Colours. Anything absent keeps the built-in value, and anything
        // unreadable says so rather than being silently ignored.
        if (auto* colours = json.getProperty(kColoursKey, {}).getDynamicObject())
        {
            for (const auto& entry : colours->getProperties())
            {
                auto key = entry.name.toString();
                const ColourField* field = nullptr;

                for (const auto& candidate : kColourFields)
                    if (key == candidate.key)
                        field = &candidate;

                if (field == nullptr)
                {
                    result.warnings.add("Unknown colour \"" + key + "\" ignored.");
                    continue;
                }

                juce::Colour colour;
                if (parseColour(entry.value.toString(), colour))
                    result.palette.*(field->member) = colour;
                else
                    result.warnings.add("Colour \"" + key + "\" isn't a hex value like #rrggbb - "
                                         "kept the built-in colour.");
            }
        }

        if (auto* fonts = json.getProperty(kFontsKey, {}).getDynamicObject())
        {
            for (const auto& entry : fonts->getProperties())
            {
                auto key = entry.name.toString();
                const FontField* field = nullptr;

                for (const auto& candidate : kFontFields)
                    if (key == candidate.key)
                        field = &candidate;

                if (field == nullptr)
                {
                    result.warnings.add("Unknown font \"" + key + "\" ignored.");
                    continue;
                }

                auto family = entry.value.toString().trim();
                if (family.isNotEmpty())
                    result.palette.*(field->member) = family;
            }
        }

        if (auto* metrics = json.getProperty(kMetricsKey, {}).getDynamicObject())
        {
            if (metrics->hasProperty("cornerRadius"))
                result.palette.cornerRadius =
                    juce::jlimit(0.0f, 24.0f, (float) (double) metrics->getProperty("cornerRadius"));

            if (metrics->hasProperty("titleBarHeight"))
            {
                auto requested = (int) metrics->getProperty("titleBarHeight");
                auto clamped = juce::jlimit(theme::Palette::kMinTitleBarHeight,
                                             theme::Palette::kMaxTitleBarHeight,
                                             requested);

                // A two-pixel title bar leaves a window that can't be
                // dragged, closed or told apart from its neighbour.
                if (clamped != requested)
                    result.warnings.add("titleBarHeight " + juce::String(requested) + " is outside "
                                         + juce::String(theme::Palette::kMinTitleBarHeight) + "-"
                                         + juce::String(theme::Palette::kMaxTitleBarHeight)
                                         + " - used " + juce::String(clamped) + ".");

                result.palette.titleBarHeight = clamped;
            }
        }

        auto logoName = json.getProperty(kLogoKey, {}).toString().trim();
        if (logoName.isNotEmpty())
        {
            auto logo = skinFolder.getChildFile(logoName);

            if (logo.existsAsFile())
                result.logoFile = logo;
            else
                result.warnings.add("Logo \"" + logoName + "\" wasn't found - using the drawn mark.");
        }

        if (auto* fontFiles = json.getProperty(kFontFilesKey, {}).getArray())
        {
            for (const auto& entry : *fontFiles)
            {
                auto fileName = entry.toString().trim();
                auto file = skinFolder.getChildFile(fileName);

                if (fileName.isEmpty() || ! file.existsAsFile())
                    result.warnings.add("Font file \"" + fileName + "\" wasn't found - ignored.");
                else if (! file.hasFileExtension("ttf;otf"))
                    result.warnings.add("Font file \"" + fileName + "\" isn't a .ttf or .otf - ignored.");
                else if (file.getSize() > kMaxFontFileBytes)
                    result.warnings.add("Font file \"" + fileName + "\" is larger than 8 MB - ignored.");
                else
                    result.fontFiles.add(file);
            }
        }

        // Sprites never fail the skin: its colours and fonts are still
        // worth having if the sheet is missing or wrong.
        result.sprites = SkinSprites::parse(json.getProperty(SkinSprites::kSpritesKey, {}),
                                             skinFolder, result.warnings);

        result.ok = true;
        return result;
    }

    juce::var SkinLoader::toVar(const theme::Palette& palette, const juce::String& name,
                                 const juce::String& logoFileName, int version)
    {
        auto* colours = new juce::DynamicObject();
        for (const auto& field : kColourFields)
            colours->setProperty(field.key, colourToString(palette.*(field.member)));

        auto* fonts = new juce::DynamicObject();
        for (const auto& field : kFontFields)
            fonts->setProperty(field.key, palette.*(field.member));

        auto* metrics = new juce::DynamicObject();
        metrics->setProperty("cornerRadius", palette.cornerRadius);
        metrics->setProperty("titleBarHeight", palette.titleBarHeight);

        auto* root = new juce::DynamicObject();
        root->setProperty(kSchemaKey, kCurrentSchemaVersion);
        root->setProperty(kNameKey, name);
        root->setProperty("version", version);
        root->setProperty(kColoursKey, juce::var(colours));
        root->setProperty(kFontsKey, juce::var(fonts));
        root->setProperty(kMetricsKey, juce::var(metrics));

        if (logoFileName.isNotEmpty())
            root->setProperty(kLogoKey, logoFileName);

        return juce::var(root);
    }

    bool SkinLoader::writeToFolder(const theme::Palette& palette, const juce::String& name,
                                    const juce::File& skinFolder, juce::String& errorMessage)
    {
        auto created = skinFolder.createDirectory();
        if (! created.wasOk())
        {
            errorMessage = created.getErrorMessage();
            return false;
        }

        auto file = skinFolder.getChildFile(kSkinFileName);
        auto json = juce::JSON::toString(toVar(palette, name), false);

        // Atomic, like every other file this app writes: an interrupted
        // write leaves the previous skin rather than half a file.
        juce::TemporaryFile temp(file);
        if (! temp.getFile().replaceWithText(json) || ! temp.overwriteTargetFileWithTemporary())
        {
            errorMessage = "Couldn't write " + file.getFullPathName();
            return false;
        }

        return true;
    }
}
