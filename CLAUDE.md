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

## Format support

`src/audio-engine/Mp3AudioFormat` (MP3, via `third_party/dr_mp3.h` -
public domain/MIT-0, vendored directly rather than via vcpkg since it's
a single header) and `src/audio-engine/MediaFoundationAudioFormat`
(AAC/M4A + WMA, via `IMFSourceReader`) extend JUCE's built-in
WAV/AIFF/FLAC/Ogg Vorbis support. Both registered via
`formatManager.registerFormat(...)` after `registerBasicFormats()` in
every `Main.cpp` that sets up an `AudioFormatManager`.

Deliberately **not** using JUCE's own bundled `MP3AudioFormat` or
`WindowsMediaAudioFormat`:
- JUCE's `MP3AudioFormat` requires an explicit `JUCE_USE_MP3AUDIOFORMAT`
  flag and ships with a real disclaimer from Raw Material Software
  themselves ("NOT guaranteed to be free from infringements of 3rd-party
  intellectual property... AT YOUR OWN RISK") - exactly the ambiguity
  `dr_mp3` avoids (MP3's patents expired worldwide in 2017; the decoder
  itself is public domain).
- JUCE's `WindowsMediaAudioFormat` uses the older, WMA-only Windows
  Media Format SDK (`IWMSyncReader`) - it doesn't cover AAC/M4A at all,
  and isn't the modern Media Foundation API the design brief specifies.

**`MediaFoundationAudioFormat` only reads real files, not arbitrary
streams** - it recovers a file path from the `InputStream*` JUCE hands
it (`dynamic_cast` to `FileInputStream`, then `getFile()`) and lets
`MFCreateSourceReaderFromURL` open the file itself, rather than
implementing a full `IMFByteStream` COM wrapper around
`juce::InputStream`. This covers every real use in this app - playlist
and soundboard always load from a `juce::File` - and was a deliberate
scope call, not an oversight.

**Requests float PCM output from Media Foundation** (`MFAudioFormat_Float`),
not the int16 PCM the Microsoft tutorial this is based on uses (it's
writing a WAV file, where int16 is the simpler target) - reuses the
exact `usesFloatingPointData = true` + raw-bytes-memcpy-into-`int*`
convention already verified against JUCE's own `OggVorbisAudioFormat`,
rather than introduce a second, untested int-scaling conversion path.

**A real bug worth not reintroducing**: `juce::StringArray` has no
`StringArray(text, delimiter)` tokenizing constructor - it doesn't
exist, despite reading like an obviously-supported thing to write. A
call shaped like `StringArray(".m4a;.aac;.wma", ";")` silently compiles
by binding to the variadic multi-value constructor instead, producing a
**two**-element array of literally `".m4a;.aac;.wma"` and `";"` - neither
of which matches any real extension. Discovered because AAC files
never appeared during a real shuffle-playback test, while WMA (a
different registration path at the time) did. Build the list with
explicit `.add()` calls instead.

`MFStartup`/`MFShutdown` are process-wide, reference-counted across
reader instances (a static mutex-guarded counter) so one reader's
destruction doesn't tear down Media Foundation while another is still
using it. COM apartment state is per-*thread*, not per-process - and
`readSamples()` runs on JUCE's read-ahead `TimeSliceThread`, not
necessarily the thread that constructed the reader - so
`CoInitializeEx` is called (idempotently, via a `thread_local` flag) on
every thread that actually makes a Media Foundation call, and
deliberately never paired with `CoUninitialize()` since this code
doesn't own those threads' lifetimes.

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
3. **VST3 hosting - built, mechanically verified.** `src/vst-hosting`
   (`PluginScanner`, `PluginChain`) plus `src/vst-hosting-test` (a
   console harness: live mic -> chain -> speakers via WASAPI, using
   `juce::AudioProcessorPlayer`). Confirmed via real runs against this
   machine's actual installed VST3 plugins (40 found, including
   FabFilter, iZotope RX, Neural DSP, Guitar Rig 7): scanning works,
   loading a real plugin into a live chain works, removing it while
   audio is running works, clean shutdown. `PluginChain::plugins` is
   mutated from the message thread while `processBlock` runs on the
   audio thread - genuinely concurrent, not hypothetical - guarded by
   `chainLock` (a `CriticalSection`); this is a deliberately-accepted
   rare-event lock, not a hot-path one. As with step 2: mechanically
   verified, not yet listened-to for actual audio quality/latency.
