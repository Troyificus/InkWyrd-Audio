#include "SkinSprites.h"

#include "SkinSpriteNames.h"

namespace inkwyrd
{
    namespace
    {
        constexpr const char* kSheetKey = "sheet";
        constexpr const char* kSheet2xKey = "sheet2x";
        constexpr const char* kScaleKey = "scale";
        constexpr const char* kPixelArtKey = "pixelArt";
        constexpr const char* kItemsKey = "items";

        // Every state suffix any control can ask for, plus "@inactive",
        // which only the title bar uses (for a window that isn't focused).
        const char* const kAllSuffixes[] =
        {
            "", "@over", "@down", "@on", "@onover", "@ondown", "@disabled", "@focus", "@inactive"
        };

        bool readFourInts(const juce::var& value, int (&out)[4])
        {
            auto* array = value.getArray();
            if (array == nullptr || array->size() != 4)
                return false;

            for (int i = 0; i < 4; ++i)
            {
                const auto& element = array->getReference(i);
                if (! (element.isInt() || element.isInt64() || element.isDouble()))
                    return false;

                auto asDouble = (double) element;
                // Whole pixels only: a sprite edge at x = 3.5 has no
                // meaning on a pixel grid, and rounding it silently would
                // hand the author a sprite one pixel off from what they
                // wrote.
                if (asDouble != std::floor(asDouble))
                    return false;

                out[i] = (int) asDouble;
            }

            return true;
        }

        bool isComponentId(const juce::String& text)
        {
            for (auto* id : sprites::kComponentIds)
                if (text == id)
                    return true;

            return false;
        }

        SkinSprites& activeStorage()
        {
            static SkinSprites sprites;
            return sprites;
        }
    }

    const SkinSprites& activeSprites() { return activeStorage(); }

    void setActiveSprites(SkinSprites sprites) { activeStorage() = std::move(sprites); }

    juce::StringArray SkinSprites::stateSuffixes(SpriteState state)
    {
        switch (state)
        {
            case SpriteState::normal:   return { "" };
            case SpriteState::over:     return { "@over", "" };
            case SpriteState::down:     return { "@down", "@over", "" };
            case SpriteState::on:       return { "@on", "" };
            case SpriteState::onOver:   return { "@onover", "@on", "@over", "" };
            case SpriteState::onDown:   return { "@ondown", "@on", "@down", "" };
            case SpriteState::disabled: return { "@disabled", "" };
            case SpriteState::focus:    return { "@focus", "" };
        }

        return { "" };
    }

    bool SkinSprites::isKnownName(const juce::String& name)
    {
        // Split off a state suffix, if it has one.
        auto base = name;
        auto at = name.indexOfChar('@');

        if (at >= 0)
        {
            auto suffix = name.substring(at);
            base = name.substring(0, at);

            bool suffixKnown = false;
            for (auto* candidate : kAllSuffixes)
                if (suffix == candidate)
                    suffixKnown = true;

            if (! suffixKnown)
                return false;
        }

        for (auto* candidate : sprites::kBaseNames)
            if (base == candidate)
                return true;

        // "<base>.<componentId>" - a control's own version of a sprite.
        auto matchesPerControl = [&base](const char* prefix)
        {
            juce::String withDot = juce::String(prefix) + ".";
            return base.startsWith(withDot) && isComponentId(base.substring(withDot.length()));
        };

        for (auto* candidate : sprites::kBaseNames)
            if (matchesPerControl(candidate))
                return true;

        for (auto* candidate : sprites::kPerControlOnlyBases)
            if (matchesPerControl(candidate))
                return true;

        return false;
    }

