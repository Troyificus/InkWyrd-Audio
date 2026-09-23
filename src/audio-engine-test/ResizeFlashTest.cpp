// Measures the white "ghost" a window shows at its growing edges while
// being resized, instead of judging it from screen recordings.
//
//   INKWYRD_RESIZETEST=1        grows a test window from code
//   INKWYRD_RESIZETEST=drag     REAL corner drags of a test window, one per
//                               candidate fix (moves the actual mouse)
//   INKWYRD_RESIZETEST=dragapp  the same drag on a running Inkwyrd window
//                               (INKWYRD_DRAG_TITLE, default the Player)
//
// Each grabs the screen after every step and counts light pixels in the
// strip the window just grew into, against a magenta backdrop - so white
// can only be the ghost - and saves the worst frame to %TEMP%.
//
// WHAT IT FOUND (see applyCompositorTheme() in DetachableWindow.cpp): the
// white is Windows 11's compositor filling not-yet-drawn area in the
// window's light/dark theme colour, and an unset theme means light.
// DWMWA_USE_IMMERSIVE_DARK_MODE took the real Player from 10/24 moves with
// white to 0/24, in both renderers. A dark class brush, a dark
// WM_ERASEBKGND fill and the software renderer each measured as no fix.
//
// Lessons from getting the measurement right, each of which produced a
// convincing wrong answer first: measure against a known backdrop (a white
// web page behind the window read as 100% ghost); save the SAME frame you
// measured (the ghost lasts about one frame, a second grab missed it); and
// slow the test window's paint down, or a trivially fast window shows the
// ghost too rarely to compare fixes.

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <iostream>
#include <thread>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <commctrl.h>
 #include <dwmapi.h>
 #pragma comment(lib, "dwmapi.lib")
#endif

namespace
{
#if JUCE_WINDOWS
    const juce::Colour kDark { 0xff0f1c15 };

    // Slow painting stands in for a real, busy window: a plain dark window
    // paints so fast the ghost is almost never on screen long enough to
    // grab (1 move in 16), while Inkwyrd's windows show it constantly.
    std::atomic<int> paintDelayMs { 0 };

    struct DarkContent : juce::Component
    {
        void paint(juce::Graphics& g) override
        {
            if (auto delay = paintDelayMs.load())
                juce::Thread::sleep(delay);
            g.fillAll(kDark);
        }
    };

    struct TestWindow : juce::DocumentWindow
    {
        explicit TestWindow(bool nativeTitleBar, bool cornerGrip = false)
            : DocumentWindow("Resize flash test", kDark, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(nativeTitleBar);
            // cornerGrip = true is what Inkwyrd's windows use: JUCE's own
            // bottom-right resize grip rather than a resizable border.
            setResizable(true, cornerGrip);
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

    // ---------------------------------------------------------------------
    // A REAL drag of the bottom-right corner, through Windows' own resize
    // loop - the path a code-driven SetWindowPos never takes, and the one
    // where the software renderer still showed white by hand.
    //
    // Windows' resize loop runs INSIDE the window's thread once the button
    // goes down, so the mouse and the screen grabs are driven from a second
    // thread while the main thread keeps the window's messages flowing.
    // Moves the real cursor: only ever run with the user's go-ahead.

    void sendMouse(DWORD flags, int x = 0, int y = 0)
    {
        INPUT input {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = flags;

        if (flags & MOUSEEVENTF_MOVE)
        {
            auto vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
            auto vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            input.mi.dx = (LONG) (((x - vx) * 65535.0) / (vw - 1));
            input.mi.dy = (LONG) (((y - vy) * 65535.0) / (vh - 1));
            input.mi.dwFlags |= MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        }

        SendInput(1, &input, sizeof(INPUT));
    }

    // A stand-in for Inkwyrd's snapping hook (DetachableWindow.cpp): answers
    // WM_SIZING / WM_MOVING itself and never passes them on to JUCE.
    LRESULT CALLBACK snapHookProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
    {
        if (message == WM_SIZING || message == WM_MOVING)
            return TRUE;

        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    // One screen capture of `area`, as a juce::Image - so the frame that is
    // MEASURED is the frame that gets saved. (Measuring one grab and saving
    // a second a few ms later lost the evidence: the flash is ~one frame.)
    juce::Image grabScreen(RECT area)
    {
        auto w = area.right - area.left, h = area.bottom - area.top;
        juce::Image image(juce::Image::RGB, juce::jmax(1, (int) w), juce::jmax(1, (int) h), false, juce::SoftwareImageType());
        if (w <= 0 || h <= 0)
            return image;

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

        juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
        auto* px = static_cast<const juce::uint8*>(bits);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                auto* p = px + (y * w + x) * 4;
                data.setPixelColour(x, y, juce::Colour(p[2], p[1], p[0]));
            }

        SelectObject(memDc, old);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        ReleaseDC(nullptr, screen);
        return image;
    }

    double lightFractionIn(const juce::Image& image, juce::Rectangle<int> r)
    {
        r = r.getIntersection(image.getBounds());
        if (r.isEmpty())
            return 0.0;

        int light = 0;
        for (int y = r.getY(); y < r.getBottom(); ++y)
            for (int x = r.getX(); x < r.getRight(); ++x)
            {
                auto c = image.getPixelAt(x, y);
                if (c.getRed() > 180 && c.getGreen() > 180 && c.getBlue() > 180)
                    ++light;
            }

        return (double) light / (double) r.getWidth() / (double) r.getHeight();
    }

    struct DragResult { double worst = 0.0; int movesWithWhite = 0, moves = 0, grew = 0; };

    // Solid magenta behind the test window. Without it, the "new strip"
    // measured whatever was behind the window - on the first run a white web
    // page, read as 100% ghost. Windows reports the grown rectangle a frame
    // before the compositor shows it, so that gap shows the backdrop: with a
    // magenta backdrop, magenta = not shown yet, white = a real ghost.
    struct Backdrop : juce::Component
    {
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xffff00ff)); }
    };

