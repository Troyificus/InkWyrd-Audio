# Third-party licenses

Inkwyrd Audio itself is **source-available and proprietary** - see
`LICENSE` at the repository root. That decision is what makes the two
entries below matter, so read both before changing how either is linked
or which JUCE version is pinned:

- **TagLib** is weak copyleft (LGPL), and stays legitimate only because
  it is dynamically linked. Its own section is below.
- **The VST3 SDK** is MIT only in JUCE 8.0.15 and newer. It is fine as
  pinned; its section below says what downgrading JUCE would cost.

Everything else is permissive: no copyleft, no royalty, and no
redistribution restriction that conflicts with closed-source or
donation-supported distribution. This list matches `docs/design-brief.md` section 10,
expanded with what's actually been integrated since.

| Component | License | Used for |
|---|---|---|
| [JUCE](https://juce.com) | Free "Starter" tier (see note below) | Application framework, audio engine |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (bundled inside JUCE) | MIT, since SDK 3.8.0 - which means JUCE 8.0.15 or newer. See below before downgrading JUCE | VST3 plugin hosting |
| [libopus](https://opus-codec.org/) | BSD-3-Clause | Opus encoding for Discord voice |
| [libsodium](https://libsodium.org/) | ISC | AEAD transport encryption for Discord voice (`aead_xchacha20_poly1305_rtpsize`) |
| [dr_mp3](https://github.com/mackron/dr_libs) | Public domain / MIT-0 (your choice) | MP3 decoding |
| [IXWebSocket](https://github.com/machinezone/IXWebSocket) | BSD-3-Clause | Discord Gateway/Voice Gateway websocket client, local Stream Deck control server |
| [discord/libdave](https://github.com/discord/libdave) | MIT | DAVE (Discord voice end-to-end encryption) |
| [Cisco mlspp](https://github.com/cisco/mlspp) (bundled inside libdave) | BSD-2-Clause | MLS protocol implementation, underlies DAVE |
| [BoringSSL](https://boringssl.googlesource.com/boringssl/) (bundled inside libdave) | OpenSSL-style / ISC (mixed, all permissive) | Cryptography, underlies DAVE |
| [nlohmann/json](https://github.com/nlohmann/json) (bundled inside libdave) | MIT | JSON parsing, underlies DAVE |
| [TagLib](https://taglib.org/) | LGPL-2.1-only **OR** MPL-1.1 (see note below) | Reading and WRITING track tags, including artwork |
| Windows Media Foundation | Part of Windows itself | AAC/M4A and WMA decoding (OS-provided, no bundled codec) |
| WASAPI | Part of Windows itself | Audio device I/O (chosen over ASIO - see below) |
| [Elgato Stream Deck SDK](https://docs.elgato.com/streamdeck) (`@elgato/streamdeck`, `@elgato/cli`) | Standard free plugin-distribution terms | Stream Deck integration |
| [ws](https://github.com/websockets/ws) | MIT | Stream Deck plugin's connection to the app's control server |
| [Silkscreen](https://github.com/googlefonts/silkscreen) font, Copyright 2001 The Silkscreen Project Authors | SIL Open Font License 1.1 | The pixel font in the Pixel Phosphor example skin. Embedded in the app and written into that skin's folder with its licence as `Silkscreen-OFL.txt`, as the OFL requires; the font is not sold on its own |

## TagLib - the one copyleft dependency

TagLib is dual-licensed **LGPL-2.1-only OR MPL-1.1**. Either is workable
here, and the way it is used satisfies both:

- **It is used UNMODIFIED**, straight from vcpkg (`taglib` 2.3.1). No
  patches, no vendored copy in this repo.
- **It is linked as a DLL** (`tag.dll`, from vcpkg's `x64-windows`
  dynamic triplet), which is the route LGPL-2.1 allows for software that
  isn't itself LGPL. Under MPL-1.1, the file-level copyleft covers
  TagLib's own source files only, which nothing here touches.
- **The obligation** is to keep shipping the licence text (this file, and
  the vcpkg copy installed beside the binary), and to say where the
  source is: https://taglib.org/ and
  https://github.com/taglib/taglib.

**Before switching to a static build of TagLib**, revisit this: static
linking removes the LGPL route, leaving only MPL-1.1, which is defensible
but a different argument. The dynamic link is deliberate, not incidental.

Why it's worth the one exception: nothing permissive covers reading AND
writing tags across MP3, FLAC, Ogg, MP4, WMA, WAV and AIFF. The Windows
property system, which this app used before, can read tags but is no use
for writing them, and hand-rolling an ID3v2 writer to avoid a licence
note would be a worse trade - a bug in that code corrupts someone's
music.

## The VST3 SDK - resolved by the JUCE 8.0.15 bump

**Current position: MIT, and nothing is owed to anyone.** Verified by
reading the licence header in the fetched JUCE source, not the release
notes: `MIT License, Copyright (c) 2025, Steinberg Media Technologies
GmbH`.

Worth keeping the history, because it is the reason JUCE is pinned where
it is and a future downgrade would quietly reintroduce the problem:

Steinberg relicensed the VST3 SDK to MIT with SDK **3.8.0** (October
2025). **JUCE 8.0.6, pinned until 0.1.1-beta, predated that** and bundled
the 2024 SDK - "Steinberg VST3 License **or** GPLv3". For a closed-source
app the GPLv3 half is unavailable, so the proprietary half applied, and
its text says:

> Before publishing a software under the proprietary license, you need to
> obtain a copy of the License Agreement signed by Steinberg Media
> Technologies GmbH.

That made it a genuine blocker for a public proprietary release, found
while choosing the app's own licence.

**Bumping JUCE to 8.0.15 removed it** rather than satisfying it with
paperwork. The SDK also moved inside JUCE, to
`modules/juce_audio_processors_headless/format_types/VST3_SDK/`.

**Do not downgrade JUCE below 8.0.15** without re-reading this. The
bundled SDK's licence is a property of the JUCE version, and going
backwards puts the Steinberg agreement back on the to-do list.

## Explicitly avoided

- **ASIO SDK** - Steinberg relicensed it to **GPLv3** in the same move
  that made VST3 MIT (October 2025). GPLv3 would license-contaminate a
  closed-source app, so this stays avoided no matter which JUCE is
  pinned. WASAPI is used instead for all audio I/O.
- **JUCE's own bundled `MP3AudioFormat`** - requires an explicit
  `JUCE_USE_MP3AUDIOFORMAT` flag and ships with a disclaimer from Raw
  Material Software themselves that it's "NOT guaranteed to be free from
  infringements of 3rd-party intellectual property." `dr_mp3` is used
  instead - MP3's patents expired worldwide in 2017, and the decoder
  itself is public domain.
- **JUCE's own bundled `WindowsMediaAudioFormat`** - uses the older,
  WMA-only Windows Media Format SDK and doesn't cover AAC at all. A
  custom Media Foundation (`IMFSourceReader`) reader is used instead,
  covering both AAC/M4A and WMA through the OS's own already-licensed
  decoders.
- **VST2** - not supported at all (v1 scope decision, see
  `docs/design-brief.md`) - its SDK isn't redistributable the way VST3's
  is once JUCE is bumped to a version carrying the MIT SDK.

## JUCE licensing - the one to actually track

JUCE 8 is **dual-licensed: AGPLv3, or the commercial JUCE licence**
(`LICENSE.md` in the JUCE source says so directly). Inkwyrd Audio is
proprietary, so it is distributed under **the commercial licence**, on
the free **Starter** tier. The AGPL option was considered and rejected
because it would let anyone fork and redistribute the app, which is the
opposite of the intent behind `LICENSE`.

That choice has one ongoing condition, and it is the only one in this
whole project:

> **Starter caps total annual revenue or funding at $20,000, and the EULA
> counts donations.** Its wording: "the applicable annual revenue or
> funding limit is the total revenue or funding generated by that
> individual or entity's use of the Framework from all sources,
> **including donations**, sponsorship, advertising, and any other
> indirect revenue."

So if Inkwyrd ever takes donations, that total is what matters - not
profit, and not sales, of which there are none.

| Tier | Cap | Price | Closed source |
|---|---|---|---|
| Starter | $20,000/yr | Free | Yes |
| Indie | $300,000/yr | $40/month, or $800 once, perpetual | Yes |
| Pro | Unlimited | $175/month, or $3,500 once | Yes |

Verified against [the JUCE 8 EULA](https://juce.com/legal/juce-8-licence/)
in September 2026. **There is no splash-screen requirement on any tier**,
and JUCE 8.0.6 removed the splash screen mechanism outright - its own
`juce_gui_basics.cpp` warns that `JUCE_DISPLAY_SPLASH_SCREEN` is now
ignored. Nothing to display, nothing to suppress.

Crossing $20,000 is not a disaster: it means buying Indie, which at $800
once is roughly a rounding error against that number. The thing to avoid
is crossing it without noticing. Re-check the terms before any release
that changes how the app is funded, since tiers and pricing can change.

Worth remembering for later: **Troy holds the copyright in all of
Inkwyrd's own code**, so nothing here is one-way. The app can be
relicensed - opened up, dual-licensed, or sold commercially - at any
point, provided the JUCE tier matches what it is doing at the time.

## Elgato Stream Deck SDK - confirm before shipping

Standard free plugin-distribution terms as of when this was built;
their [terms](https://docs.elgato.com) should be re-checked before any
public release, same as the design brief originally flagged - terms for
third-party SDKs can change between when integration work happens and
when a release actually ships.