    SkinSprites SkinSprites::parse(const juce::var& spritesJson, const juce::File& skinFolder,
                                    juce::StringArray& warnings)
    {
        if (spritesJson.isVoid() || spritesJson.isUndefined())
            return {};

        auto loadSheet = [&](const char* key, bool required) -> juce::Image
        {
            auto fileName = spritesJson.getProperty(key, {}).toString().trim();

            if (fileName.isEmpty())
            {
                if (required)
                    warnings.add("Sprites: no \"sheet\" named - using the drawn look.");
                return {};
            }

            auto file = skinFolder.getChildFile(fileName);

            if (! file.existsAsFile())
            {
                warnings.add("Sprites: \"" + fileName + "\" wasn't found in the skin's folder"
                              + juce::String(required ? " - using the drawn look." : " - ignored."));
                return {};
            }

            if (file.getSize() > kMaxSheetBytes)
            {
                warnings.add("Sprites: \"" + fileName + "\" is larger than 16 MB - "
                              + juce::String(required ? "using the drawn look." : "ignored."));
                return {};
            }

            auto image = juce::ImageFileFormat::loadFrom(file);
            if (! image.isValid())
                warnings.add("Sprites: couldn't read \"" + fileName + "\" as an image - "
                              + juce::String(required ? "using the drawn look." : "ignored."));

            return image;
        };

        auto sheet = loadSheet(kSheetKey, true);
        if (! sheet.isValid())
            return {};

        auto sheet2x = loadSheet(kSheet2xKey, false);
        return parseWithImages(spritesJson, sheet, sheet2x, warnings);
    }

    SkinSprites SkinSprites::parseWithImages(const juce::var& spritesJson,
                                              const juce::Image& sheet, const juce::Image& sheet2x,
                                              juce::StringArray& warnings)
    {
        SkinSprites result;

        if (spritesJson.getDynamicObject() == nullptr)
        {
            warnings.add("Sprites: \"sprites\" must be a JSON object - using the drawn look.");
            return {};
        }

        if (! sheet.isValid())
        {
            warnings.add("Sprites: no usable sprite sheet - using the drawn look.");
            return {};
        }

        if (sheet.getWidth() > kMaxSheetPixels || sheet.getHeight() > kMaxSheetPixels)
        {
            warnings.add("Sprites: the sheet is bigger than " + juce::String(kMaxSheetPixels)
                          + " pixels - using the drawn look.");
            return {};
        }

        if (spritesJson.hasProperty(kScaleKey))
        {
            auto requested = (float) (double) spritesJson.getProperty(kScaleKey, 1.0);
            result.scale = juce::jlimit(kMinScale, kMaxScale, requested);

            if (! juce::approximatelyEqual(result.scale, requested))
                warnings.add("Sprites: scale " + juce::String(requested) + " is outside "
                              + juce::String(kMinScale, 0) + "-" + juce::String(kMaxScale, 0)
                              + " - used " + juce::String(result.scale) + ".");
        }

        result.pixelArt = (bool) spritesJson.getProperty(kPixelArtKey, true);

        if (sheet2x.isValid())
        {
            if (sheet2x.getWidth() == sheet.getWidth() * 2 && sheet2x.getHeight() == sheet.getHeight() * 2)
                result.hasSheet2x = true;
            else
                warnings.add("Sprites: sheet2x must be exactly twice the size of the sheet ("
                              + juce::String(sheet.getWidth() * 2) + " x " + juce::String(sheet.getHeight() * 2)
                              + ") - ignored.");
        }

        auto* items = spritesJson.getProperty(kItemsKey, {}).getDynamicObject();
        if (items == nullptr)
        {
            warnings.add("Sprites: no \"items\" - using the drawn look.");
            return {};
        }

        auto sheetBounds = sheet.getBounds();

        for (const auto& entry : items->getProperties())
        {
            auto name = entry.name.toString();

            if (! isKnownName(name))
            {
                warnings.add("Sprites: unknown sprite \"" + name + "\" ignored.");
                continue;
            }

            int rectValues[4] {};
            if (! readFourInts(entry.value.getProperty("rect", {}), rectValues))
            {
                warnings.add("Sprites: \"" + name + "\" needs a \"rect\" of four whole numbers "
                              "[x, y, width, height] - ignored.");
                continue;
            }

            SpriteItem item;
            item.rect = { rectValues[0], rectValues[1], rectValues[2], rectValues[3] };

            if (item.rect.isEmpty() || ! sheetBounds.contains(item.rect))
            {
                warnings.add("Sprites: \"" + name + "\" is empty or reaches outside the sheet ("
                              + juce::String(sheet.getWidth()) + " x " + juce::String(sheet.getHeight())
                              + ") - ignored.");
                continue;
            }

            auto sliceValue = entry.value.getProperty("slice", {});
            if (! sliceValue.isVoid())
            {
                int s[4] {};
                if (! readFourInts(sliceValue, s) || s[0] < 0 || s[1] < 0 || s[2] < 0 || s[3] < 0)
                {
                    warnings.add("Sprites: \"" + name + "\" has a \"slice\" that isn't four whole numbers "
                                  "[left, top, right, bottom] - stretched whole instead.");
                }
                else if (s[0] + s[2] > item.rect.getWidth() || s[1] + s[3] > item.rect.getHeight())
                {
                    // Corners wider than the sprite would draw pixels from
                    // outside it.
                    warnings.add("Sprites: \"" + name + "\"'s slice is bigger than the sprite itself - "
                                  "stretched whole instead.");
                }
                else
                {
                    item.slice = juce::BorderSize<int>(s[1], s[0], s[3], s[2]);
                }
            }

            item.tile = (bool) entry.value.getProperty("tile", false);

            // Copies rather than views of the sheet, so each sprite is its
            // own small image and the sheet can be dropped.
            item.image1x = sheet.getClippedImage(item.rect).createCopy();

            if (result.hasSheet2x)
                item.image2x = sheet2x.getClippedImage(item.rect * 2).createCopy();

            result.items[name] = std::move(item);
        }

        return result;
    }