    // Presses the window's bottom-right corner and drags it outwards in
    // `moves` steps, grabbing the screen after each and measuring light
    // pixels in the strips the window grew into. The caller's thread keeps
    // pumping messages meanwhile - it has to, since Windows' resize loop
    // runs on the thread that owns the window, which may be this one.
    DragResult dragCornerAndMeasure(HWND hwnd, const juce::String& name, int moves, RECT& start)
    {
        DragResult result;
        GetWindowRect(hwnd, &start);
        POINT savedCursor {};
        GetCursorPos(&savedCursor);

        std::atomic<bool> done { false };
        auto grabFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("inkwyrd-drag-" + name.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") + ".png");

        std::thread driver([&]
        {
            // Just inside the bottom-right corner - the resize grip, or the
            // border's corner when there is no grip.
            int x = start.right - 6, y = start.bottom - 6;
            sendMouse(MOUSEEVENTF_MOVE, x, y);
            Sleep(250);
            sendMouse(MOUSEEVENTF_LEFTDOWN);
            Sleep(250);

            RECT previous = start;

            for (int move = 0; move < moves; ++move)
            {
                x += 384 / moves;
                y += 288 / moves;
                sendMouse(MOUSEEVENTF_MOVE, x, y);

                // Grab the freshly exposed strips a few times over the next
                // ~50ms: the window grows as soon as Windows processes the
                // move, and the question is what shows there before paint.
                double worstThisMove = 0.0;
                for (int grab = 0; grab < 4; ++grab)
                {
                    Sleep(12);
                    RECT now {};
                    GetWindowRect(hwnd, &now);

                    RECT whole { now.left - 20, now.top - 20, now.right + 20, now.bottom + 20 };
                    auto frame = grabScreen(whole);

                    // Strips in the captured image's own coordinates.
                    auto ox = whole.left, oy = whole.top;
                    juce::Rectangle<int> right  (previous.right - ox, now.top + 50 - oy,
                                                 now.right - previous.right - 1, now.bottom - now.top - 51);
                    juce::Rectangle<int> bottom (now.left + 1 - ox, previous.bottom - oy,
                                                 now.right - now.left - 2, now.bottom - previous.bottom - 1);
                    auto f = juce::jmax(lightFractionIn(frame, right), lightFractionIn(frame, bottom));

                    if (f > result.worst)
                    {
                        grabFile.deleteFile();
                        juce::FileOutputStream out(grabFile);
                        juce::PNGImageFormat().writeImageToStream(frame, out);
                    }

                    worstThisMove = juce::jmax(worstThisMove, f);
                    result.worst = juce::jmax(result.worst, f);
                }

                RECT now {};
                GetWindowRect(hwnd, &now);
                if (now.right > previous.right)
                    ++result.grew;
                previous = now;

                if (worstThisMove > 0.2)
                    ++result.movesWithWhite;
                ++result.moves;
            }

            sendMouse(MOUSEEVENTF_LEFTUP);
            Sleep(150);
            SetCursorPos(savedCursor.x, savedCursor.y);
            done = true;
        });

        while (! done)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

        driver.join();
        return result;
    }

    DragResult runDragVariant(const juce::String& name, int renderer, bool cornerGrip, bool snapHook,
                               bool classBrush = false, bool eraseFill = false, int moves = 16,
                               bool darkMode = false)
    {
        Backdrop backdrop;
        backdrop.setBounds(60, 60, 1100, 800);
        backdrop.addToDesktop(juce::ComponentPeer::windowIsTemporary);
        backdrop.setAlwaysOnTop(true);
        backdrop.setVisible(true);

        TestWindow window(false, cornerGrip);
        window.setBounds(120, 120, 360, 260);
        window.setAlwaysOnTop(true);
        window.setVisible(true);
        window.toFront(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(500);

        auto hwnd = (HWND) window.getWindowHandle();
        DragResult result;
        if (hwnd == nullptr)
            return result;

        if (auto* peer = window.getPeer())
            peer->setCurrentRenderingEngine(renderer);

        if (snapHook)
            SetWindowSubclass(hwnd, snapHookProc, 9, 0);

        HBRUSH brush = nullptr;
        LONG_PTR previousBrush = 0;
        if (classBrush)
        {
            brush = CreateSolidBrush(RGB(0x0f, 0x1c, 0x15));
            previousBrush = SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR) brush);
        }

        if (eraseFill)
            SetWindowSubclass(hwnd, eraseDarkProc, 7, 0);

        // Tells the compositor this is a dark window, so anything it fills
        // in itself (like area the app hasn't drawn yet) is dark, not white.
        if (darkMode)
        {
            BOOL on = TRUE;
            DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &on, sizeof(on));
        }