4. **Wire the audio engine into the Discord client from step 1 - built,
   mechanically verified for the local half, Discord-streaming half not
   yet tested.** `src/app` is the actual combined application. Along the
   way, `GatewayClient`/`VoiceGatewayClient`/`VoiceUdpSocket`/
   `DaveSession` were extracted out of `discord-spike` into a new shared
   library, `src/discord-voice` - `discord-spike` now just links it
   (rebuilt clean after the move, confirming the refactor didn't break
   it).

   New pieces specific to `src/app`:
   - `MasterEngine` (`juce::AudioIODeviceCallback`) - the one real-time
     audio callback for the whole app. Neither `AudioSourcePlayer` nor
     `AudioProcessorPlayer` alone can combine mic-input-through-a-VST3-
     chain (processor-style) with playlist+soundboard output
     (source-style) into one buffer, so this drives both directly.
   - `DiscordAudioSender` - bridges the audio callback (device-rate
     blocks, arbitrary size) to Discord's fixed 20ms/48kHz Opus cadence
     via a lock-free `juce::AbstractFifo` and a background thread that
     does the actual encode/DAVE-encrypt/RTP-send (never on the audio
     thread). Resamples device-rate -> 48kHz with `LagrangeInterpolator`
     when the device isn't already 48kHz - deliberately "good enough for
     voice chat" (small per-block drift, self-corrects via the FIFO's
     ~2s headroom), not sample-accurate mastering-grade resampling.
     Stereo throughout, unlike the spike's mono test tone, since this
     carries mixed music+voice.

   Verified via a real local run (no Discord credentials - the app
   degrades to local-monitor-only mode cleanly when they're absent, by
   design, so this doesn't require Discord to test the local half):
   VST3 scan (40 real plugins) + playlist auto-crossfade + soundboard
   trigger + VST chain add/remove all running *concurrently* on the same
   shared audio callback without interfering with each other, clean
   shutdown. **Not yet verified:** whether audio actually reaches
   Discord through `DiscordAudioSender` - needs a real bot token and a
   listening test, same as steps 1-3's own audio quality.
5. **Stream Deck integration - built, verified up to the hardware
   boundary.** Two halves:
   - `src/app/ControlServer` - a loopback-only (`127.0.0.1:39231`)
     `ix::WebSocketServer` inside `InkwyrdAudioApp` accepting plain JSON
     commands (`skipTrack`, `toggleShuffle`, `triggerSoundboard`,
     `toggleMute`). Commands land on the websocket server's own thread
     and are marshalled onto the message thread via `callAsync`, same
     pattern as everywhere else engine methods are called from outside
     the message thread. **Fully verified**: a real external Node.js
     websocket client (`streamdeck-plugin/test-control-client.mjs`)
     sent real commands to a real running `InkwyrdAudioApp`; the app's
     own log confirmed receipt and the resulting state change (e.g.
     `mic muted = true`).
   - `streamdeck-plugin/` - the actual Stream Deck plugin (TypeScript,
     bundled via esbuild, Elgato's official `@elgato/streamdeck` SDK).
     Passes Elgato's own `streamdeck validate`. **Not fully verified**:
     this machine has no physical Stream Deck attached, so Elgato's
     software never actually launches the plugin process (it only does
     when one of its actions is visible on a connected device) - a real
     keyDown reaching the app is the one thing that needs actual
     hardware to confirm.

   Two real things caught along the way, worth not re-learning:
   - The `elgatosf/streamdeck-plugin-template` repo (shows up first in
     search results) is **archived** - its manifest format may be
     stale. Used `elgatosf/streamdeck-plugin-samples` instead (current,
     actively maintained) as the reference for manifest fields and the
     bootstrap/action-class pattern.
   - `@elgato/streamdeck`'s `@action(...)` decorator is typed for the
     newer Stage-3 ECMAScript decorators, not TypeScript's legacy
     `experimentalDecorators`. Turning that tsconfig option on breaks
     the build (`TS1238`) - only caught by actually running
     `tsc --noEmit`, since esbuild alone transpiles without
     type-checking and would have built "successfully" anyway.
6. **Broader format support - done, verified with real files.** See the
   "Format support" section above for the full detail (including a real
   bug worth reading about: `StringArray` has no tokenizing constructor).
7. **Packaging/installer - done, verified end-to-end.** Inno Setup 7
   script (`installer/InkwyrdAudio.iss`) bundling `InkwyrdAudioApp.exe`
   + its 3 runtime DLLs (`libdave`, `libsodium`, `opus`) + a licensing
   readme (`docs/THIRD_PARTY_LICENSES.md`, shown as `ThirdPartyNotices.txt`
   pre-install) + `README.md`.

   **Per-user install, not per-machine** - `PrivilegesRequired=lowest` +
   `DefaultDirName={localappdata}\Programs\{#MyAppName}`. This was a real
   fix, not the original design: the first version used
   `DefaultDirName={autopf}\...` (Program Files), which forces admin/UAC
   regardless of any runtime override, since Inno Setup decides privilege
   level from `DefaultDirName`'s constant at compile time. A silent-install
   test against that version spawned an elevated child process stuck
   waiting on a UAC consent prompt a headless session can't answer -
   neither `Stop-Process` nor `taskkill` could kill it afterward ("Access
   is denied": a non-admin session can't touch an elevated process, even
   one it spawned itself). **Those two zombie processes (PIDs 51104 the
   setup exe, 32332 its `.tmp` child) are still running** as of this
   writing and can't be cleared without a reboot or the user manually
   ending them in Task Manager - this is *why* `OutputBaseFilename` in
   the `.iss` is currently `InkwyrdAudio-Setup-v2-{#MyAppVersion}` rather
   than the clean `InkwyrdAudio-Setup-{#MyAppVersion}`: the zombies still
   hold the original output filename open, and recompiling under that
   name fails with `Error 32: process cannot access the file` (confirmed
   directly - reverted the name and re-tried the compile specifically to
   check whether the lock had cleared; it hadn't). **Revert this once
   those processes are gone.**

   Switching to `PrivilegesRequired=lowest` fixed the actual bug: verified
   via a full real end-to-end cycle (Playwright-style discipline, not
   just reading the script) - `/VERYSILENT /SUPPRESSMSGBOXES` install to
   a scratch `%LOCALAPPDATA%`-style directory completed in-process with no
   elevation prompt and no hang; installed file layout matched exactly
   (exe + 3 DLLs + the two renamed docs + uninstaller); launching the
   installed exe directly ran correctly (printed the expected
   "set PLAYLIST_FOLDER first" message and exited cleanly - correct
   behavior for a console app run with no env vars set, not a crash, and
   proof all 3 DLLs actually resolved from the install dir); running the
   bundled `unins000.exe` with the same silent flags removed the install
   directory, the Start Menu folder, and the `HKCU` uninstall registry key
   completely - nothing left behind.

   One tooling gotcha hit while testing this, worth recording since it
   looks exactly like a real installer bug at first: **the Bash tool's
   MSYS/Git-Bash layer silently rewrites leading-slash arguments as
   Windows paths** - `/VERYSILENT` became the literal string
   `C:/Program Files/Git/VERYSILENT` before Inno Setup ever saw it,
   so the installer launched fully interactive instead of silent and sat
   there indefinitely (looked identical to another UAC-style hang until
   the install log's `Setup command line:` entry was actually read).
   Fixed by driving `Start-Process`/Inno Setup invocations through the
   PowerShell tool instead of Bash for anything with `/`-prefixed
   arguments.

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