    const SpriteItem* SkinSprites::find(const juce::String& name) const
    {
        auto it = items.find(name);
        return it == items.end() ? nullptr : &it->second;
    }

    const SpriteItem* SkinSprites::findForState(const juce::String& base, SpriteState state,
                                                 const juce::String& componentId,
                                                 bool* exactState) const
    {
        if (items.empty())
            return nullptr;

        auto suffixes = stateSuffixes(state);

        juce::StringArray bases;
        if (componentId.isNotEmpty())
            bases.add(base + "." + componentId);
        bases.add(base);

        for (const auto& candidateBase : bases)
        {
            for (int i = 0; i < suffixes.size(); ++i)
            {
                if (auto* item = find(candidateBase + suffixes[i]))
                {
                    if (exactState != nullptr)
                        *exactState = (i == 0);
                    return item;
                }
            }
        }

        return nullptr;
    }

    const juce::Image& SkinSprites::pickImage(const SpriteItem& item, const juce::Graphics& g) const
    {
        if (! hasSheet2x || ! item.image2x.isValid())
            return item.image1x;

        // The hi-res sheet adds real detail only once a sheet pixel covers
        // more than one physical pixel - at 100% Windows scaling with a
        // skin scale of 1 it would just be downsampled back again.
        auto physical = g.getInternalContext().getPhysicalPixelScaleFactor() * scale;
        return physical >= 1.5f ? item.image2x : item.image1x;
    }

    namespace
    {
        void drawPiece(juce::Graphics& g, const juce::Image& image,
                        juce::Rectangle<int> source, juce::Rectangle<int> dest)
        {
            if (source.isEmpty() || dest.isEmpty())
                return;

            g.drawImage(image, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
                         source.getX(), source.getY(), source.getWidth(), source.getHeight());
        }

        // Repeats `source` at `tileSize` across `dest`, clipping the last
        // row and column - the Winamp playlist-frame behaviour, where
        // edges grow by repeating rather than smearing.
        void drawTiledPiece(juce::Graphics& g, const juce::Image& image,
                             juce::Rectangle<int> source, juce::Rectangle<int> dest,
                             juce::Point<int> tileSize)
        {
            if (source.isEmpty() || dest.isEmpty() || tileSize.x <= 0 || tileSize.y <= 0)
                return;

            juce::Graphics::ScopedSaveState saved(g);
            g.reduceClipRegion(dest);

            for (int y = dest.getY(); y < dest.getBottom(); y += tileSize.y)
                for (int x = dest.getX(); x < dest.getRight(); x += tileSize.x)
                    drawPiece(g, image, source, { x, y, tileSize.x, tileSize.y });
        }
    }

