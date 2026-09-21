# Inkwyrd Audio Deck (Stream Deck plugin)

Maps physical Stream Deck buttons to Inkwyrd Audio's playlist, soundboard,
and mic mute controls, per the design brief's step 5. Built against
Elgato's official SDK, using the currently-maintained (non-archived)
[streamdeck-plugin-samples](https://github.com/elgatosf/streamdeck-plugin-samples)
repo as the format/version reference - the older
`streamdeck-plugin-template` repo that shows up first in search results
is archived and shouldn't be trusted as current.

## How it talks to the app

This plugin doesn't touch the audio engine directly - it's a thin
websocket client. `src/app/ControlServer.h` in the main repo hosts a
tiny loopback-only server (`ws://127.0.0.1:39231`) inside
`InkwyrdAudioApp`; this plugin connects to it and forwards button
presses as plain JSON commands (`skipTrack`, `toggleShuffle`,
`triggerSoundboard`, `toggleMute`, `stopAllSounds`). `InkwyrdAudioApp` must be running for
buttons to do anything - the plugin reconnects quietly in the background
if it isn't (or hasn't started yet).

## Building

```
npm install
npm run build
```

This bundles `src/*.ts` (via esbuild, TypeScript, no other build step)
into `com.inkwyrd.audiodeck.sdPlugin/bin/plugin.js` - a single
self-contained CommonJS file, `@elgato/streamdeck` and `ws` included, so
nothing needs installing inside the `.sdPlugin` folder itself.

**Native ECMAScript decorators, not TypeScript's legacy
`experimentalDecorators`** - `@elgato/streamdeck`'s `@action(...)`
decorator is typed for the newer Stage-3 decorator proposal.
`tsconfig.json` deliberately does *not* set `experimentalDecorators`;
turning it on breaks the build with `TS1238: Unable to resolve
signature of class decorator when called as an expression`. Caught by
actually running `tsc --noEmit` - esbuild alone doesn't type-check, so
it will build "successfully" with a decorator-signature mismatch that
would only surface once Stream Deck tried to load the plugin.

## Installing into Stream Deck

```
npx @elgato/cli link com.inkwyrd.audiodeck.sdPlugin
npx @elgato/cli restart com.inkwyrd.audiodeck
```

(or `npm install -g @elgato/cli` once, then drop the `npx`). Elgato's own
`create` wizard is a full-screen interactive prompt (arrow-key
navigation, checkboxes) that doesn't script well - this plugin was
hand-written against the verified-current manifest/SDK reference instead
of scaffolded through it.

Validated against Elgato's own schema via `npx @elgato/cli validate
com.inkwyrd.audiodeck.sdPlugin` - passes clean.

**Not yet tested against a physical Stream Deck** - this machine doesn't
have one attached, so Stream Deck's software never actually launches the
plugin process (it only does so when an action from it is visible on a
connected device). Everything up to that boundary is verified: official
schema validation, a real `tsc` type-check against the SDK's actual
`.d.ts` files, and the app-side control server confirmed working via a
real external websocket client (see below). The one thing that needs a
real device: does a physical keyDown actually reach `onKeyDown` and flow
through to `InkwyrdAudioApp`.

## Testing the app side without a Stream Deck

`test-control-client.mjs` is a small standalone script (uses the `ws`
dependency already installed here) that sends the same JSON commands a
real button press would, for verifying `ControlServer` independent of
any Stream Deck hardware:

```
node test-control-client.mjs mute
node test-control-client.mjs skip
node test-control-client.mjs shuffle
node test-control-client.mjs soundboard <soundName>
node test-control-client.mjs stopall
```

Run `InkwyrdAudioApp` first, then run these against it. This is exactly
how `ControlServer` was verified: real command sent, `[ControlServer]
received: ...` and the resulting state change (e.g. `mic muted = true`)
both appeared in the app's own log output.

## Configuring the Soundboard button

Each Soundboard button needs its target sound's name typed into the
button's settings (select it in the Stream Deck app once placed). It
must match **the name shown on that button in Inkwyrd's Soundboard
window**, exactly - capitals and spaces included.

That name starts out as the sound file's name without its extension,
but it is the BUTTON's name, not the file's: rename a button in Inkwyrd
(right-click -> Rename) and the Stream Deck button has to be updated to
match. Moving a button around the board does NOT change its name, so
rearranging never breaks a Stream Deck button.

A looping button (right-click -> Loop this sound) starts and stops from
the same Stream Deck press - nothing extra to set up.

**The key flashes a warning triangle** if no name has been set. It does
NOT warn about a name that matches nothing: a typo just does nothing,
because the app treats an unknown name as ordinary user error rather
than a fault. If a Soundboard key seems dead, check the name first.

(This section used to say the name had to match a file in a
`SOUNDBOARD_FOLDER` - true before the soundboard became a board of
nameable buttons, and misleading ever since.)
