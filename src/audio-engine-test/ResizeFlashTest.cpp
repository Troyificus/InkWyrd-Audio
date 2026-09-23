// INKWYRD_RESIZETEST=1: measures the white "ghost" a window shows when it
// grows, instead of judging it from screen recordings.
//
// Three fixes that sounded right didn't work (a faster paint, a dark
// WM_ERASEBKGND fill, a dark window-class brush), each needing a manual
// drag and a recording to find out. This opens a plain dark JUCE window,
// grows it from code, and grabs the screen straight after each step -
// before the app has had a chance to paint, and again after - counting
// light pixels in the strip that was just exposed. Each candidate fix is
// a variant, so they can be compared by number.
//
// A programmatic SetWindowPos is not Windows' own drag loop, but what the
// compositor shows for a freshly grown, not-yet-painted window is the
// same question either way - and that is what the white is.

#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <commctrl.h>
#endif

namespace
{
#if JUCE_WINDOWS
    const juce::Colour kDark { 0xff0f1c15 };

    struct DarkContent : juce::Component
    {
        void paint(juce::Graphics& g) override { g.fillAll(kDark); }
    };

    struct TestWindow : juce::DocumentWindow
    {
        explicit TestWindow(bool nativeTitleBar)
            : DocumentWindow("Resize flash test", kDark, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(nativeTitleBar);
            setResizable(true, false);
            setContentNonOwned(&content, false);
        }

        DarkContent content;
    };

