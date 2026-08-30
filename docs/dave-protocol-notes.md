# Discord voice + DAVE: notes from getting the spike working

Working notes from building `src/discord-spike`, kept because several of
these cost real debugging time and none of them are obvious from the
docs alone. Sources: Discord's [voice connection
docs](https://docs.discord.com/developers/topics/voice-connections), the
[DAVE protocol spec](https://github.com/discord/dave-protocol), and the
official [`@discordjs/voice`](https://github.com/discordjs/voice) source
(used as ground truth when the docs were ambiguous).

## The one that cost the most time: keep the endpoint's port

`VOICE_SERVER_UPDATE` hands back an endpoint like
`c-lhr13-15f05e60.discord.media:8443`. **Use it verbatim, port and all.**

Stripping the port and letting the websocket default to 443 produces a
uniquely misleading failure: TLS completes, the voice gateway sends
Hello, you send Identify, and *then* it closes with **4006 "Session is
no longer valid."** Everything up to that point looks perfectly healthy,
so the natural assumption is that the Identify payload is wrong - it
isn't. Port 443 just isn't the backend that owns your session.

Confirmed against `@discordjs/voice`'s `Networking.ts`, which builds the
URL as `` `wss://${endpoint}?v=4` `` with the endpoint passed straight
through from `VOICE_SERVER_UPDATE` untouched.

Symptoms this produced, all red herrings in hindsight:

- Identical failure regardless of gateway version (`?v=4` and `?v=8` both).
- Identical failure with and without a correct DAVE handshake.
- Every field in the Identify payload verifiably correct against the docs.
- Survived a 30-minute cooldown, so not rate limiting.

## DAVE (E2EE) is mandatory, not optional

Since **March 2026**, Discord only supports end-to-end encrypted voice.
A client that doesn't negotiate DAVE gets rejected. This applies to bots.

- Send `max_dave_protocol_version` in Identify (opcode 0). We source it
  from `daveMaxSupportedProtocolVersion()` rather than hardcoding.
- The server replies with the negotiated `dave_protocol_version` inside
  **Session Description (opcode 4)**, not in a dedicated message.
- Note that older libraries (e.g. Eris) don't implement DAVE at all.
  Don't use them as a reference for this part.

We use Discord's own [`libdave`](https://github.com/discord/libdave)
(MIT) rather than implementing MLS by hand. See "Why the prebuilt
binary" below.

## Handshake order, and the ordering hazard in it

The sequence that actually happens on a fresh connection:

1. `Hello (8)` → send `Identify (0)` with `max_dave_protocol_version`
2. `Ready (2)` → SSRC, IP, port for the UDP side
3. UDP IP discovery → send `Select Protocol (1)`
4. `Session Description (4)` → transport secret key **and**
   `dave_protocol_version`
5. `dave_mls_external_sender_package (25)` → send
   `dave_mls_key_package (26)`
6. `dave_mls_proposals (27)` → send `dave_mls_commit_welcome (28)`
7. `dave_mls_announce_commit_transition (29)` → send
   `dave_protocol_ready_for_transition (23)`
8. Transition executes → media encryptor is usable

**The hazard:** step 5 can arrive *before* step 4. But `DaveSession::init()`
needs the protocol version that only step 4 carries. So the external
sender package has to be buffered if it lands early - see
`pendingExternalSenderBytes` in `VoiceGatewayClient`.

## transition_id 0 executes immediately - no execute_transition follows

Normally `dave_protocol_ready_for_transition (23)` is answered by
`dave_protocol_execute_transition (22)` once every member is ready.

**`transition_id = 0` is the exception.** It signals (re)initialization
and, per the spec, "can be executed immediately." Discord sends **no**
opcode 22 for it. Waiting for one deadlocks the handshake at the very
last step - which is exactly what happened on the first otherwise-working
run: the MLS commit processed successfully, epoch reached 1, and then it
sat there until the timeout.

This applies to `transition_id = 0` arriving via opcode 21, via
`dave_mls_announce_commit_transition (29)`, or via
`dave_mls_welcome (30)`.

## Binary opcode framing differs by direction

DAVE opcodes 25-30 are binary websocket frames, not JSON, and the header
is **not** uniform:

| Direction | Opcodes | Header |
|---|---|---|
| server → client | 25, 27, 29, 30 | `uint16 sequence_number` + `uint8 opcode` (3 bytes) |
| client → server | 26, 28 | `uint8 opcode` only (1 byte) |

Opcodes 29 and 30 additionally carry a big-endian `uint16 transition_id`
immediately after the opcode byte, before the MLS payload.

Everything else (0, 4, 11, 13, 21-24, 31) stays JSON.

## Two independent encryption layers, stacked

Easy to conflate; they're separate and both required:

1. **DAVE / MLS** encrypts the Opus *payload* end-to-end, per frame
   (AES128-GCM, keys from the MLS group epoch). Discord itself can't
   read this.
2. **Transport AEAD** (`aead_xchacha20_poly1305_rtpsize`) encrypts the
   RTP packet between us and Discord's servers, using the secret key
   from Session Description.

Order on send: encode Opus → DAVE-encrypt the frame → wrap in RTP and
transport-encrypt → send. `VoiceUdpSocket` deliberately knows nothing
about DAVE; it receives an already-DAVE-encrypted payload and treats it
as opaque bytes.

## Why the prebuilt libdave binary

`libdave` builds with a Makefile and a bootstrap shell script, with no
Windows/MSVC path and no CMake. Discord publishes prebuilt per-platform
releases, so `CMakeLists.txt` fetches
`libdave-Windows-X64-boringssl.zip` (pinned by SHA256) and declares it
as an IMPORTED target instead of fighting a second toolchain.

The DLL statically links BoringSSL, mlspp, and nlohmann-json; all
licenses (MIT / BSD-2-Clause / OpenSSL-style / MIT) are permissive and
fine for a closed-source donation-supported release. License texts ship
inside the archive under `licenses/`.

If a Linux/macOS build is ever needed, the same FetchContent block needs
per-platform URL/hash selection.

## Small things worth knowing

- **Leaving a channel** needs `channel_id: null` in the opcode 4 voice
  state update - a JSON null, not an empty string. An empty string is
  silently ignored.
- **Close code 4014 on exit is normal** when you deliberately left.
- **Disable the websocket library's auto-reconnect.** Discord's Resume
  is its own protocol (opcode 6); a raw reconnect just replays a stale
  Identify and buries the real error under a retry loop.
- **`juce::Logger::writeToLog` goes to `OutputDebugString` on Windows**,
  which is invisible in a console window. Hence `Log.h`.
- **Log from multiple threads with a lock.** Websocket callbacks run on
  the library's own threads; unsynchronised `std::cout` interleaves
  mid-line (observed in practice).
