# Design Brief: Standalone Discord Audio App for D&D Sessions

**Status:** Pre-implementation design brief. Nothing has been built yet — this document captures the scoping and architecture decisions made before any code was written, intended as the starting brief for implementation (e.g. handing to Claude Code running locally).

**Owner:** Troy — runs D&D games, currently uses Kenku FM linked to Discord for music/SFX, wants a purpose-built replacement.

---

## 1. Goal

A single standalone Windows desktop application, not a fork of Kenku FM and not dependent on virtual audio cables or a DAW, that:

- Connects directly to Discord as its own bot and streams audio into a voice channel.
- Plays local music files as playlists, with shuffle and crossfade between tracks.
- Provides a soundboard of sound effects that can be triggered on demand at any time, independent of playlist state.
- Captures the host's own microphone, runs it live through a chain of VST3 plugins the host already owns, and sends the processed voice into the same Discord call — a self-hosted alternative to paid tools like Voicemod.
- Requires no complicated setup and no third-party programs beyond what's specified below (Elgato's own software, if a Stream Deck is used).

Longer-term intent: possibly release this publicly and accept donations. This shapes several licensing decisions below — everything in the recommended dependency stack is chosen to keep the app legally distributable closed-source, without ongoing hosting costs.

## 2. Explicitly out of scope

- **No Kenku FM fork.** Built from scratch.
- **No virtual audio cables** (VB-Cable, etc.) and **no DAW-based routing** (Mixcraft/Reaper monitoring chains). All audio — music, SFX, and processed voice — is mixed and sent by the app itself.
- **No cloud storage / remote hosting for music files.** Originally considered (Google Drive/Dropbox as a source, or a cloud-hosted relay bot to work around poor home upload bandwidth), but dropped: an always-on server is an ongoing cost that doesn't fit a free/donation-supported app. **Local files only.**
  - Note: the original motivation was choppy/low-bitrate audio for players caused by a poor home upload connection. Reading files from cloud storage instead of local disk would **not** have fixed this — the bottleneck is the outbound stream from the host's PC to Discord's voice servers, which is unaffected by where the source file lives. If this remains a real problem after shipping v1, the actual fix is a client/server split (a cheap always-on relay bot doing the Discord upload from a well-connected server) — parked for a possible future version, not v1.
  - Worth diagnosing later whether the choppiness is raw bandwidth (throughput too low) or jitter/packet loss (common on many "poor" connections) — the fix differs slightly, but both point toward eventually moving the send point off the home connection if it recurs.

## 3. Voice routing model (important design constraint)

The host's processed voice is carried through **the app's own bot presence** in the Discord call, not through the host's personal Discord account or system microphone selection. In practice: the host mutes their personal Discord mic and speaks; the app captures the mic locally, and the bot "speaks" in the channel. This is the specific choice that eliminates the need for any virtual cable — the app owns the entire outbound Discord connection, so everything (music, SFX, voice) is mixed inside the process before anything leaves the machine.

## 4. High-level architecture

One process, one executable, one installer. Recommended foundation: **JUCE (C++)**, since a JUCE + Visual Studio 2022 environment is already set up from a prior project (MixCoach).

Three audio sources feed one master mixer, which feeds the Discord voice client:

1. **Playlist engine** — shuffle + crossfade. Built on `AudioTransportSource`/`MixerAudioSource`: crossfade is two transport sources briefly overlapped, ramping the outgoing track's gain down while ramping the incoming track's gain up.
2. **Soundboard engine** — a pool of one-shot players, triggerable at any time regardless of playlist state, mixed in independently.
3. **Live voice path** — mic capture → VST3 plugin chain (JUCE's plugin-hosting classes; same machinery as JUCE's own `AudioPluginHost` example) → mixed in.

All three sum on a master bus, which is what gets encoded and sent to Discord.

## 5. Discord voice connection (build natively)

No existing C++ audio framework includes Discord support — this is protocol work, not DSP work, and is the newest technical territory in the project. Recommend prototyping this piece first, in isolation (a bare console app that joins a channel and plays a test tone), before integrating it with the full mixer, since it's the highest-risk unknown.

Sequence for a bot joining a voice channel:

1. Send a voice state update (Gateway opcode 4) with guild/channel IDs over the regular Discord Gateway websocket.
2. Receive a Voice State Update (session ID) and Voice Server Update (token + endpoint).
3. Open a second websocket to the voice endpoint, send Identify (opcode 0).
4. Select Protocol (opcode 1) with a supported encryption mode.
5. Receive Session Description (opcode 4) containing the encryption key.
6. UDP IP discovery for NAT traversal.
7. Send Speaking (opcode 5), then RTP packets of Opus-encoded, encrypted audio over UDP. Maintain heartbeats throughout.

**Encryption modes:** the old `xsalsa20_poly1305*` modes were discontinued by Discord on November 18, 2024 — don't build against tutorials that reference them. Current requirement: support `aead_xchacha20_poly1305_rtpsize` (mandatory), prefer `aead_aes256_gcm_rtpsize` when available.

**Dependencies needed:** a websocket client, an Opus codec library (`libopus`, BSD-licensed), a crypto library for the AEAD ciphers (`libsodium`, ISC-licensed). JUCE already provides JSON parsing and UDP sockets natively, so no extra dependency needed there.

**Prerequisite (one-time, not code):** register a bot application in the Discord Developer Portal, get a bot token, invite it to the server with voice permissions — same as any Discord bot.

