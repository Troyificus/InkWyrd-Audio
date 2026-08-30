# Third-party licenses

Every dependency below is permissive (no copyleft, no royalty, no
redistribution restriction that conflicts with closed-source or
donation-supported distribution). This list matches
`docs/design-brief.md` section 10, expanded with what's actually been
integrated since.

| Component | License | Used for |
|---|---|---|
| [JUCE](https://juce.com) | Free "Starter" tier (see note below) | Application framework, audio engine |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (bundled inside JUCE) | MIT (since Nov 2025) | VST3 plugin hosting |
| [libopus](https://opus-codec.org/) | BSD-3-Clause | Opus encoding for Discord voice |
| [libsodium](https://libsodium.org/) | ISC | AEAD transport encryption for Discord voice (`aead_xchacha20_poly1305_rtpsize`) |
| [dr_mp3](https://github.com/mackron/dr_libs) | Public domain / MIT-0 (your choice) | MP3 decoding |
| [IXWebSocket](https://github.com/machinezone/IXWebSocket) | BSD-3-Clause | Discord Gateway/Voice Gateway websocket client, local Stream Deck control server |
| [discord/libdave](https://github.com/discord/libdave) | MIT | DAVE (Discord voice end-to-end encryption) |
| [Cisco mlspp](https://github.com/cisco/mlspp) (bundled inside libdave) | BSD-2-Clause | MLS protocol implementation, underlies DAVE |
| [BoringSSL](https://boringssl.googlesource.com/boringssl/) (bundled inside libdave) | OpenSSL-style / ISC (mixed, all permissive) | Cryptography, underlies DAVE |
| [nlohmann/json](https://github.com/nlohmann/json) (bundled inside libdave) | MIT | JSON parsing, underlies DAVE |
| Windows Media Foundation | Part of Windows itself | AAC/M4A and WMA decoding (OS-provided, no bundled codec) |
| WASAPI | Part of Windows itself | Audio device I/O (chosen over ASIO - see below) |
| [Elgato Stream Deck SDK](https://docs.elgato.com/streamdeck) (`@elgato/streamdeck`, `@elgato/cli`) | Standard free plugin-distribution terms | Stream Deck integration |
| [ws](https://github.com/websockets/ws) | MIT | Stream Deck plugin's connection to the app's control server |

## Explicitly avoided

- **ASIO SDK** - Steinberg relicensed it to **GPLv3** in the same move
  that made VST3 MIT. Embedding it would risk license-contaminating the
  rest of a closed-source app. WASAPI is used instead for all audio I/O.
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
  now is.

## JUCE licensing - the one to actually track

The free **Starter** tier permits a closed-source app, but caps **total
annual revenue or funding - donations explicitly included - at
$20,000**. Beyond that, the **Indie** tier is $40/month (or an $800
one-time perpetual license) for up to $300,000/year. Not a concern at
launch; worth revisiting if the app takes off. See
[juce.com/get-juce](https://juce.com/get-juce) for current terms before
a public release, since pricing/tiers can change.

## Elgato Stream Deck SDK - confirm before shipping

Standard free plugin-distribution terms as of when this was built;
their [terms](https://docs.elgato.com) should be re-checked before any
public release, same as the design brief originally flagged - terms for
third-party SDKs can change between when integration work happens and
when a release actually ships.