    LRESULT CALLBACK eraseDarkProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
    {
        if (message == WM_ERASEBKGND)
        {
            RECT client {};
            GetClientRect(hwnd, &client);
            auto brush = CreateSolidBrush(RGB(0x0f, 0x1c, 0x15));
            FillRect((HDC) wParam, &client, brush);
            DeleteObject(brush);
            return 1;
        }

        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    // Fraction of pixels in `area` (screen coords, physical) that are light
    // - anything near white. The window itself is near-black, so light
    // pixels there can only be the ghost.
    double lightFraction(RECT area)
    {
        auto w = area.right - area.left, h = area.bottom - area.top;
        if (w <= 0 || h <= 0)
            return 0.0;

        auto screen = GetDC(nullptr);
        auto memDc = CreateCompatibleDC(screen);

        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        auto bitmap = CreateDIBSection(memDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        auto old = SelectObject(memDc, bitmap);
        BitBlt(memDc, 0, 0, w, h, screen, area.left, area.top, SRCCOPY | CAPTUREBLT);

        int light = 0;
        auto* px = static_cast<const juce::uint8*>(bits);
        for (int i = 0; i < w * h; ++i)
            if (px[i * 4] > 180 && px[i * 4 + 1] > 180 && px[i * 4 + 2] > 180)
                ++light;

        SelectObject(memDc, old);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        ReleaseDC(nullptr, screen);

        return (double) light / (double) (w * h);
    }

    // Saves the screen area around the window, so a number can be checked
    // against what was actually on screen.
    void saveGrab(RECT area, const juce::File& file)
    {
        auto w = area.right - area.left, h = area.bottom - area.top;
        juce::Image image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        auto screen = GetDC(nullptr);
        auto memDc = CreateCompatibleDC(screen);
        auto bitmap = CreateCompatibleBitmap(screen, w, h);
        auto old = SelectObject(memDc, bitmap);
        BitBlt(memDc, 0, 0, w, h, screen, area.left, area.top, SRCCOPY | CAPTUREBLT);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                auto c = GetPixel(memDc, x, y);
                image.setPixelAt(x, y, juce::Colour(GetRValue(c), GetGValue(c), GetBValue(c)));
            }
        SelectObject(memDc, old);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        ReleaseDC(nullptr, screen);

        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat().writeImageToStream(image, out);
    }

    struct Result { double beforePaint = 0.0, afterPaint = 0.0; int stepsWithWhite = 0, steps = 0; };

    Result runVariant(const juce::String& name, bool nativeTitleBar, int renderer, bool classBrush, bool eraseFill)
    {
        TestWindow window(nativeTitleBar);
        window.setBounds(80, 80, 360, 260);
        window.setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);

        auto hwnd = (HWND) window.getWindowHandle();
        Result result;

        if (hwnd == nullptr)
        {
            std::cout << "  " << name << ": no window handle" << std::endl;
            return result;
        }

        if (auto* peer = window.getPeer())
            if (renderer >= 0 && renderer < peer->getAvailableRenderingEngines().size())
                peer->setCurrentRenderingEngine(renderer);

        HBRUSH brush = nullptr;
        LONG_PTR previousBrush = 0;
        if (classBrush)
        {
            brush = CreateSolidBrush(RGB(0x0f, 0x1c, 0x15));
            previousBrush = SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR) brush);
        }

        if (eraseFill)
            SetWindowSubclass(hwnd, eraseDarkProc, 7, 0);

        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);

        constexpr int stepPx = 40;
        for (int step = 0; step < 12; ++step)
        {
            RECT before {};
            GetWindowRect(hwnd, &before);

            // Grow right and down, like dragging the bottom-right corner.
            SetWindowPos(hwnd, nullptr, 0, 0,
                         before.right - before.left + stepPx, before.bottom - before.top + stepPx,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

            // The newly exposed strip along the right edge.
            RECT strip { before.right, before.top + 40, before.right + stepPx - 2, before.bottom - 10 };

            // Several grabs across the next ~60ms WITHOUT pumping messages -
            // what the compositor shows before the app can paint - then
            // pump and grab again.
            double worstBefore = 0.0;
            for (int grab = 0; grab < 4; ++grab)
            {
                Sleep(16);
                auto f = lightFraction(strip);
                if (f > worstBefore && f > result.beforePaint)
                {
                    RECT whole {};
                    GetWindowRect(hwnd, &whole);
                    whole.left -= 20; whole.top -= 20; whole.right += 20; whole.bottom += 20;
                    saveGrab(whole, juce::File::getSpecialLocation(juce::File::tempDirectory)
                                        .getChildFile("inkwyrd-resize-" + name.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") + ".png"));
                }
                worstBefore = juce::jmax(worstBefore, f);
            }

            juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
            auto after = lightFraction(strip);

            result.beforePaint = juce::jmax(result.beforePaint, worstBefore);
            result.afterPaint = juce::jmax(result.afterPaint, after);
            if (worstBefore > 0.2 || after > 0.2)
                ++result.stepsWithWhite;
            ++result.steps;
        }

        if (eraseFill)
            RemoveWindowSubclass(hwnd, eraseDarkProc, 7);
        if (classBrush)
        {
            SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, previousBrush);
            DeleteObject(brush);
        }

        std::cout << "  " << name.paddedRight(' ', 44)
                  << " white before paint: " << juce::String(result.beforePaint * 100.0, 0).paddedLeft(' ', 3) << "%"
                  << "   after paint: " << juce::String(result.afterPaint * 100.0, 0).paddedLeft(' ', 3) << "%"
                  << "   steps with white: " << result.stepsWithWhite << "/" << result.steps << std::endl;

        window.setVisible(false);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        return result;
    }
#endif
}

int runResizeFlashTest()
{
#if JUCE_WINDOWS
    juce::ScopedJuceInitialiser_GUI gui;

    std::cout << "Resize flash test - a small window will grow in the top-left corner." << std::endl;
    std::cout << "Renderers: 0 = software, 1 = Direct2D" << std::endl;

    for (int renderer : { 1, 0 })
    {
        auto r = juce::String(renderer == 1 ? "Direct2D" : "software");
        runVariant(r + ", JUCE title bar, no fix",                  false, renderer, false, false);
        runVariant(r + ", JUCE title bar, class brush",             false, renderer, true,  false);
        runVariant(r + ", JUCE title bar, erase fill",              false, renderer, false, true);
        runVariant(r + ", JUCE title bar, brush + erase",           false, renderer, true,  true);
        runVariant(r + ", NATIVE title bar, no fix",                true,  renderer, false, false);
    }

    return 0;
#else
    return 0;
#endif
}