## 6. VST3 hosting

- **VST3 only** — no VST2 support planned for v1.
- Steinberg relicensed the VST3 SDK to **MIT** in November 2025 — fully permissive, clean for a closed-source app, no strings attached.
- JUCE's plugin-hosting API handles scanning, loading, and running VST3 instances; this is the same pattern as JUCE's bundled `AudioPluginHost` example project.

## 7. Audio device I/O: prefer WASAPI over ASIO

In the same relicensing move, Steinberg moved the **ASIO SDK to GPLv3**. If the app stays closed-source, embedding the ASIO SDK risks license contamination. Recommendation: default to **WASAPI** shared/exclusive low-latency mode (part of Windows itself, no separate SDK, no licensing complication) for both the mic input and general audio I/O. Only reconsider ASIO later, deliberately, with the licensing implications for the rest of the app understood.

## 8. Audio file format support

Goal: read "pretty much anything," restricted to codecs that carry no licensing obligation.

| Tier | Formats | Approach |
|---|---|---|
| 1 — bundle directly, no license needed | WAV, AIFF | Uncompressed; JUCE built-in |
| 1 | FLAC | Patent-free by design (Xiph); JUCE built-in |
| 1 | Ogg Vorbis | Patent-free by design (Xiph); JUCE built-in |
| 1 | MP3 | All patents expired worldwide since April 2017 (Fraunhofer's own licensing program ended then) — use `dr_mp3` (public domain, single-header, part of the `dr_libs` collection) rather than a licensed library |
| 1 | Opus (`.opus` files) | Essentially free to add — `libopus` is already a dependency for the Discord voice connection |
| 2 — OS fallback, don't embed a codec | AAC / M4A, WMA | Decode via Windows Media Foundation instead of bundling a codec of your own — relies on Windows' own already-licensed decoder rather than the app taking on any licensing obligation |
| Excluded / caution | Monkey's Audio (`.ape`) and similar niche lossless formats | SDK terms aren't as cleanly permissive as FLAC's — leave out unless specifically vetted |

## 9. Stream Deck integration

**Decision: use Elgato's official Stream Deck SDK** (the websocket-based plugin architecture, running as a plugin managed by Elgato's own Stream Deck software), not a raw-HID bypass. Reasoning: anyone who owns a Stream Deck has almost certainly already installed Elgato's software, so requiring it isn't the "extra third-party setup" problem it would be for other integrations. This also avoids exclusive-device-access conflicts and reliance on a reverse-engineered protocol.

(A raw-HID alternative was scoped and rejected — noted here only so it isn't reconsidered without reason: Elgato does document a direct-HID protocol, but it forces a choice between running Elgato's software and running this app, since both want exclusive access to the device.)

## 10. Licensing checklist for public/donation release

| Component | License status | Notes |
|---|---|---|
| VST3 SDK | MIT (since Nov 2025) | Clean for closed-source |
| ASIO SDK | GPLv3 (since Nov 2025) | Avoid embedding; use WASAPI instead |
| JUCE framework | Tiered — see below | Free "Starter" tier permits closed-source |
| libopus | BSD | Clean |
| libsodium | ISC | Clean |
| dr_mp3 / dr_libs | Public domain | Clean |
| MP3 codec (patents) | Expired worldwide since 2017 | Clean |
| FLAC / Ogg Vorbis | Xiph, patent-free by design | Clean |
| Elgato Stream Deck SDK | Standard free plugin-distribution terms | Not a blocker; confirm current terms before shipping |

**JUCE licensing detail:** the free "Starter" tier allows a closed-source app, but caps **total annual revenue or funding — donations explicitly included** — at $20,000. Beyond that, the Indie tier is $40/month (or an $800 one-time perpetual license) for up to $300,000/year. Worth tracking if/when the app takes off, not a concern at the outset.

## 11. Suggested build order

Front-load the least-familiar, highest-risk piece rather than saving it for last:

1. **Discord voice spike** — a minimal console app that authenticates as a bot, joins a channel, and plays a test tone. Proves out the Gateway/Voice Gateway/UDP/Opus/encryption path in isolation.
2. **Core audio engine** — playlist (shuffle, crossfade), soundboard, master mixer, tested against local files only, no Discord yet.
3. **VST3 hosting** — plugin scanning, chain building, live mic processing, still local (e.g. monitor to speakers/headphones).
4. **Wire the audio engine into the Discord client** from step 1 — this is where the three sources actually reach a live voice channel.
5. **Stream Deck integration** via Elgato's SDK — map physical buttons to playlist/soundboard/voice-mute actions.
6. **Broaden format support** — add `dr_mp3`, wire up the Windows Media Foundation fallback path for AAC/WMA.
7. **Packaging, installer, and the licensing checklist above** ahead of any public release.

## 12. Development environment notes

- Existing setup: JUCE + Visual Studio 2022 (carried over from the MixCoach project).
- Recommended to build this with **Claude Code running natively on Windows**, not a cloud sandbox — this project needs real Windows audio hardware, installed VST3 plugins, and eventually a physical Stream Deck to test against, none of which a cloud environment has.
- Use git; GitHub is optional but recommended (doesn't require open-sourcing the code — a private repo still gives version history, backups, and later, Releases/issue tracking if the app goes public). Simplest auth setup: `gh auth login` via the GitHub CLI rather than manually managing token scopes.
