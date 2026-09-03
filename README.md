# Inkwyrd Audio

**Beta** - a standalone Windows app for running D&D (or any tabletop)
sessions over Discord: local music playlists with shuffle and crossfade,
an on-demand soundboard, and live mic processing through your own VST3
plugins - all mixed together and sent straight to Discord through the
app's own bot connection. No virtual audio cable, no DAW routing, no
Kenku FM.

## Download

Grab the latest installer from the
[Releases page](https://github.com/Troyificus/InkWyrd-Audio/releases)
(the newest one at the top - GitHub's "latest" shortcut skips
pre-releases entirely, so it doesn't work while this is still a beta)
and run it. It installs just for your own Windows account - no admin
rights needed.

> Windows will likely show a **"Windows protected your PC"** SmartScreen
> warning the first time you run the installer. This is a small beta
> project without a paid code-signing certificate yet, not a sign
> anything is wrong - click **More info -> Run anyway** to continue.

## Setting up your Discord bot

Inkwyrd Audio connects to Discord as its own bot, so everyone running it
needs their own bot application. It's free and takes about five minutes.

1. Go to the [Discord Developer Portal](https://discord.com/developers/applications)
   and sign in with your Discord account.
2. Click **New Application**, give it a name (e.g. "Table Audio"), and
   create it.
3. Open the **Bot** tab on the left. Click **Reset Token** and copy the
   token that appears - this is your `DISCORD_BOT_TOKEN`. Keep it
   private; anyone with it can control the bot.
4. You don't need to enable any of the privileged "Gateway Intents" -
   the bot only joins voice channels, it never reads messages.
5. Open the **OAuth2** tab, then **URL Generator**. Under **Scopes**,
   check `bot`. Under **Bot Permissions**, check `Connect` and `Speak`.
   Copy the URL generated at the bottom of the page.
6. Paste that URL into your browser, pick your Discord server from the
   dropdown, and click **Authorize**. The bot now shows up in your
   server's member list (it'll show offline until the app connects it).
7. Back in Discord, turn on Developer Mode: **User Settings -> Advanced
   -> Developer Mode**. Then right-click your server's icon and
   **Copy Server ID** (this is `DISCORD_GUILD_ID`), and right-click the
   voice channel you want the bot to join and **Copy Channel ID** (this
   is `DISCORD_CHANNEL_ID`).

You should now have three values: a bot token, a server ID, and a voice
channel ID.

## Installing and running

1. Install and run the app as described under [Download](#download).
2. **First run:** a Setup screen appears. Click **Browse...** and point
   it at a folder of music files (WAV, AIFF, FLAC, Ogg Vorbis, MP3,
   AAC/M4A, or WMA) - that's the only required field. Optionally Browse
   to a folder of short sound-effect files for the soundboard, and paste
   in the three values from the [Discord bot setup](#setting-up-your-discord-bot)
   above if you want to actually stream to Discord. Then click
   **Save & Launch**.

   Leaving the Discord fields blank is fine - the app runs in
   **local-monitor-only mode**: mic, playlist, and soundboard mixed and
   played through your own speakers, nothing sent to Discord. That's a
   good way to try it out before setting up a bot at all. Everything you
   enter is saved, so this screen only needs filling in once - a
   **Settings** button on the main screen brings it back later if you
   want to change folders or Discord details.
3. **Wear headphones.** Mic input is mixed live into the same output as
   the music and soundboard, so without headphones you'll get feedback.

### Using it

Across the top: a "Now playing" line, the Discord connection status,
and Skip / Shuffle / Mic mute / Monitor buttons.

**Left - your playlists.** Keep as many as you like (one per scene, mood
or session). Click one to look at its tracks; **double-click, or hit
Play, to switch to it** - the music crossfades across rather than
cutting. **Add folder...** either keeps the folder linked (files you add
to it later show up automatically) or takes a one-time copy of what's in
it, and **Add files...** adds individual tracks. You can also **drag
files and folders straight in from Windows Explorer** - drop them on a
playlist in the list to add them to that one, or anywhere else on the
left-hand side to add them to whichever playlist is selected. Shuffle is
remembered per playlist and only ever picks from that list. Playlists are
stored as readable JSON files - **Open folder** shows you where.

Adding tracks to the playlist you're currently listening to never
interrupts it: the track playing carries on, and the new tracks join the
running order without it jumping back to the top.

**Right - the soundboard.** One button per sound in your soundboard
folder; click to fire it. Sounds can overlap.

**Voice FX...** opens your VST3 plugin list and the live mic chain -
plugins are found automatically in the standard VST3 folder, nothing to
configure.

Changing a Discord bot token/server/channel via Settings takes effect on
the next launch, not immediately - everything else applies right away.

## Features

- As many named playlists as you like, with equal-power crossfade both
  between tracks and when you jump from one playlist to another - no
  manual DJing during a session. Shuffle is per-playlist.
- Build playlists from linked folders (which stay up to date as you add
  files), one-off folder imports, or individual tracks - added through
  the buttons or dragged in from Windows Explorer.
- An on-demand soundboard for sound effects, layered independently of
  the music (up to 16 sounds can overlap at once).
- Live microphone processing through your own VST3 plugin chain (EQ,
  compression, noise gates - whatever you already own), added and
  removed on the fly while a session is running.
- Everything - music, soundboard, and processed mic - is mixed in one
  place and streamed to Discord through the app's own bot connection.
  No virtual audio cable, no separate DAW routing.
- Broad format support: WAV, AIFF, FLAC, Ogg Vorbis, MP3, AAC/M4A, and
  WMA.
- Fully implements Discord's mandatory end-to-end-encrypted voice
  protocol (DAVE), the same one the official Discord client uses.
- Optional Elgato Stream Deck integration - map physical buttons to
  skip/shuffle/soundboard/mic-mute (see `streamdeck-plugin/README.md`;
  currently build-from-source only, not included in the installer).

## Known limitations (beta)

- The installer isn't code-signed, so Windows SmartScreen will flag it
  on first run (see [Download](#download) above).
- The Stream Deck plugin needs to be built from source and requires
  physical Stream Deck hardware to fully test.
- Changing your Discord bot token/server/channel via Settings takes
  effect on the next launch, not immediately - the app won't drop an
  active Discord connection to reconnect with new details mid-session.
- No seek bar yet (planned).
- The soundboard fills itself from your soundboard folder; assignable
  Stream-Deck-style buttons you arrange yourself are planned.

If you hit a bug, please open an issue on this repo with what you were
doing and (if possible) a screenshot of the status line/error.

---

### For developers

See `CLAUDE.md` for the full dev environment setup, architecture notes,
and build order; `docs/design-brief.md` for the original design brief.