    void SkinSprites::drawNineSlice(juce::Graphics& g, const SpriteItem& item,
                                     juce::Rectangle<float> destFloat, float alpha) const
    {
        const auto& image = pickImage(item, g);
        auto dest = destFloat.getSmallestIntegerContainer();

        if (! image.isValid() || dest.isEmpty())
            return;

        // Sheet pixels per 1x pixel of this image: 1, or 2 for the hi-res.
        auto res = image.getWidth() / item.rect.getWidth();

        juce::Graphics::ScopedSaveState saved(g);
        g.setImageResamplingQuality(pixelArt ? juce::Graphics::lowResamplingQuality
                                              : juce::Graphics::highResamplingQuality);
        g.setOpacity(alpha);

        const auto& slice = item.slice;

        // Source columns and rows, in this image's own pixels.
        int sx[4] = { 0, slice.getLeft() * res, image.getWidth() - slice.getRight() * res, image.getWidth() };
        int sy[4] = { 0, slice.getTop() * res, image.getHeight() - slice.getBottom() * res, image.getHeight() };

        // Corner sizes on screen. When the destination is smaller than the
        // corners, shrink them in proportion rather than letting opposite
        // corners overlap - a short button still gets both its ends.
        auto left = (float) slice.getLeft() * scale, right = (float) slice.getRight() * scale;
        auto top = (float) slice.getTop() * scale, bottom = (float) slice.getBottom() * scale;

        if (left + right > (float) dest.getWidth() && left + right > 0.0f)
        {
            auto f = (float) dest.getWidth() / (left + right);
            left *= f;
            right *= f;
        }

        if (top + bottom > (float) dest.getHeight() && top + bottom > 0.0f)
        {
            auto f = (float) dest.getHeight() / (top + bottom);
            top *= f;
            bottom *= f;
        }

        int dx[4] = { dest.getX(), dest.getX() + juce::roundToInt(left),
                      dest.getRight() - juce::roundToInt(right), dest.getRight() };
        int dy[4] = { dest.getY(), dest.getY() + juce::roundToInt(top),
                      dest.getBottom() - juce::roundToInt(bottom), dest.getBottom() };

        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                juce::Rectangle<int> source(sx[col], sy[row], sx[col + 1] - sx[col], sy[row + 1] - sy[row]);
                juce::Rectangle<int> target(dx[col], dy[row], dx[col + 1] - dx[col], dy[row + 1] - dy[row]);

                auto isCorner = (row != 1 && col != 1);

                if (item.tile && ! isCorner)
                {
                    // A tile keeps its real size along the direction it
                    // repeats in, and fills the slice across the other.
                    juce::Point<int> tileSize(
                        col == 1 ? juce::jmax(1, juce::roundToInt((float) source.getWidth() / (float) res * scale))
                                 : target.getWidth(),
                        row == 1 ? juce::jmax(1, juce::roundToInt((float) source.getHeight() / (float) res * scale))
                                 : target.getHeight());

                    drawTiledPiece(g, image, source, target, tileSize);
                }
                else
                {
                    drawPiece(g, image, source, target);
                }
            }
        }
    }

    juce::Rectangle<float> SkinSprites::naturalBounds(const SpriteItem& item) const
    {
        return { (float) item.rect.getWidth() * scale, (float) item.rect.getHeight() * scale };
    }

    void SkinSprites::drawNatural(juce::Graphics& g, const SpriteItem& item,
                                   juce::Rectangle<float> area, float alpha) const
    {
        const auto& image = pickImage(item, g);
        if (! image.isValid() || area.isEmpty())
            return;

        // Largest scale that fits, preferring whole steps down from the
        // skin's own: pixel art shrunk by 2 -> 1 stays crisp, shrunk by
        // 2 -> 1.37 does not.
        auto w = (float) item.rect.getWidth(), h = (float) item.rect.getHeight();
        auto s = scale;

        while (s > 1.0f && (w * s > area.getWidth() || h * s > area.getHeight()))
            s -= 1.0f;

        if (w * s > area.getWidth() || h * s > area.getHeight())
            s = juce::jmin(area.getWidth() / w, area.getHeight() / h);

        auto target = juce::Rectangle<float>(w * s, h * s).withCentre(area.getCentre());
        auto snapped = juce::Rectangle<int>(juce::roundToInt(target.getX()), juce::roundToInt(target.getY()),
                                             juce::roundToInt(target.getWidth()),
                                             juce::roundToInt(target.getHeight()));

        juce::Graphics::ScopedSaveState saved(g);
        g.setImageResamplingQuality(pixelArt ? juce::Graphics::lowResamplingQuality
                                              : juce::Graphics::highResamplingQuality);
        g.setOpacity(alpha);
        drawPiece(g, image, image.getBounds(), snapped);
    }
}
