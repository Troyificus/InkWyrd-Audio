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
2. **`src/audio-engine`** (+ `src/audio-engine-test`) - **built,
   mechanically verified.** Playlist (shuffle + crossfade) and soundboard
   (overlapping one-shot triggers), summed and played to real speakers.
   Confirmed via real runs: auto-crossfade fires on schedule, shuffle
   order is genuinely random, soundboard triggers overlap correctly.
   Not yet confirmed by ear - see `CLAUDE.md`.
3. **`src/vst-hosting`** (+ `src/vst-hosting-test`) - **built,
   mechanically verified.** VST3 scanning/loading and a live
   mic -> plugin chain -> speakers path. Confirmed via real runs against
   this machine's actual installed plugins: scan, load into a live
   chain, remove while running, clean shutdown. Not yet confirmed by
   ear - see `CLAUDE.md`.
4. **`src/app`** - **built, mechanically verified locally, Discord
   streaming not yet listened-to.** The actual combined application:
   mic through the VST3 chain, mixed with playlist+soundboard, streamed
   to Discord (or local-monitor-only if no Discord credentials are set).
   Confirmed via a real run: VST scan/add/remove, playlist crossfade,
   and soundboard triggers all working concurrently on one shared audio
   callback. Not yet confirmed whether audio actually reaches Discord -
   see `CLAUDE.md`.
5. **`streamdeck-plugin/`** - **built, verified up to the hardware
   boundary.** Maps Stream Deck buttons to skip/shuffle/soundboard/mute
   via a new loopback-only control server (`src/app/ControlServer`) in
   the main app. The control server side is fully verified with a real
   external client; the plugin itself passes Elgato's own validator but
   hasn't been pressed on a real device - see `streamdeck-plugin/README.md`.
6. **Broader format support - done, verified with real files.** MP3 via
   `dr_mp3` (`src/audio-engine/Mp3AudioFormat`); AAC/M4A and WMA via a
   hand-written Windows Media Foundation reader
   (`src/audio-engine/MediaFoundationAudioFormat`), since JUCE's own
   bundled `WindowsMediaAudioFormat` uses the older WMA-only Windows
   Media Format SDK and doesn't cover AAC at all. Verified against real
   ffmpeg-generated MP3/AAC/WMA files: all three load, decode, and
   crossfade correctly through the playlist engine.
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

### Running the full app

```
setx PLAYLIST_FOLDER "path\to\your\music"
setx SOUNDBOARD_FOLDER "path\to\your\sound-effects"
```

Supports WAV, AIFF, FLAC, Ogg Vorbis, MP3, AAC/M4A, and WMA.
`SOUNDBOARD_FOLDER` is optional. Add the same `DISCORD_BOT_TOKEN` /
`DISCORD_GUILD_ID` / `DISCORD_CHANNEL_ID` as the spike above to actually
stream to Discord - without them the app runs in local-monitor-only mode
(mic + playlist + soundboard through your speakers, nothing sent
anywhere), which is a fine way to test the audio engine and VST3 chain
without any Discord setup at all.

Then run `build/src/app/Debug/InkwyrdAudioApp.exe` (path may vary by
generator/config). Commands once running: `s` skip/crossfade, `h` toggle
shuffle, `t` now playing, `m` toggle mic mute, `p` list found VST3
plugins, `a <index>` / `r <index>` add/remove a plugin from the live
voice chain, `l` list the chain, a number triggers a soundboard sound,
`q` quits (cleanly leaves the Discord channel first, if connected).

**Wear headphones when testing** - mic input runs live to your speakers
through the VST3 chain, and speaker-to-mic feedback is exactly as
unpleasant as it sounds.

It also opens a loopback-only control server on port 39231, which
`streamdeck-plugin/` connects to - see that directory's own README for
building and installing the Stream Deck plugin.
