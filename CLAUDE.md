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
   script (`installer/InkwyrdAudio.iss`) bundling `Inkwyrd Audio.exe`
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
   one it spawned itself). Those two zombie processes (PIDs 51104 the
   setup exe, 32332 its `.tmp` child) are **still alive in a later
   session**, still holding the original output filename open -
   `OutputBaseFilename` is still `-v2-` suffixed as a result. A
   `tasklist` check that seemed to show them gone was a false negative:
   passing two `/FI "PID eq ..."` filters to one `tasklist` call ANDs
   them together (a process can't match two different PIDs at once), so
   it reports "no tasks" regardless of whether either PID individually
   is still running - check each PID with its own separate `tasklist`
   call. `taskkill /F` on either PID still fails with Access is denied
   from this non-admin session. Revert `OutputBaseFilename` once they're
   *actually* gone - confirmed via a real reboot or the user manually
   ending them in Task Manager, not a repeat of this same false-negative
   check.

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

## GUI conversion (post-beta) - done, verified end-to-end

After the first public beta shipped console-only (env-var configuration,
typed single-letter commands), real user feedback rejected that
entirely: testers shouldn't have to touch environment variables or a
terminal at all, just click Browse and point at a folder. `src/app` was
converted from a `juce_add_console_app` to a real `juce_add_gui_app` -
the console interface is gone completely, not just supplemented.

**New files, all under `src/app/gui/`**: `AppSettings` (typed wrapper
around `juce::ApplicationProperties`/`PropertiesFile`, persists to
`%APPDATA%\Inkwyrd Audio\Inkwyrd Audio.settings` as plain XML - same
pragmatic-security tradeoff as `ControlServer`'s unauthenticated
loopback socket, the bot token is not encrypted at rest), `DiscordConnector`
(the old `Main.cpp` `DiscordConnection`/`attemptDiscordConnection` join
sequence, unchanged step-for-step, moved onto a background `std::thread`
with `callAsync`-marshaled status/complete callbacks - required, since
every `GatewayClient`/`VoiceGatewayClient` readiness wait is a blocking
poll with no async variant), `SetupComponent` (folder Browse buttons via
`juce::FileChooser::launchAsync` + Discord credential fields, prefilled
from `AppSettings`), `PlayerComponent` (now-playing/skip/shuffle/mute,
soundboard buttons, VST3 plugin list with Add, live chain list with
Remove), `MainWindow` (thin `DocumentWindow` swapping Setup/Player
content), `InkwyrdAudioApplication` (the actual `JUCEApplication`,
owns every engine object for the app's lifetime). `Main.cpp` is now
just `START_JUCE_APPLICATION(InkwyrdAudioApplication)`.

`PlaylistEngine`/`SoundboardEngine`/`PluginScanner`/`PluginChain`/
`MasterEngine`/`DiscordAudioSender`/`ControlServer` are all reused
completely unchanged - `ControlServer`'s constructor signature and
`start(39231)`/`stop()` contract were deliberately preserved exactly so
the already-shipped Stream Deck plugin keeps working with zero changes,
confirmed by regression-testing it against the running GUI app (see
below).

**Mid-session Settings behavior, deliberate**: folder changes (playlist/
soundboard) hot-swap immediately - `playlist.stop()` → `loadFolder()` →
`start()`. Discord credential changes persist to disk but do **not**
live-reconnect; the status label shows "Restart to apply" instead. The
DAVE/MLS handshake has only ever been verified via the connect-once-
then-shutdown path documented above - a live reconnect-while-connected
path would be new, unverified surface, and this project's whole
discipline is "verified against the real thing," not "the logic looks
right."

### Real bugs found only by actually building and running it (not just reading the code)

- **`juce_add_gui_app` names the output binary after `PRODUCT_NAME`,
  not the CMake target name.** The console app's `juce_add_console_app`
  used the target name (`InkwyrdAudioApp.exe`) despite having the same
  `PRODUCT_NAME "Inkwyrd Audio"` set; switching to `juce_add_gui_app`
  silently changed the real output filename to `Inkwyrd Audio.exe`.
  Confirmed by listing the actual artefact directory, not assumed - a
  guess here would have shipped an installer pointing at a file that
  doesn't exist. Fixed in `installer/InkwyrdAudio.iss`'s
  `MyAppExeName` define.
- **`JUCE_APPLICATION_NAME_STRING`/`JUCE_APPLICATION_VERSION_STRING`
  are not auto-defined by `juce_add_gui_app`** the way a first guess
  (or even some JUCE example comments) might suggest - they have to be
  injected explicitly via `target_compile_definitions` using
  `$<TARGET_PROPERTY:InkwyrdAudioApp,JUCE_PRODUCT_NAME>`/
  `JUCE_VERSION` generator expressions. Confirmed against JUCE's own
  `examples/CMake/GuiApp/CMakeLists.txt` in the fetched source rather
  than guessed - the example's own comment says as much.
- **`DocumentWindow::setContentOwned(component, true)` resizes the
  *window* to fit the *content component's own size*** - and neither
  `SetupComponent` nor `PlayerComponent` called `setSize()` in their
  constructors, so the window silently collapsed to a ~128x128 stub on
  first real launch (confirmed via a real screenshot, not just "should
  work" reasoning - the window was genuinely there, just tiny). Fixed
  by giving each top-level content component an explicit `setSize(...)`
  call at the end of its own constructor.
- **VST3 scanning is genuinely slow enough to matter for a GUI**
  (~15-20s on this dev machine's real plugin folder) - it runs
  synchronously in `initialise()` before the window is even created,
  which is fine (matches the console app's own behavior) but is worth
  knowing before assuming a launched process that hasn't shown a window
  yet has hung.
- **The mute/shuffle button labels didn't reflect state changes made
  by the Stream Deck plugin.** `ControlServer` mutates
  `MasterEngine`/`PlaylistEngine` state directly (an atomic store /
  direct call) with no notification back to `PlayerComponent` - a real
  Stream Deck `toggleMute` command genuinely muted the mic (confirmed:
  the audio-thread behavior was always correct) but the button kept
  reading "Mic: Live" until the user happened to click something else.
  Only caught by actually running `streamdeck-plugin/test-control-client.mjs`
  against the live GUI app and screenshotting the result, exactly as
  the plan's verification step called for - reading the code would not
  have caught this, since each half (ControlServer's mutation,
  PlayerComponent's button text) is independently correct in isolation.
  Fixed by having the existing ~500ms polling `Timer` (already refreshing
  "Now playing") also refresh both button labels every tick.
- **A settings-resave with no Discord fields ever touched showed
  "Restart Inkwyrd Audio to apply changed Discord settings."** - the
  mid-session-Settings code only checked "has a connect attempt already
  happened this run," not "are Discord credentials actually configured,"
  so a beta tester who only ever changes their music folder would see a
  confusing Discord-specific message. Fixed by gating that message on
  `settings.hasDiscordCredentials()`.

### Verified via real upload/run/click testing, not just compiling

Real Windows GUI, real screenshots (window found and captured via
`EnumWindows`/`GetWindowRect`, not assumed on-screen), real files:
first-run Setup with no prior settings file, Browse to a real folder via
the actual native Windows folder picker (typed a real path into it, not
scripted around it), label updates to the chosen path, Save & Launch
enabling correctly, transition to the Player view with a real track
actually playing (`PlaylistEngine` genuinely loaded and started),
soundboard buttons appearing for a real folder of sound files, the real
VST3 scan populating the plugin list with this machine's actually-
installed plugins, Add/Remove on the live chain, Settings reopening
prefilled with the just-saved values, a second Save & Launch hot-
swapping the playlist folder live, clean shutdown via the window's close
button (process actually exits, no hang), and the existing Stream Deck
`test-control-client.mjs` driving `toggleMute`/`toggleShuffle`/
`skipTrack` against the running GUI app with each change visibly
reflected on screen. **Not yet verified**: an actual Discord connection
from the GUI (needs a real bot token/guild/channel, not exercised in
this pass) and the Settings "restart to apply changed Discord settings"
message's accuracy across a real restart.

## Beta release process

Established during real beta testing, follow this for every future
release:

- **Version numbering**: hotfixes (bug fixes, diagnostics, no new
  user-facing capability) bump the last dot only - `beta.2` ->
  `beta.2.1` -> `beta.2.2`. Only bump to a new whole number
  (`beta.2.x` -> `beta.3`) when an actual feature lands. This is a
  user preference, not a technical constraint - don't infer feature-vs-
  hotfix from the diff size, ask if it's ambiguous.
- **`installer/InkwyrdAudio.iss`'s `#define MyAppVersion`** carries the
  full beta-qualified string (e.g. `"0.1.0-beta.2.1"`) and must be
  bumped to match every release tag - it feeds `AppVersion`, so Windows'
  "Installed apps" list shows which beta is actually installed rather
  than a static "0.1.0" for every one (a real gap: this used to be
  disconnected from the release tag entirely). Bump it *before*
  building the installer for a release, not after.
- **Each release is its own GitHub Release** (a new tag per version,
  e.g. `v0.1.0-beta.2.1`), not one release with its asset silently
  swapped out - keeps a stable download link per version and a real
  changelog per hotfix. Mark the previous release's notes with a
  one-line "Superseded by vX" pointer so anyone landing on an old
  release page finds the current one.
- **Always verify the installer for real before publishing**: silent
  install to a scratch directory (`/VERYSILENT /SUPPRESSMSGBOXES
  /DIR=...`), confirm the file layout and the exe's file size actually
  changed vs. the previous build (catches "forgot to rebuild before
  compiling the installer"), launch it and confirm the main window
  actually appears (poll for the window, don't just fire-and-wait a
  fixed sleep - VST scanning alone can take 15-25s), then uninstall via
  the bundled `unins000.exe`. See the git history around the beta.2 and
  beta.2.1 releases for the exact commands used.
- The `OutputBaseFilename`'s `-v2-` workaround (see the `.iss` comment
  near it) is unrelated to the beta version number - don't confuse the
  two. It's purely about a still-unresolved locked-file conflict with
  two old zombie processes.

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

### Reading the app's real log/settings from a Claude Code session (MSIX redirection)

**Claude Code's Bash/PowerShell tools run inside the Claude desktop
app's MSIX package (`Claude_pzs8sxrjxfjjc`), so their `%APPDATA%` access
is redirected into
`%LOCALAPPDATA%\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Roaming\...`.**
An app the *user* launched normally (parent process `explorer.exe`)
writes to the **real** `%APPDATA%`, so the two diverge and a session can
sit there reading a stale sandbox copy of `log.txt`/`Inkwyrd Audio.settings`
while insisting the user's run "wrote nothing".

This cost real time once already: a beta report was nearly misdiagnosed
as "logging is broken" when the real log was 1406 bytes of perfectly
good diagnostics and the sandbox copy was a stale 63-byte leftover from
an earlier in-session smoke test. The attachment the user pasted was
*also* resolved through the sandbox, so it showed the stale copy too -
matching contents are NOT confirmation you're looking at the right file.

To read the real files, spawn a process without package identity via
WMI (the WMI service host is unpackaged, so the child isn't redirected)
and copy them somewhere unredirected, e.g. the repo drive:

```powershell
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
  CommandLine = 'cmd.exe /c copy /Y "C:\Users\<user>\AppData\Roaming\Inkwyrd Audio\log.txt" "G:\Inkwyrd-Audio\_real_log.txt"' }
```

Check `Get-CimInstance Win32_Process` → `ParentProcessId` to tell which
context a running instance is in. And **delete any copied
`Inkwyrd Audio.settings` immediately** - it stores the Discord bot token
in plaintext by design, so a copy inside the repo is a live credential
sitting in the working tree.

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
