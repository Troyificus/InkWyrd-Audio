# Inkwyrd Audio

## What this is

A standalone Windows desktop app for running D&D sessions over Discord:
local music playlists (shuffle/crossfade), an on-demand soundboard, and
live mic processing through the host's own VST3 chain - all mixed
in-process and sent to Discord through the app's own bot connection.
Built from scratch, not a Kenku FM fork; no virtual audio cables, no DAW
routing. Full rationale and every dependency/licensing decision lives in
`docs/design-brief.md` - read that first for the "why," this file is
about what's actually been built and what's been learned building it.

Longer-term intent: possibly release publicly, donation-supported. Every
dependency choice is made to keep the app legally closed-source-
distributable with no ongoing hosting cost.

## Repo layout

```
CMakeLists.txt              - top-level: JUCE (FetchContent), vcpkg deps,
                               libdave (FetchContent, prebuilt binary)
CMakePresets.json            - windows-vs2022 preset, wires vcpkg toolchain
vcpkg.json                   - manifest: opus, libsodium, ixwebsocket
vcpkg-overlay-triplets/       - x64-windows.cmake with a local build fix,
                               see "Toolchain gotchas" below
docs/
  design-brief.md             - the original pre-implementation brief
  dave-protocol-notes.md      - DAVE/MLS protocol notes from getting the
                               voice spike working - READ before touching
                               any voice gateway or DAVE code
src/
  discord-spike/               - step 1 of the build order, DONE and
                               verified against real Discord (see below)
```

## Build order status

Per the design brief's suggested build order (front-load the riskiest
unknown):

1. **Discord voice spike - DONE, verified.** `src/discord-spike` joins a
   real voice channel as a bot and plays an audible test tone over
   encrypted RTP, including the full DAVE/MLS handshake. Not just "the
   code ran" - actually heard in a real Discord voice channel.
2. **Core audio engine - built, mechanically verified, not yet
   listened-to.** `src/audio-engine` (a static lib: `PlaylistEngine`,
   `SoundboardEngine`) plus `src/audio-engine-test` (a console harness
   playing to real speakers via WASAPI). Confirmed via real runs:
   auto-crossfade fires on schedule and completes, shuffle order is
   genuinely randomized, overlapping soundboard triggers work, manual
   skip works, clean shutdown. What's NOT yet confirmed: whether the
   crossfade actually *sounds* smooth and the soundboard *sounds* right
   - that needs a human listening, which hasn't happened yet.
3. VST3 hosting - not started.
4. Wire the audio engine into the Discord client from step 1 - not started.
5. Stream Deck integration - not started.
6. Broader format support (dr_mp3, Media Foundation for AAC/WMA) - not started.
7. Packaging/installer - not started.

## Dev environment

- **VCPKG_ROOT** is set as a user env var, pointing at `C:\vcpkg`
  (bootstrapped globally, not per-project - reusable for MixCoach too).
- Configure/build:
  ```
  cmake --preset windows-vs2022
  cmake --build --preset windows-vs2022
  ```
  First configure is slow (vcpkg builds opus/libsodium from source,
  JUCE and libdave are fetched). Subsequent ones are fast.
- Running the spike needs `DISCORD_BOT_TOKEN` / `DISCORD_GUILD_ID` /
  `DISCORD_CHANNEL_ID` env vars (`setx`, then a **new** terminal).
  Never paste a bot token into chat with Claude - set it locally.

## Toolchain gotchas hit on this machine (real, not hypothetical)