        SetForegroundWindow(hwnd);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);

        RECT start {};
        auto result2 = dragCornerAndMeasure(hwnd, name, moves, start);
        result = result2;

        RECT end {};
        GetWindowRect(hwnd, &end);

        if (snapHook)
            RemoveWindowSubclass(hwnd, snapHookProc, 9);
        if (eraseFill)
            RemoveWindowSubclass(hwnd, eraseDarkProc, 7);
        if (classBrush)
        {
            SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, previousBrush);
            DeleteObject(brush);
        }

        std::cout << "  " << name.paddedRight(' ', 40)
                  << " worst white in the new strip: " << juce::roundToInt(result.worst * 100.0) << "%"
                  << "   moves with white: " << result.movesWithWhite << "/" << result.moves
                  << "   (window grew on " << result.grew << " moves, "
                  << (start.right - start.left) << " -> " << (end.right - end.left) << " px wide)" << std::endl;

        window.setVisible(false);
        backdrop.removeFromDesktop();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        return result;
    }

    // ---------------------------------------------------------------------
    // The same measured drag, on a window belonging to a RUNNING Inkwyrd
    // Audio - the plain test window stopped matching what the user saw
    // (software + erase fill cured it there, but not in the app).

    HWND findAppWindow(const juce::String& title)
    {
        struct Search { juce::String title; HWND found = nullptr; } search { title };

        EnumWindows([](HWND hwnd, LPARAM param) -> BOOL
        {
            auto& s = *reinterpret_cast<Search*>(param);
            if (! IsWindowVisible(hwnd))
                return TRUE;

            wchar_t text[256] {};
            GetWindowTextW(hwnd, text, 256);
            if (s.title != juce::String(text))
                return TRUE;

            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == GetCurrentProcessId())
                return TRUE;

            s.found = hwnd;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&search));

        return search.found;
    }
#endif
}

