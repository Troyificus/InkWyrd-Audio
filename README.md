# Inkwyrd Audio

**Beta** - a standalone Windows app for running D&D (or any tabletop)
sessions over Discord: local music playlists with shuffle and crossfade,
an on-demand soundboard, and live mic processing through your own VST3
plugins - all mixed together and sent straight to Discord through the
app's own bot connection. No virtual audio cable, no DAW routing, no
Kenku FM.

## Download

Grab the latest installer from the
[Releases page](https://github.com/Troyificus/InkWyrd-Audio/releases/latest)
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
2. Set the required environment variables. The easiest way on Windows:
   search the Start menu for **"Edit environment variables for your
   account"** and add them there (or use `setx NAME "value"` in a
   terminal). At minimum you need:
   - `PLAYLIST_FOLDER` - a folder of music files (WAV, AIFF, FLAC,
     Ogg Vorbis, MP3, AAC/M4A, or WMA).

   To actually stream to Discord, also set the three values from the bot
   setup above:
   - `DISCORD_BOT_TOKEN`, `DISCORD_GUILD_ID`, `DISCORD_CHANNEL_ID`

   Optional:
   - `SOUNDBOARD_FOLDER` - a folder of short one-shot sound effect files.

   Without the Discord variables set, the app still runs fine in
   **local-monitor-only mode** - mic, playlist, and soundboard mixed and
   played through your own speakers, nothing sent to Discord. That's a
   good way to try it out before setting up a bot at all.

   > Environment variables set this way need a fresh terminal/Explorer
   > session to take effect. If the app doesn't pick them up on the
   > first try, sign out and back in (or reboot) and try again.
3. Launch **Inkwyrd Audio** from the Start Menu. It opens a console
   window and connects to Discord automatically if the three Discord
   variables are set.
4. **Wear headphones.** Mic input is mixed live into the same output as
   the music and soundboard, so without headphones you'll get feedback.

### Commands

Once it's running, type these into the console window and press Enter:

| Command | Action |
|---|---|
| `s` | Skip to the next track (crossfades) |
| `h` | Toggle shuffle on/off |
| `t` | Show what's currently playing |
| `m` | Toggle mic mute |
| `p` | List VST3 plugins found on your system |
| `a <index>` | Add a plugin (from the `p` list) to your live mic chain |
| `r <index>` | Remove a plugin from the chain |
| `l` | List the plugins currently in your chain |
| a number | Trigger that soundboard sound (numbers are shown at startup) |
| `q` | Quit (leaves the Discord voice channel cleanly first) |

VST3 plugins are picked up automatically from your system's standard
VST3 folder (usually `C:\Program Files\Common Files\VST3`) - nothing to
configure.

## Features

- Local music playlists with shuffle and equal-power crossfade between
  tracks - no manual DJing during a session.
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

- Console-only interface for now - everything is typed commands in a
  terminal window, no graphical UI yet.
- The installer isn't code-signed, so Windows SmartScreen will flag it
  on first run (see [Download](#download) above).
- The Stream Deck plugin needs to be built from source and requires
  physical Stream Deck hardware to fully test.
- Changing `PLAYLIST_FOLDER`/`SOUNDBOARD_FOLDER` currently means editing
  the environment variable and restarting the app - there's no in-app
  way to change folders yet.

If you hit a bug, please open an issue on this repo with what you were
doing and (if possible) the console output.

---

### For developers

See `CLAUDE.md` for the full dev environment setup, architecture notes,
and build order; `docs/design-brief.md` for the original design brief.
