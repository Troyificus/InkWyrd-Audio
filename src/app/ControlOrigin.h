#pragma once

#include <cctype>
#include <string>

// Who is allowed to drive the local control server.
//
// ControlServer listens on 127.0.0.1 with no authentication, on the
// reasoning that nothing outside this machine can reach it. That is
// true of the network, and it is NOT true of the browser: a WebSocket
// handshake is not subject to the same-origin rules that stop a page
// calling a local HTTP endpoint, so any web page the user happens to
// have open can connect to ws://127.0.0.1:39231 and start sending
// commands. Nothing sensitive can be read back - the commands only skip
// tracks, fire the killswitch, trigger a sound, change scene - but a
// page that can stop the music in the middle of somebody's game is not
// something to ship knowingly.
//
// The rule is one line, and it works because of an asymmetry in who
// sends the Origin header:
//
//   - A browser ALWAYS sends Origin on a WebSocket handshake. It cannot
//     be turned off from page script; a sandboxed iframe sends the
//     literal "null" rather than omitting it.
//   - The Stream Deck plugin connects with Node's "ws", which sends no
//     Origin unless asked to. It never asks.
//
// So "carries an Origin header at all" means "a web page is calling",
// and that is exactly the caller to refuse. Localhost origins are
// refused too: a page served from a dev server on this machine is still
// a web page, and allowing them would hand the rule to anything that
// can get a page onto 127.0.0.1.
//
// This does not defend against a hostile program already running as the
// user - that program can simply omit the header. Nothing short of real
// authentication would, and a local process with the user's privileges
// has far worse options available than skipping a track.
namespace inkwyrd
{

// headers: any map-like range of (name, value) pairs. Looked up with a
// case-insensitive linear scan rather than map::find, so this works for
// both IXWebSocket's case-insensitive map and a plain std::map in the
// test - and because HTTP header names are case-insensitive regardless
// of which container is holding them.
template <typename Headers>
bool controlHandshakeAllowed(const Headers& headers)
{
    const std::string wanted = "origin";

    for (const auto& header : headers)
    {
        const std::string& name = header.first;
        if (name.size() != wanted.size())
            continue;

        bool same = true;
        for (std::size_t i = 0; i < name.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(name[i])) != wanted[i])
            {
                same = false;
                break;
            }

        // Present at all is enough - including present but empty. The
        // value is not inspected, deliberately: an allow-list of origins
        // would be a list of web pages permitted to drive the app, and
        // there is no such page.
        if (same)
            return false;
    }

    return true;
}

} // namespace inkwyrd