int runResizeDragAppTest()
{
#if JUCE_WINDOWS
    juce::ScopedJuceInitialiser_GUI gui;

    auto title = juce::SystemStats::getEnvironmentVariable("INKWYRD_DRAG_TITLE", "Inkwyrd Audio");
    auto label = juce::SystemStats::getEnvironmentVariable("INKWYRD_DRAG_LABEL", "app");
    auto hwnd = findAppWindow(title);

    if (hwnd == nullptr)
    {
        std::cout << "No running window titled \"" << title << "\"" << std::endl;
        return 1;
    }

    RECT original {};
    GetWindowRect(hwnd, &original);

    // Magenta behind it, as with the test window, then the app window on
    // top of that.
    Backdrop backdrop;
    backdrop.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    backdrop.setAlwaysOnTop(true);
    backdrop.setVisible(true);
    SetWindowPos((HWND) backdrop.getWindowHandle(), HWND_TOPMOST,
                 original.left - 60, original.top - 60,
                 (original.right - original.left) + 560, (original.bottom - original.top) + 460,
                 SWP_NOACTIVATE);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(600);

    RECT start {};
    auto result = dragCornerAndMeasure(hwnd, label, 24, start);

    RECT end {};
    GetWindowRect(hwnd, &end);

    std::cout << "  " << label.paddedRight(' ', 40)
              << " worst white in the new strip: " << juce::roundToInt(result.worst * 100.0) << "%"
              << "   moves with white: " << result.movesWithWhite << "/" << result.moves
              << "   (" << (start.right - start.left) << " -> " << (end.right - end.left) << " px wide)" << std::endl;

    // Put it back as it was.
    SetWindowPos(hwnd, HWND_NOTOPMOST, original.left, original.top,
                 original.right - original.left, original.bottom - original.top, SWP_NOACTIVATE);
    backdrop.removeFromDesktop();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
#endif
    return 0;
}

// INKWYRD_RESIZETEST=seam: two test windows placed exactly flush, and the
// pixels across the join printed - for the "gap between docked windows"
// report. No mouse involved.
int runSeamTest()
{
#if JUCE_WINDOWS
    juce::ScopedJuceInitialiser_GUI gui;

    // 0 = nothing set, 1 = dark mode, 2 = dark mode + no border,
    // 3 = dark mode + no border + square corners
    for (int variant = 0; variant < 4; ++variant)
    {
        TestWindow left(false), right(false);
        left.setBounds(120, 120, 300, 220);
        right.setBounds(420, 120, 300, 220);
        left.setAlwaysOnTop(true);
        right.setAlwaysOnTop(true);
        left.setVisible(true);
        right.setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);

        auto l = (HWND) left.getWindowHandle(), r = (HWND) right.getWindowHandle();

        for (auto h : { l, r })
        {
            if (variant >= 1)
            {
                BOOL dark = TRUE;
                DwmSetWindowAttribute(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
            }
            if (variant >= 2)
            {
                COLORREF none = 0xFFFFFFFE; // DWMWA_COLOR_NONE
                DwmSetWindowAttribute(h, 34 /* DWMWA_BORDER_COLOR */, &none, sizeof(none));
            }
            if (variant >= 3)
            {
                int square = 1; // DWMWCP_DONOTROUND
                DwmSetWindowAttribute(h, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &square, sizeof(square));
            }
        }

        // Exactly flush, in physical pixels - what the snapping produces.
        RECT lr {};
        GetWindowRect(l, &lr);
        SetWindowPos(r, nullptr, lr.right, lr.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);

        RECT rr {};
        GetWindowRect(r, &rr);
        RECT strip { lr.right - 4, lr.top, lr.right + 4, lr.bottom };
        auto frame = grabScreen(strip);

        const char* names[] = { "nothing set", "dark mode", "dark mode + no border", "dark + no border + square" };
        std::cout << names[variant] << "   (left ends x=" << lr.right << ", right starts x=" << rr.left << ")" << std::endl;
        for (int y : { 30, 110, 190 })
        {
            std::cout << "   y+" << y << ":";
            for (int x = 0; x < 8; ++x)
            {
                auto c = frame.getPixelAt(x, y);
                std::cout << (x == 4 ? " | " : " ") << (int) c.getRed() << "," << (int) c.getGreen() << "," << (int) c.getBlue();
            }
            std::cout << std::endl;
        }

        left.setVisible(false);
        right.setVisible(false);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
    }
#endif
    return 0;
}

int runResizeDragTest()
{
#if JUCE_WINDOWS
    juce::ScopedJuceInitialiser_GUI gui;
    std::cout << "Real drag test - moving the mouse. Grabs go to %TEMP%\inkwyrd-drag-*.png" << std::endl;
    // Every paint takes 30ms, like a busy real window.
    paintDelayMs = 30;
    std::cout << "(slow paint: 30ms)" << std::endl;
    runDragVariant("Direct2D, no fix", 1, true, false, false, false, 24);
    runDragVariant("Direct2D, dark mode", 1, true, false, false, false, 24, true);
    runDragVariant("software, erase fill", 0, true, false, false, true, 24);
    runDragVariant("software, dark mode", 0, true, false, false, false, 24, true);
    runDragVariant("software, erase fill + dark mode", 0, true, false, false, true, 24, true);
    paintDelayMs = 0;
#endif
    return 0;
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