- **A genuine MSVC toolset skew**, not a code bug: this machine's
  installed compiler emits calls to a vectorized STL helper
  (`__std_find_first_not_of_trivial_pos_1`) that its own runtime libs
  don't export, breaking the link for any code touching
  `std::string::find_first_not_of` (hit via ixwebsocket's URL parsing).
  Confirmed by grepping every CRT lib on the machine for the symbol -
  genuinely absent everywhere. Worked around via
  `_USE_STD_VECTOR_ALGORITHMS=0`, applied globally (top-level
  `CMakeLists.txt` for our own code, `vcpkg-overlay-triplets/x64-windows.cmake`
  for vcpkg-built dependencies) since any TU could hit the same missing
  symbol. If a VS/toolset update ever fixes this upstream, the
  workaround is harmless to leave in place - don't spend time removing
  it speculatively.
- **A CRT-linkage red herring already tried and reverted**: switching
  vcpkg to the `x64-windows-static` triplet was tried first (wrong
  guess at the cause above) and produced a much worse cascade of
  `LNK2038 RuntimeLibrary mismatch` errors, because JUCE's console app
  actually defaults to the **dynamic** CRT, not static as commonly
  assumed. Confirmed directly from the linker's own `MDd_DynamicDebug`
  vs `MTd_StaticDebug` error text. Stay on the plain `x64-windows`
  triplet.
- **mbedtls needs `bcrypt.lib` explicitly** on Windows (its entropy
  source calls `BCryptGenRandom`) - vcpkg's exported config doesn't
  always propagate this transitively, so it's linked explicitly in
  `src/discord-spike/CMakeLists.txt`.
- **`juce::Logger::writeToLog` is invisible in a plain console window**
  on Windows (falls back to `OutputDebugString` with no logger
  installed). Any new console-app target needs its own `logLine()`
  (see `Log.h`) writing to `std::cout` instead.
- **Log from a lock.** Websocket/network callbacks land on library
  threads; unsynchronized console writes from multiple threads
  interleave mid-line in practice, not just in theory - `Log.h`'s
  `logLine()` already guards this with a mutex.
- **`juce::Timer` callbacks never fire without an active message loop
  being pumped**, and a bare console app doesn't run one. `startTimer()`
  itself succeeds and `isTimerRunning()` reports true regardless - the
  registration isn't the problem, dispatch is. `PlaylistEngine`'s
  crossfade timing depends on this; hit in `audio-engine-test` as
  "tracks just never advance," confirmed by finding `timerCallback()`
  itself was never once invoked across 36 seconds even though the timer
  was "running." The fix in `Main.cpp` there
  (`juce::ScopedJuceInitialiser_GUI` + running `runDispatchLoop()` on
  the main thread, stdin reading moved to a background thread,
  playlist/soundboard calls marshalled back via `callAsync`) is the
  pattern to reuse for any future console-app testing of Timer-driven
  engine code. The eventual real GUI app won't need this workaround -
  it'll have a genuine message loop already.

## Discord voice + DAVE

Full details, including the two real bugs that cost the most debugging
time (silently connecting to the wrong port, and a DAVE transition-ID
edge case that deadlocks the handshake), are in
`docs/dave-protocol-notes.md`. Read it before touching
`GatewayClient`/`VoiceGatewayClient`/`DaveSession`/`VoiceUdpSocket` -
several of these are non-obvious from Discord's own docs and easy to
silently reintroduce.

The one-line summary: Discord requires DAVE (end-to-end voice
encryption) for every voice connection as of March 2026 - not optional,
applies to bots. It's implemented via Discord's own `discord/libdave`
(MIT), fetched as a prebuilt Windows binary since upstream's build is
Make-based with no CMake/MSVC path.

## Testing discipline

The Discord voice spike was debugged entirely through real, live runs
against actual Discord servers - there was no way to unit-test the
protocol handshake, and several failures (the port-stripping bug
specifically) looked completely healthy at every layer except the
final result. When something in this project claims to work, it should
mean "verified against the real thing" (a real Discord connection, a
real audio device, a real Stream Deck once that's built) - not "the
code compiles and the logic looks right." Byte-level protocol claims
especially: this session repeatedly found that carefully-reasoned
protocol code that looked correct on paper was still wrong in a way
only a live test surfaced (see `docs/dave-protocol-notes.md`'s "port"
section for the clearest example).

When official documentation is ambiguous or the model's own training
data might be stale (protocol changed after knowledge cutoff, library
API evolved), check a real, actively-maintained reference
implementation rather than guessing from memory - `@discordjs/voice`'s
actual source is what broke the 4006 deadlock in this project's
history, after extensive guessing from spec text alone hadn't.
