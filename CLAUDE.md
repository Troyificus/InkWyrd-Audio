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

## Playlist library + two-column player (drop 1 of 3)

Replaced the single music folder with a real playlist library, and
rebuilt the main screen as playlist-left / soundboard-right. Design
decisions were put to the user and answered; see the "Agreed but not yet
built" section below for what's deliberately still outstanding.

- **`src/audio-engine/PlaylistLibrary.{h,cpp}`** - `PlaylistEntry` /
  `Playlist` / `PlaylistLibrary`, persisted one JSON file per playlist
  under `%APPDATA%\Inkwyrd Audio\Playlists\`. Deliberately NOT in the
  settings XML: the user wanted them individually backup-able and
  shareable. An entry is a single file, a **live** folder link
  (re-scanned on use, so files added later appear), or a folder
  **snapshot** (frozen at import so tracks can be removed individually).
  A snapshot is a folder entry with `live=false` carrying its own list,
  so it stays one removable row instead of exploding into N file rows.
- **It lives in the AudioEngine lib, not the app**, specifically so the
  console harness can exercise it headlessly - that's what made 26
  automated checks possible for a feature that's otherwise GUI-bound.
- Writes are **atomic** (`juce::TemporaryFile`); a **newer**
  `schemaVersion` is skipped, left untouched on disk and reported rather
  than being rewritten by older code; missing files are reported via
  `ResolvedPlaylist::missingPaths`, never deleted from the JSON (an
  unplugged drive must not destroy a playlist).
- **`PlaylistEngine` stays single-list.** The app resolves a playlist to
  a `juce::Array<juce::File>` and hands it over. This is why
  `ControlServer.cpp` needed *no changes at all* - its four commands
  still mean "the current list". New API: `setTracks` (no playback side
  effects, so editing the list you're listening to doesn't restart it),
  `crossfadeToTracks`, `crossfadeToTrackInCurrentList`,
  `setShuffleChangedCallback`.
- **`start()` was hardened.** It used to reset `activeDeck = 0` without
  stopping deck 1, so using it to switch lists mid-playback left two
  decks running over each other. It now stops both first, and the header
  says to use `crossfadeToTracks()` for live switching.
- **`skipToNext()` no longer no-ops mid-crossfade** - it collapses the
  in-flight fade and starts the next one, so hammering Skip (or a Stream
  Deck button) stops dropping presses. Flagged to the user as an
  audible behaviour change to judge by ear.
- **`SoundboardEngine` gained the API it never had**: `hasSound`,
  `getRegisteredNames`, `removeSound`, `clearSounds`, `stopAllVoices`.
  This fixed a real leak - changing the soundboard folder cleared the
  GUI's parallel `soundNames` array but never the engine's map, so old
  sounds stayed triggerable from the Stream Deck while invisible in the
  app. That parallel array is now deleted entirely; the grid reads
  `getRegisteredNames()`.
- **Removed `jassertfalse` from `trigger()`** on an unknown name. Stream
  Deck buttons carry free-text names, so a typo is ordinary user error -
  it was aborting Debug builds.
- **Stream Deck compatibility is the acceptance criterion**: sound names
  stay `getFileNameWithoutExtension()` and stay the trigger key, so
  every already-shipped plugin button keeps matching.
- **Migration**: guarded by an explicit `playlistLibraryMigrated`
  setting (not "is the library empty?", so deleting every playlist
  doesn't resurrect the old one), the previous single music folder
  becomes a playlist with one live recursive folder link - exactly what
  `loadFolder` used to do.
- **VST panel moved verbatim** into `VoiceFxComponent` behind a
  `Voice FX...` button. A pure move, not a redesign - the user deferred
  designing its permanent home on the main screen.

### Verified

26 headless checks via `INKWYRD_SELFTEST=1` on the console harness
(JSON round-trip, dedup across a linked folder and a file inside it,
missing files reported, duplicate names suffixed, future schemaVersion
skipped and left intact, garbage JSON survived, rename collision
refused, and the engine's "crossfade to an empty list is refused"
guarantee). Plus a real launch against a synthetic legacy settings file
confirming migration produced the right JSON and the right UI.

**GUI click automation proved unreliable here** - `SetForegroundWindow`
silently fails (Windows blocks foreground stealing), so `CopyFromScreen`
captured whatever window was actually on top instead of the app. Use
**`PrintWindow` with `PW_RENDERFULLCONTENT` (flag 2)**, which captures a
window's own pixels even when occluded. Synthetic clicks also minimised
the window once, so treat click-driving as best-effort and prefer
headless verification for anything load-bearing.

## Drag and drop from Explorer (drop 2) - shipped

`PlaylistPanel` implements `juce::FileDragAndDropTarget`. Two things
about JUCE's Win32 file-drop path were **read out of
`juce_ComponentPeer.cpp` rather than assumed**, and both shape the
design:

- `findDragAndDropTarget()` hit-tests to the component under the pointer
  and then walks **up** through `getParentComponent()`. So implementing
  the interface once on the panel catches drops over the playlist list,
  the track list and the buttons alike - there is no need to make either
  `ListBox` a target, and a drop that lands in the gap between them
  still does something sensible instead of being swallowed.
- The `x, y` handed to `fileDragMove`/`filesDropped` are already in the
  **target component's** coordinate space (`newTarget->getLocalPoint`),
  not the window's. `playlistRowAt()` converts panel coords to ListBox
  coords before calling `getRowContainingPosition`.
- `filesDropped` is delivered via `MessageManager::callAsync`, so it's
  safe to open a modal dialog from it (which the folder path does).

Behaviour: dropping onto a playlist ROW targets that playlist even if
it isn't the selected one; dropping anywhere else targets the selected
one; dropping when the library is empty creates a playlist named after
what was dropped rather than being a dead end. Non-playable files are
filtered via the new `PlaylistLibrary::isPlayableFile()` (shared with
`addFiles`, so the UI can't drift out of step with the engine about
what's playable), and a drop containing *nothing* playable says so
rather than silently doing nothing. Dropped folders go through the same
link-vs-snapshot prompt the "Add folder..." button uses - now factored
into `addFoldersWithPrompt()`, which asks **once** for a whole batch
rather than once per folder.

### The real bug drop 2 exposed: editing the list you're listening to

`setTracks()` resets `nextOrderIndex` to 0 and reshuffles. That is
correct for a deliberate switch to a different playlist and **wrong**
for an in-place edit: dropping one track onto the playlist that's
currently playing would have sent it back to the top of the list (with
shuffle off) or re-randomised everything still to come (with shuffle
on). The plan for drop 2 said to just call `setTracks()`; that would
have shipped the bug.

New `PlaylistEngine::updateTracksPreservingOrder()` instead:
- **Shuffle off** - take the playlist's own order outright and resume
  immediately after whatever is playing.
- **Shuffle on** - keep the existing permutation, drop entries that are
  gone, and splice new ones into the part of the order that *hasn't
  played yet* (so a track dropped in mid-session can still come up this
  cycle) while leaving the already-played part untouched.
- Removing a track that had already played slides the cursor back with
  it, so the not-yet-played set stays correct.

Wired up as `PlaylistPanel`'s `onPlaylistEdited(Uuid)` ->
`InkwyrdAudioApplication::handlePlaylistEdited()`, which ignores edits
to any playlist that isn't the active one. The **Refresh button now
fires it too** - re-scanning a linked folder previously updated the
track list on screen while the thing actually playing carried on with
the old files.

Two smaller fixes made while in here:
- The `onLibraryChanged` callback `PlayerComponent` passed to
  `PlaylistPanel` was an empty lambda, and the add-files/add-folder
  buttons only called `refreshTracks()` - so **adding tracks through the
  buttons never reached the engine either**. Same fix covers both.
- Every async `FileChooser`/`AlertWindow` callback in the panel captured
  a raw `this`. Hitting **Settings** while one was open destroys the
  `PlayerComponent` (and with it the panel), so answering the dialog
  afterwards would use freed memory. All five now capture a
  `juce::Component::SafePointer` and bail if the panel is gone.

### Verification

`PlaylistPanel.cpp` is compiled into the **AudioEngineTest** target so
the self-test can drive `filesDropped()` directly - a JUCE Component
doesn't need a peer or a desktop window to be laid out and receive the
call. Real Explorer drags can't be automated from outside the process
(JUCE registers an OLE `IDropTarget` via `RegisterDragDrop`; there's no
`WM_DROPFILES` handler to post to, and the `IDropTarget` pointer from
`GetProp` is only valid inside the owning process), so the OS handoff
itself is the one part left for manual confirmation. Everything from
`isInterestedInFileDrag` inward is covered headlessly, including the
row-targeting coordinate maths and the write-to-disk.

`INKWYRD_SELFTEST=1` is now **87 checks** (26 before drop 2, 48 after it,
the rest added by drop 3 - which compiles `SoundboardGridComponent.cpp`
into the test target for the same reason).

## Assignable soundboard slots (drop 3) - shipped

The board used to BE the contents of the sound-effects folder,
alphabetically - so adding one file shifted every button along by one,
and there was nowhere to keep a name or a colour. New
`src/audio-engine/SoundboardLayout.{h,cpp}` holds a fixed array of slots
(default 24, growable to 256) persisted to
`%APPDATA%\Inkwyrd Audio\soundboard.json`, alongside the Playlists
folder. Same conventions as `PlaylistLibrary`: `schemaVersion`, a newer
file left strictly untouched and reported, atomic writes via
`juce::TemporaryFile`, every mutator saving immediately.

Storage is **sparse** - only filled slots are written, each carrying its
own `index` - so changing the board size can't shift everything along.
The model deliberately stores colour as a plain `juce::uint32` ARGB
rather than a `juce::Colour`, so the AudioEngine library doesn't have to
take a dependency on `juce_graphics`; the GUI wraps it.

**Stream Deck compatibility is the acceptance criterion**, and
`ControlServer.h/.cpp` is again untouched. A slot's NAME is what
`SoundboardEngine` registers it under and what a Stream Deck button's
payload carries, so:

- Migration (`importFolder`) keeps the old alphabetical order and names
  each sound `getFileNameWithoutExtension()` - exactly what the old
  folder scan produced. Verified live: all four commands driven through
  `test-control-client.mjs` against a running app after migration,
  including triggering a migrated sound by its old name.
- Names must be unique, because the engine keys sounds by name - a
  duplicate would make one of them untriggerable. `assign()` auto-
  suffixes " (2)"; `rename()` refuses a collision and says why.
- Renaming a slot therefore has to **re-register** with the engine, not
  just repaint. Recolouring doesn't.

Other behaviour worth not re-deriving:

- **Import is non-destructive.** `importFolder` only fills FREE slots and
  skips files already on the board, so re-importing the same folder adds
  nothing and never rearranges a board someone set up by hand. The
  sound-effects folder field in Setup is relabelled "Sound effects folder
  to import" and now only ever adds.
- **A slot whose file has gone stays on the board** (shown as
  "(file missing)" in a red-ish tint) but is NOT registered with the
  engine, so pressing it is a no-op rather than a failed read
  mid-session. An unplugged drive must not silently wipe a board layout.
- **Shrinking the board never discards a sound**: `setNumSlots` stops at
  the last filled slot and returns what it actually settled on, and the
  UI explains why when it did less than asked.
- **A multi-file drop overwrites only the slot aimed at**, then fills
  gaps after it - it can't wipe out sounds further along the board.
- Right-click is handled by a `SlotButton : juce::TextButton` subclass
  that deliberately does NOT call through to `TextButton::mouseDown` for
  a popup click - letting it register a press would fire the sound as
  well as open the menu.

**The bug the self-test caught here**: `importFolder` was written
non-recursive, mirroring the old folder scan. That is the exact trap that
once made a music folder look empty because every track sat in per-album
subfolders. It now goes through the shared
`inkwyrd::scanFolderForAudio(..., recursive)` the playlists use. Because
that sorts by full path, a flat folder - what every existing user has -
imports in an identical order, so Stream Deck buttons still line up.

Deferred, deliberately: dragging a slot to a different position (assign
and clear is the workaround), and per-slot volume / loop / stop-on-
retrigger.

## Fixes from the first real beta.5 session (worth not re-deriving)

- **Monitor now defaults to OFF unconditionally.** It used to be
  `setLocalMonitoring(!settings.hasDiscordCredentials())` - on whenever
  Discord wasn't configured - reasoning that local-only mode would
  otherwise be silent with nothing explaining why. That produced exactly
  the surprise it was meant to prevent: launching with a cleared bot
  token started playing out of the speakers immediately, which is the
  thing the user had specifically asked to never happen. The silence
  problem is now solved by SAYING SO: `PlayerComponent` shows an orange
  "Monitor is off, so nothing is audible" hint, and only when that's
  actually true (no Discord configured) - with Discord set up, Monitor
  off is the correct normal state and nagging about it would be noise.
  **The regression was visible in this session's own verification
  screenshot and went unnoticed** - when a screenshot is taken to check
  a change, read the whole window, not just the part that changed.
- **Play/Stop.** There was no way to stop playback at all - only Skip and
  Shuffle - so a playlist that auto-started on launch could only be
  silenced by quitting. `PlaylistEngine::pause()`/`resume()`/
  `isPlaying()`; `pause()` collapses an in-flight crossfade first, or
  resuming comes back with two decks stuck at partial gain.
- **Deleting the playing playlist now stops the audio.** The engine holds
  its own copy of the track list, so it happily played a deleted playlist
  forever with nothing on screen owning it. `PlaylistPanel::deleteSelected`
  now calls `notifyEdited(id)`, and `handlePlaylistEdited` treats
  "active playlist, but `findById` returns null" as delete-and-stop.
- **Discord settings usually no longer need a restart.**
  `discordConnectAttempted` was set at startup even when there were no
  credentials to connect with, so pasting a token into Settings
  afterwards left the app insisting on a restart it didn't need. Renamed
  `discordConnectStarted` and set inside `startDiscordConnectIfConfigured`
  where a connect really happens. When a restart IS genuinely required
  (credentials changed after a connection was already made), the app now
  offers a "Restart now" button - which relaunches via a detached
  `cmd /c ping ... & start ""` because the single-instance guard would
  otherwise make the replacement quit on sight.
- **Every modal dialog is now anchored to a component** via
  `gui/Dialogs.h`. Without an associated component JUCE centres a message
  box on the PRIMARY display - on a multi-monitor setup that can put a
  modal dialog on a different screen from the app, and being modal it
  swallows every click on the main window. That reads exactly like the
  app having frozen: nothing in Inkwyrd Audio responds, other programs
  are fine. This is a *candidate* explanation for the reported freeze,
  not a confirmed one.

## The "app froze" report - what was ruled OUT, and the instrumentation added

A real report of "audio pausing, and I couldn't click anything in the
app but other programs were fine". Not reproduced. What measurement
actually established, so none of it gets re-derived:

- **Recursive folder scanning is NOT the cause.** `activatePlaylist`
  does a `library.resolve()` and then `updateWarningBanner()` does a
  second one of the same playlist, and clicking a playlist row does
  another - all synchronous on the message thread, which looked damning.
  Timed against the real configured folder: 4 files, 3 ms. Not it.
- **File opening is NOT the cause.** `createReaderFor` on the user's real
  15 MB MP3s: 3-5 ms. `PlaylistEngine::start()` blocks the calling thread
  for ~25 ms, `skipToNext()` ~11 ms.
- **The 1210 ms of silence at track start is NOT a bug.** Measured via
  `INKWYRD_TIMELOAD` (see below) and initially assumed to be
  `BufferingAudioSource` under-running, which would have justified a
  whole preload-the-next-deck redesign. A warm-up sweep killed it: the
  silence was **exactly 1210 ms regardless of how long the source was
  given to buffer** (0/250/500/1000/2000 ms), and the same harness
  reports 0 ms on files known to start at full amplitude. It is those
  ambient tracks' own quiet intro. **A fix was written and discarded on
  this evidence** - the sweep is the only reason it wasn't shipped.

Added instead of a guessed fix:

- **`MessageThreadWatchdog`** (`src/app/`) logs any message-thread stall
  over 300 ms, with its real duration. It immediately earned its place by
  catching something nobody was looking for: **the VST3 scan blocked the
  UI thread for ~18 seconds at every launch.** That was a genuine
  "the app is unresponsive" defect in its own right, and matched an
  earlier "took about a minute to launch" report. Fixed - see the VST3
  scan section above. The watchdog's 18,222 ms and the scan test's
  18,268 ms were measured independently and agree.
- **`INKWYRD_TIMELOAD="fileA|fileB"`** on the AudioEngineTest binary
  times reader creation and message-thread blocking, and measures how
  long output stays silent after a load. Keep it: it is what disproved
  the buffering theory.

**If the app appears frozen and the watchdog logs NOTHING, the message
thread was never blocked** - look at off-screen modal dialogs first (see
`gui/Dialogs.h`), because that swallows input without blocking anything.

## Launching the app for verification - read this first

**A second copy of the app exits instantly with code 0 and an empty
log.** `moreThanOneInstanceAllowed()` is false, so JUCE detects the
running instance, **skips `initialise()` entirely, calls `shutdown()`,
and returns 0**. There is no error, no crash dump, no log line - it looks
exactly like a silent startup crash, and it cost two separate debugging
detours in one session (once because the user's own installed copy was
running, once because they had relaunched it).

Before concluding the app crashed at startup, run
`Get-Process | Where-Object { $_.ProcessName -like '*Inkwyrd*' }`.

To launch a second copy deliberately, set
**`INKWYRD_ALLOW_MULTIPLE_INSTANCES=1`** - an opt-in escape hatch added
for exactly this. It contends for the control-server port (logged, then
ignored) and shares the audio device, which is fine for checking the UI.

**Also: the app writes to `%APPDATA%\Inkwyrd Audio\` - the same place
the user's real settings, playlists and soundboard live, including their
bot token in plaintext.** Back that directory up before any launch test
that seeds a synthetic config, restore it afterwards, and check the
user's app is not running first: a test script that overwrites the
settings file while their session is live will strip their credentials
from disk. That happened once in this session and had to be restored
from a backup taken minutes earlier.

Copy the backup's CHILDREN into place when restoring
(`Get-ChildItem $bak | ForEach-Object { Copy-Item -Recurse -Force ... }`),
not the folder itself - `Copy-Item -Recurse $bak $dir` nests the backup
inside the target when the target exists.

## The VST3 scan no longer freezes startup

`MessageThreadWatchdog` caught this on its first run: **the plugin scan
blocked the message thread for ~18 seconds at every launch**, because
`initialise()` called `PluginScanner::scan()` directly.
`PluginDirectoryScanner` loads every plugin binary on the machine to read
its description, so this is inherent to scanning, not a bug in the loop.
It matches an earlier user report of the app "taking about a minute to
launch".

Two changes, and the cache is the one that matters most:

- **The scan result is cached** to `%APPDATA%\Inkwyrd Audio\plugins.xml`
  via `KnownPluginList::createXml()`/`recreateFromXml()`, written
  atomically through `juce::TemporaryFile` like the playlist and
  soundboard files. Measured on the dev machine's 40 plugins:
  **a real scan takes 14-18 seconds; restoring the cache takes 7 ms.**
  A normal launch now does no scanning at all.
- **A real scan runs on its own `std::thread`**, joined in `shutdown()`.
  It only happens on a first run (empty cache) or an explicit **Rescan**
  in the Voice FX panel. The Voice FX button is *disabled* and reads
  "Scanning..." while one is in flight - not just relabelled, because the
  scan is writing to the same `KnownPluginList` the panel would be
  instantiating plugins from. `PlayerComponent::setAvailablePlugins()`
  also closes an open Voice FX panel, since its rows are built once at
  construction and can't grow.

`PluginScanner::scan()` and `restoreFromCache()` both now
`sort(sortAlphabetically)`. That was originally just to make a cached
list and a scanned list come out *identical* rather than merely
equivalent - the cache test caught a difference in order - but it also
makes the Voice FX list findable instead of filesystem-ordered.

**Measured end to end after the fix** (Release, cache present): the main
window appears in **4.5-4.8 s**, down from ~18 s of frozen UI. The
watchdog now reports a single ~4.3-4.6 s stall, and permanent startup
phase logging (`[App] startup: ... took N ms`, only logged above 100 ms)
attributes it: **opening the audio device ~3.0 s** and **creating the
first window ~1.6 s**. Both are platform costs - WASAPI device
enumeration and JUCE's first window/graphics init - not repeated waste
like the scan was, so they were left alone. If that last few seconds ever
matters, the lever is showing the window BEFORE opening the audio device
so the app appears at ~1.6 s; `showPlayer()` currently depends on the
device state (warning banner, monitoring default), so that reorder is not
free.

**`INKWYRD_CACHETEST=1`** on the VstHostingTest binary does a real scan,
saves and restores the cache, and checks the restored list against the
scanned one **as a set** before checking order, so a missing plugin is
never mistaken for a reordering. It also covers a missing and a corrupt
cache file, both of which must degrade to "no plugins known yet" (which
the app treats as "scan in the background") rather than crashing.

## Volume: master fader, per-track trims, per-button trims

Three separate controls, deliberately modelled differently:

- **Master** is a FADER: 0-100%, applied in `MasterEngine`'s callback to
  the finished mix, so it affects local monitoring AND what Discord
  receives. Ramped from the previous block's value
  (`applyGainRamp`) rather than applied flat, or dragging it steps the
  gain between blocks and clicks. Persisted in `AppSettings`.
- **Per-track** and **per-soundboard-button** are TRIMS: dB, -24..+6,
  0 = untouched. They exist to fix material that was exported at the
  wrong level, not to mix with.

**Per-track trims are keyed by FILE, not by (playlist, file).** A track
exported hotter than everything else is loud wherever it appears, so
turning it down once fixes it everywhere - and, importantly, a
folder-linked playlist has no per-track rows of its own to hang a setting
off, so a per-playlist model would have left exactly the common case
uncovered. Stored in `TrackGainStore`
(`%APPDATA%\Inkwyrd Audio\track-gains.json`), 0 dB stored as *no entry*
so an untouched library has an empty file. Keys are lowercased: Windows
paths are case-insensitive and the same track reached two ways must not
end up with two trims.

`PlaylistEngine` takes a `setTrackGainProvider()` callback rather than
owning a map, and folds the answer into the deck gain **alongside** the
crossfade curve (`cos(t) * currentTrackGain`), so a trim and a fade
multiply rather than one overwriting the other. `refreshTrackGains()`
re-applies them so dragging a slider is audible on the track that is
already playing. `getCurrentTrackGain()` exists so the self-test can
assert the trim reached the deck, not merely that nothing crashed.

Soundboard trims live on the slot and are passed to
`SoundboardEngine::registerSound(name, file, linearGain)`, applied at
trigger time - these are one-shots, so there is nothing sensible to do to
a clip already halfway through.

**`soundboard.json` went to schemaVersion 2** for the trim and the
picture. An older build then reads it as "newer version", leaves it
strictly alone and reports it, rather than rewriting it and silently
dropping both. `track-gains.json` is a new file at version 1, so no
compatibility question arises.

### The volume bars

Both the soundboard buttons and the playlist's track rows draw a small
level bar, and clicking the BAR opens a slider (`gui/VolumeCallout.h`,
shared so the two can't drift apart) while clicking elsewhere does the
normal thing. Two details that are easy to get wrong:

- **The bar has a tick at unity.** The range is -24..+6 dB, so an
  untouched item sits at 80% of the bar and reads as "turned up loud"
  without one. Against the tick it reads as "at the mark". Untouched also
  draws dimmer; boosted draws amber, so "louder than recorded" and
  "quieter than recorded" are distinguishable at a glance.
- **The track row's hit test uses the ROW width, not the ListBox width**
  (`PlaylistPanel::trackRowWidth()`). Once the list scrolls, rows are
  narrower by the scrollbar, and testing against the ListBox would put
  the clickable area a scrollbar's width right of the visible bar.

`SlotButton` had to stop being a `TextButton`: a background image, a
label over it and a level bar do not survive the LookAndFeel's own
painting, and one rectangle now has to distinguish three different
presses (fire / menu / volume), which `Button::onClick` can't express.
Background images are decoded once into a cache keyed by path - a grid of
photographs would otherwise decode them on every repaint - and cached
even when decoding FAILS, so a bad file isn't retried forever.

Adjusting a button's volume deliberately does NOT call `notifyChanged()`:
that rebuilds every button, which would destroy the one the callout is
anchored to while it is still open. It re-registers with the engine and
repaints just that button instead.

### INKWYRD_RENDERTEST

`INKWYRD_RENDERTEST=<out.png>` on the AudioEngineTest binary paints the
soundboard grid and the playlist panel **offscreen** via
`createComponentSnapshot()` and writes them side by side.

This exists because the app shares `%APPDATA%` with the user's live
session, and seeding a fixture there while they are using it has already
cost one accidental clobbering of their real settings (see the launch
notes above). Painting the components headlessly verifies the actual
paint code - bars, ticks, background pictures, colours - with zero risk
to their data and without needing their app closed.

## Transport: Pause, Stop, Fade out, and the crossfade toggle

Three distinct things that all sound like "stop", kept distinct:

- **Pause** keeps the position. **`hardStop()`** deliberately does not -
  it clears `currentTrackFile` and resets `nextOrderIndex`, so Play
  starts the list from the top. That is the difference that makes it a
  stop rather than a second pause, and the button labels say so
  ("Pause" only appears once a real Stop sits next to it).
- **`fadeOutAndStop(seconds)`** ramps `fadeGain` 1 -> 0 on the existing
  30 ms timer and then calls `hardStop()`. Both **Play and Pause cancel a
  fade in progress** and restore full gain - otherwise resuming comes
  back quieter than it went away with nothing on screen explaining why.
  It is a no-op when nothing is playing.
- It fades the MUSIC, not the mic. A button sitting next to Stop that
  fades the host out mid-sentence would be a surprising thing; the master
  fader is there for taking everything down.

**`applyDeckGains()` replaced `applyCrossfadeGains()`.** Three separate
things now multiply into each deck's gain - the crossfade curve, the
track's own trim, and any fade-out - and having them applied from four
different call sites is exactly how they end up fighting each other.
One function decides, everything else calls it.

**Crossfade off** is not "a crossfade of length zero". `beginCrossfadeTo`
takes a separate straight-cut path that stops the outgoing deck and
starts the incoming at full. The outgoing deck is stopped rather than
left to run out because the same path serves a manual **Skip**, where
letting the old track finish naturally would leave it playing underneath
for minutes. The end-of-track look-ahead also changes:
`transitionLookAheadSeconds()` returns the crossfade length when fading,
and 0.05 s when cutting - not zero, because the timer only ticks every
30 ms and a gap would open.

`crossfadeDurationSeconds` was a `const` member; it is now
`crossfadeSeconds`, clamped to 0.5-15 s, with the enabled flag and the
fade-out length all persisted in `AppSettings`.

## Per-track fade lengths, and why not "wiring"

The user asked whether to build a **wiring system** - linking specific
tracks in specific orders with a crossfade length assigned to each
pairing - or to attach a length to each adjacent pair as they sit in the
playlist order. The answer given, and built, was **neither**: a fade
length per TRACK.

The reasoning is worth keeping, because the wiring idea will sound
appealing again:

- It is **cheap to implement**, which is the misleading part.
  `beginCrossfadeTo` already knows both files at the moment a transition
  starts, so a pair-keyed lookup is a handful of lines. Implementation
  cost is not the argument against it.
- **Shuffle is.** Playlists shuffle by default - the randomiser is a
  headline feature - so a configured pairing mostly never comes up. To
  make wiring meaningful it would have to override shuffle, at which
  point there are two competing ordering systems.
- The use case wiring would serve, "these tracks always segue as a
  suite", **already works**: a playlist with shuffle turned off is a
  fixed order with fixed transitions, today, with no new concept.
- Per-POSITION (a length attached to "the gap between rows 3 and 4") is
  worse still: positions shift the moment a track is added or the list is
  shuffled, so the setting silently attaches to a different transition.

A length per track survives all of that - "this one ends on a long tail"
is true of the track wherever it lands - and is N settings rather than
N-squared relationships.

Mechanically: the length is asked of the **outgoing** track, since a
transition is that track leaving. `transitionLookAheadSeconds()` returns
it too, because a track has to start handing over exactly that far from
its own end. The value is captured into `activeCrossfadeSeconds` when the
fade begins, so changing a setting mid-fade can't make the ramp jump. 0
means "use the global crossfade length", and the slider says **Default**
there rather than "0.0 s", which would read as "cut straight over".

`TrackGainStore` became **`TrackSettingsStore`** (`track-settings.json`)
once it held more than gains, with entries as objects rather than bare
numbers. It reads the old `track-gains.json` when the new file is absent,
so trims set in beta.7 survive the upgrade, and leaves the old file alone
- the first change writes the new one. Only non-default fields are
written, so a track with a trim and no fade doesn't claim a fade of zero.

## The beta.8 silence bug, and the test gap that let it ship

**Symptom:** playing a track crossfaded straight into the next one and
then went silent a few seconds later. Reported from real use, one release
after it shipped.

**Cause, entirely self-inflicted:** `finishCrossfadeNow()` flips
`activeDeck` to the incoming deck and then applies gains. beta.8 replaced
its direct `setGain(currentTrackGain)` with the new `applyDeckGains()`
**without moving `crossfading = false` above the call**. `applyDeckGains()`
branches on that flag, so it took the crossfade branch at t=1 and handed
the deck that had just become active `cos(90 degrees)` - zero. Every
completed crossfade landed on silence.

Order in that function is not cosmetic. The comment there says so.

**Why 155 checks missed it.** Every one of them drove the engine by
calling methods and inspecting state, and **not one ever let a crossfade
run to completion** - that needs `juce::Timer` to fire, which needs a
pumped message loop. The tests could only ever observe a fade *starting*
(`skipToNext` then `isCrossfading()`) or being *cut short* (`pause()`,
which re-applies gains afterwards and would have masked the bug anyway).
The failure lived exactly at the moment none of them reached.

**The fix for the gap**, not just the bug: a real playback check that
writes two test tones, prepares the engine, pulls audio from a background
thread at roughly real-time pace while the message loop runs, and asserts
the output is still non-silent **after a crossfade completes**. It needs
`JUCE_MODAL_LOOPS_PERMITTED=1`, set on the test target only (never the
app, where modal loops are a hazard).

Verified by reintroducing the bug: the new check fails, and the other 162
still pass. A regression test that has never been seen to fail is not
known to test anything.

**Worth generalising:** this project's engine is timer-driven, so any
behaviour that happens *at the end of a timed process* - a crossfade
completing, a fade-out reaching zero, an end-of-track transition - is
invisible to state-poking tests. Those need the pumped-loop harness.

## Looping, and the end-of-track problem underneath it

**Loop track** repeats the current track rather than starting the
playlist over - the case is a single ambient bed left running. Skip is
unaffected: looping only governs what happens when a track reaches its
own end.

The gap between repeats is 0-10 s. **Zero is not a special case**: it
hands over through the normal transition path to the same file, so with
crossfade on the track dissolves into itself and loops seamlessly, and
with crossfade off it cuts straight round. A gap greater than zero
instead lets the track play right OUT and then holds silence - handing
over early the way a normal transition does would eat the end of the
track and then add a gap on top of it.

### The real bug this exposed

`AudioTransportSource` **stops itself** when it reaches the end of its
source. The old end-of-track check was `if (! isPlaying()) return;`
followed by "is the remaining time small?", which means a deck that has
already finished was read as "not playing, nothing to do".

With a 3 s crossfade look-ahead that never mattered - the transition
always fired seconds before the end. But **beta.8's crossfade-off mode
uses a 50 ms look-ahead**, and the transport can finish and stop itself
between two 30 ms timer ticks, which would leave playback dead at the end
of the first track. Looping made it obvious because a gap-based loop has
to wait for the end deliberately.

Fixed with `hasReachedEndOfTrack()`, which treats "stopped while playback
was asked for" as finished, plus a `playbackRequested` flag - because
"the deck isn't playing" otherwise means either "it finished" or "you
pressed Pause", and those want opposite responses. Every place that
starts or stops playback maintains that flag; **`pause()` in particular
must clear it**, or Pause instantly starts the next track.

A length of zero (a file that never loaded) counts as neither playing nor
finished, so a bad file can't send the engine racing through the whole
playlist in a few ticks.

### The test harness needed fixing too

The first attempt at a looping test failed for a reason worth recording:
the audio-pulling thread slept 11 ms per 512-sample block, and
**Windows' default timer granularity is ~15.6 ms**, so it played audio at
about three-quarters speed. A two-second tone hadn't finished after two
and a half seconds of test time, and every timing assertion was
measuring the wrong thing. The pullers now pull however many blocks the
wall clock says are owed rather than trusting sleep_for.

## Voice FX: chosen plugins, and their own editors

Two complaints, both fair, both fixed:

**1. It listed every VST3 on the machine.** The app scanned the whole
system folder, which on a working machine is dozens of plugins - almost
none of which anyone would put on a microphone - and cost 15-20 seconds
on a first launch. Now there is an **Add VST3...** button that opens a
file chooser at the system plugin folder, and only what the user picks
goes on the list. `PluginScanner::addPluginsFromFile()` uses
`VST3PluginFormat::findAllTypesForFile` on ONE file (a single .vst3 can
legitimately contain several plugins, hence the count return value).

Stored in `voice-plugins.xml` via the same `KnownPluginList` XML
round-trip the cache used. **Deliberately a new filename**: reusing
`plugins.xml` would have shown an upgrading user all 40 scanned plugins
again, which is precisely what they asked to be rid of. The old file is
left alone, just unused.

The whole background-scan machinery from beta.6.1 is gone from the app -
no scan thread, no "Scanning..." button state, no startup scan at all.
`PluginScanner::scan()` remains for the standalone VstHostingTest.

**2. Adding a plugin loaded it at its defaults with no way to change
anything**, which for an EQ or de-esser is close to useless. Adding a
plugin now opens **the plugin's own interface** in a `PluginEditorWindow`,
and every plugin in the chain has an **Edit** button to reopen it. A
plugin with no interface of its own gets `GenericAudioProcessorEditor`,
so everything is at least adjustable.

**The lifetime rule that matters**: the editor belongs to the plugin
instance, so an open editor window MUST be closed before that instance
leaves the chain. `VoiceFxComponent::removeFromList` never touches the
chain, Remove closes the editor first, and the destructor closes them
all. `PluginChain::getPlugin()` is message-thread-only and its pointer is
valid only until removal - the header says so.

`PlayerComponent` no longer carries a copy of the plugin list. The panel
reads it straight from `PluginScanner`, because a second copy of the
truth is how the two drift apart.

### Verified

`INKWYRD_PICKTEST=<path.vst3>` on the VstHostingTest binary runs the
whole path against a real plugin: one chosen file yields a description,
a duplicate add is refused with a reason, the list survives
save/restore, a non-plugin file is refused, removal works - and then it
instantiates the plugin, creates its editor, checks the size is real,
and destroys it again, which is where a lifetime mistake would show up.
Confirmed against Bertom Phantom Center 2: its own editor, 425x271.

Then verified in a real launch of the Release build, driven by real
mouse input (a synthetic `element.click()` equivalent would not have
exercised focus properly - see the note on that above): the panel opens
empty rather than listing 40 plugins, **Add VST3...** lands on
`C:\Program Files\Common Files\VST3`, picking Bertom Phantom Center 2
puts it on the shelf and writes `voice-plugins.xml`, clicking it adds it
to the chain and opens **its own GUI** (sliders, preset gear, vendor
branding - not a generic parameter list). Startup was 4.7s with
`0 voice FX plugin(s) in your list` in the log, confirming no scan
happens at all now.

The lifetime paths were exercised deliberately, since that is where this
would go wrong: Edit with the editor already open brings it forward
rather than stacking a second window; **Remove with the editor open**
closes the editor and the app survives; closing the whole panel with an
editor open closes both and leaves the chain intact (reopening still
showed the plugin in the chain); **Forget** empties the shelf and the
XML while leaving the chain alone, as designed.

**One real bug found this way and fixed** (commit 121ef49): adding the
first plugin to the chain showed its row but not the hint line
explaining what the chain is and that Edit opens the plugin's own
window. `rebuildChainListUI()` made the hint visible without ever giving
it bounds, because it only re-laid the rows instead of calling
`resized()` - and showing the hint changes how much vertical room the
list gets. It appeared on a *reopen* of the panel, since the
constructor's own `resized()` runs after the rebuild there, which is
exactly why reading the code would not have caught it.

### Plugin settings are saved as the plugin's own presets

Asked whether the live chain and each plugin's state should persist
across restarts. The user's call: **no** - "users will have to make
presets in the VSTs themselves to save settings, same as they would do
if using them in a DAW." So the chain stays session-only by design, not
by omission. This avoids instantiating plugins during startup (time
cost) and avoids a badly-behaved plugin being able to block launch,
which was the real risk of the alternative.

## Agreed but not yet built

- **Host-selectable Opus bitrate.** `DiscordAudioSender::kDefaultBitrate`
  is currently a fixed 64000 (dropped from 128000 during beta testing -
  128k is a lot of sustained upstream for a home connection, and the
  host is usually also in the call sending their own voice). The user
  explicitly wants this exposed as a setting when the feature pass
  happens, so the host can pick their own quality/bandwidth tradeoff.
  It's a named constant specifically so that's a small change.
- **Per-playlist track position across restarts.** Currently
  session-only (`lastPlayedByPlaylistId` in the app, keyed by id and
  storing a FILE not an index, because shuffle reshuffles the order on
  wrap). A `"lastPlayed"` field is an additive, schema-v1-compatible
  change.
- **Crossfade duration** is a `const` member and doubles as the
  end-of-track look-ahead, so per-playlist fade times need care around a
  live ramp.
- **Winamp-style detachable window layout** - the user's chosen next
  direction after drop 3, decided with reference screenshots of Winamp
  and AIMP. Main window = player head + the playing playlist; SFX and
  Voice FX become **satellite windows** that can pop out, be moved
  anywhere, and snap magnetically to each other and to the main window
  (dragging the main window drags anything flush-attached to it).
  Sequenced explicitly as **layout first, custom skin as its own later
  drop** - the user wants to see the layout working before art time is
  spent on it.

  Feasibility notes from reading the current code:
  - The panels are already standalone components taking engine
    references and owning no window state, and **`Voice FX...` already
    opens in its own free-floating resizable window** - so the pattern
    is proven in-app. Reparenting a JUCE component between the main
    window and a satellite is cheap; `PlayerComponent` can keep owning
    it either way, so `setSoundNames`/`setPlayingPlaylistId` keep
    working regardless of where it's displayed.
  - **JUCE ships no docking framework.** `MultiDocumentPanel` is
    tabbed/tiled MDI, not floating snap. The magnetic snapping and
    move-as-a-group behaviour is custom - a manager every top-level
    window registers with, snapping edges within ~10px on drag. Fully
    testable headlessly (it's pure geometry).
  - **Known crash to design around up front**: Settings calls
    `setContentOwned`, which deletes `PlayerComponent` and with it the
    panels a satellite would be pointing at. Satellites must be torn
    down first. Same class of bug as the `SafePointer` fix in drop 2.
  - A skinned non-native title bar costs Windows snap layouts, Aero
    shake and native accessibility, so it should be optional rather
    than mandatory. Any skin must be original artwork in that visual
    spirit - Winamp's own skin bitmaps are copyrighted.
  - Winamp shows ONE playlist with a "Playlist Selection" dropdown,
    whereas today's panel stacks the library list above the track list.
    Going this route probably means the library becomes a dropdown or
    its own satellite. That's a real design decision, not a detail.

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
- **The uninstall registry key is NOT visible from a Claude Code session
  on this machine, and reading it will lie to you.** Confirmed directly
  during the beta.5 release: `HKCU:\...\Uninstall\{CA652386-...}_is1`
  reads as absent, while
  `%LOCALAPPDATA%\Programs\Inkwyrd Audio\` contains a complete
  install including `unins000.exe`. The app is installed; the registry
  view isn't the real one. Since the AppId is fixed, running the
  documented silent-install-then-uninstall verification would therefore
  **deregister the user's actual installed copy**, leaving orphaned
  files and no entry in "Installed apps".

  So: **don't run that cycle from a Claude Code session here.** The
  failure mode it exists to catch - "forgot to rebuild before compiling
  the installer" - can be covered safely instead by checking that the
  freshly built `Inkwyrd Audio.exe`'s SIZE actually differs from the
  currently installed one, and that the installer's mtime is later than
  the build's. Both are a couple of `Get-Item` calls. Do that, record
  the installer's SHA-256, and say plainly in the handover that the full
  install/uninstall cycle was skipped and why.

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
