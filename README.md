# Inkwyrd Audio

Standalone Windows desktop app for running D&D sessions over Discord: local
music playlists (shuffle/crossfade), an on-demand soundboard, and live
mic processing through your own VST3 chain - all mixed in-process and sent
to Discord through the app's own bot connection. No virtual audio cables,
no DAW routing. See `docs/design-brief.md` for the full architecture and
the reasoning behind each dependency choice.

## Build order

Per the design brief, the riskiest unknown (the Discord voice protocol) is
being built and proven first, in isolation, before the real audio engine:

1. **`src/discord-spike`** - **done and verified.** A bare console app that
   joins a voice channel as a bot and plays a generated test tone. Proves
   out Gateway -> Voice Gateway -> DAVE/MLS key exchange -> UDP IP
   discovery -> encrypted RTP, confirmed by actually hearing the tone in
   a real Discord voice channel.
2. Core audio engine (playlist, soundboard, master mixer) - not started.
3. VST3 hosting - not started.
4. Wire the audio engine into the Discord client from step 1 - not started.
5. Stream Deck integration - not started.
6. Broader format support (MP3 via dr_mp3, AAC/WMA via Media Foundation) - not started.
7. Packaging/installer - not started.

## Dev environment setup

- **Visual Studio 2022** with the "Desktop development with C++" workload.
- **CMake** 3.22+ (bundled with VS2022, or install separately).
- **vcpkg**, bootstrapped at `C:\vcpkg`, with `VCPKG_ROOT` set as a user
  environment variable pointing there. Manifest mode is used (`vcpkg.json`
  in the repo root) - `opus`, `libsodium`, and `ixwebsocket` install
  automatically on first CMake configure, no manual `vcpkg install` needed.
- JUCE is **not** a shared local checkout - it's fetched fresh per-build via
  CMake `FetchContent` (pinned to a tagged JUCE release in the top-level
  `CMakeLists.txt`), so there's nothing extra to install for it.

### Configuring and building

```
cmake --preset windows-vs2022
cmake --build --preset windows-vs2022
```

The first configure will take a while - vcpkg builds `opus` and `libsodium`
from source, and FetchContent clones JUCE. Subsequent configures are fast.

Or open the folder directly in Visual Studio 2022 (File > Open > CMake...)
and let its built-in CMake integration pick up `CMakePresets.json`.

### Running the Discord voice spike

Needs a Discord bot application (Developer Portal), invited to a test
server with voice permissions. Set these before running:

```
setx DISCORD_BOT_TOKEN "your-bot-token"
setx DISCORD_GUILD_ID "your-test-server-id"
setx DISCORD_CHANNEL_ID "the-voice-channel-id-to-join"
```

Then run `build/src/discord-spike/Debug/DiscordVoiceSpike.exe` (path may
vary slightly by generator/config). It should join the channel and play a
3-second 440Hz test tone, logging each protocol stage as it goes.

**Verified working** against a real Discord voice channel - the tone was
audible, not just "the code ran without errors."

Note that Discord requires **DAVE (end-to-end encryption)** for all voice
connections as of March 2026, so this isn't just Opus-in-RTP: there's a
full MLS group key exchange (via Discord's own
[libdave](https://github.com/discord/libdave), fetched automatically by
CMake) before any audio can be sent. `docs/dave-protocol-notes.md` covers
that protocol and the non-obvious parts that cost real debugging time -
**read it before touching the voice code**, particularly the notes on
keeping the endpoint's port and on `transition_id = 0`.
