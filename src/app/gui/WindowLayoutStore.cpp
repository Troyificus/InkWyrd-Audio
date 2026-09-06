#include "WindowLayoutStore.h"

namespace
{
    constexpr const char* kKeyX = "x";
    constexpr const char* kKeyY = "y";
    constexpr const char* kKeyW = "w";
    constexpr const char* kKeyH = "h";
    constexpr const char* kKeyVisible = "visible";
}

namespace WindowLayoutStore
{
    std::map<juce::String, WindowState> fromJson(const juce::String& json)
    {
        std::map<juce::String, WindowState> result;
        if (json.isEmpty())
            return result;

        auto parsed = juce::JSON::parse(json);
        auto* root = parsed.getDynamicObject();
        if (root == nullptr)
            return result;

        for (auto& property : root->getProperties())
        {
            auto* windowObject = property.value.getDynamicObject();
            if (windowObject == nullptr)
                continue; // one malformed entry doesn't invalidate the rest

            WindowState state;
            state.bounds = { (int) windowObject->getProperty(kKeyX),
                              (int) windowObject->getProperty(kKeyY),
                              (int) windowObject->getProperty(kKeyW),
                              (int) windowObject->getProperty(kKeyH) };
            state.visible = (bool) windowObject->getProperty(kKeyVisible);

            if (state.bounds.getWidth() > 0 && state.bounds.getHeight() > 0)
                result[property.name.toString()] = state;
        }

        return result;
    }

    juce::String toJson(const std::map<juce::String, WindowState>& states)
    {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();

        for (auto& [name, state] : states)
        {
            juce::DynamicObject::Ptr windowObject = new juce::DynamicObject();
            windowObject->setProperty(kKeyX, state.bounds.getX());
            windowObject->setProperty(kKeyY, state.bounds.getY());
            windowObject->setProperty(kKeyW, state.bounds.getWidth());
            windowObject->setProperty(kKeyH, state.bounds.getHeight());
            windowObject->setProperty(kKeyVisible, state.visible);

            root->setProperty(name, juce::var(windowObject.get()));
        }

        return juce::JSON::toString(juce::var(root.get()), false);
    }

    juce::Rectangle<int> clampToNearestDisplay(juce::Rectangle<int> bounds)
    {
        auto& displays = juce::Desktop::getInstance().getDisplays();

        for (auto& display : displays.displays)
            if (display.userArea.intersects(bounds))
                return bounds;

        const juce::Displays::Display* target = displays.getDisplayForPoint(bounds.getCentre());
        if (target == nullptr)
            target = displays.getPrimaryDisplay();
        if (target == nullptr && ! displays.displays.isEmpty())
            target = &displays.displays.getReference(0);
        if (target == nullptr)
            return bounds; // no display info at all - nothing sensible to clamp against

        auto area = target->userArea;
        auto width = juce::jmin(bounds.getWidth(), area.getWidth());
        auto height = juce::jmin(bounds.getHeight(), area.getHeight());
        auto x = juce::jlimit(area.getX(), area.getRight() - width, bounds.getX());
        auto y = juce::jlimit(area.getY(), area.getBottom() - height, bounds.getY());

        return { x, y, width, height };
    }

    juce::Rectangle<int> primaryDisplayArea()
    {
        auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        return primary != nullptr ? primary->userArea : juce::Rectangle<int>(0, 0, 1920, 1080);
    }
}
