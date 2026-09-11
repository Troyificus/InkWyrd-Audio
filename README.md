# Inkwyrd Audio

**Beta** A standalone Windows app for running D&D (or any tabletop)
sessions over Discord: local music playlists with shuffle and crossfade,
an on-demand soundboard, and live mic processing through your own VST3
plugins, all mixed together and sent straight to Discord through the
app's own bot connection. No virtual audio cable, no DAW routing.

Heavily inspired by [Kenku FM](https://www.kenku.fm/).

## Download

Get the latest release from the
[Releases page](https://github.com/Troyificus/InkWyrd-Audio/releases).
Take the newest one at the top: GitHub's "latest" shortcut skips
pre-releases, so it doesn't work while this is still a beta.

Each release comes in two forms, the same program in both:

- **The installer** (`InkwyrdAudio-Setup-...exe`) installs just for your
  own Windows account, with no admin rights needed. It adds a Start menu
  entry and an uninstaller.
- **The portable ZIP** (`InkwyrdAudio-Portable-...zip`) needs no
  installing: unzip it anywhere and run `Inkwyrd Audio.exe`. To remove
  it, delete the folder. Your settings and playlists live in
  `%APPDATA%\Inkwyrd Audio` either way, so you can switch between the two
  without losing anything.

> Windows will likely show a **"Windows protected your PC"** SmartScreen
> warning the first time you run either one. This is a small beta
> project without a paid code-signing certificate yet, not a sign
> anything is wrong. Click **More info -> Run anyway** to continue.
>
> Your browser may also say the file **"isn't commonly downloaded."**
> That's the same thing: a new release has no download history yet.
>
> If **Microsoft Defender blocks a download as a threat**, please don't
> override it. [Open an issue](https://github.com/Troyificus/InkWyrd-Audio/issues)
> instead. Microsoft's automatic machine-learning check (detection names
> ending in `!ml`) has flagged Inkwyrd's unsigned downloads, while other
> antivirus products flag few or none of them, and its verdict has
> flipped between builds of identical code. It has been reported to
> Microsoft as a false positive; code signing is the long-term fix. The
> portable ZIP avoids the checks that flag *installers* specifically,
> but Microsoft's check has flagged the program itself too.
>
> Each release's notes list the SHA-256 of both downloads, so you can
> check yours matches with `Get-FileHash <file>` in PowerShell.

## Setting up your Discord bot

Inkwyrd Audio connects to Discord as its own bot, so everyone running it
needs their own bot application. It's free and takes about five minutes.

1. Go to the [Discord Developer Portal](https://discord.com/developers/applications)
   and sign in with your Discord account.
2. Click **New Application**, give it a name (e.g. "Table Audio"), and
   create it.
3. Open the **Bot** tab on the left. Click **Reset Token** and copy the
   token that appears, this is your `DISCORD_BOT_TOKEN`. Keep it
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

## Optional: muting yourself in Discord automatically

When you make your mic live in Inkwyrd, your voice reaches the call
twice: once from your own Discord client and again through the bot.
Inkwyrd can mute *you* in Discord whenever your mic goes live here, and
put your setting back when it stops.

This is entirely optional. If you only use Inkwyrd for music and sound
effects, skip it - nothing else depends on it.

It needs two more things from the **same** Discord application you made
above:

1. **Register a redirect URI.** In the Developer Portal, open your app,
   go to **OAuth2 -> Redirects**, click **Add Redirect**, enter
   `https://inkwyrd.com/rpc` exactly, and **Save Changes**.

   Nothing is ever sent to that address - OAuth just requires one to
   exist. It does need to be that exact URI, and if you register several
   it must be the **first** one in the list.

2. **Fully restart Discord.** Right-click Discord in the system tray and
   choose **Quit Discord** (closing the window only hides it). The
   desktop client caches your application's settings when it starts and
   won't see a redirect you added while it was running.

3. **Copy your client secret.** Developer Portal -> your app ->
   **OAuth2** -> **Client Secret** -> **Reset Secret**, then copy it.
   Resetting it is safe: nothing else uses it. Keep it private, like the
   bot token.

4. **In Inkwyrd**, open **Settings**. Under *Mute me in Discord while my
   mic is live*, paste the client secret, tick **Enable**, and click
   **Authorise...**. Discord will put a consent dialog on screen - click
   **Authorize** on it. The status line under the field will confirm it.
   Then click **Save & Apply**.

You don't need a client ID: Inkwyrd reads it from the bot token you've
already entered.

This mutes your own Discord client locally. It needs no special
permissions on the server and works in any server, including ones you
don't run.

## Installing and running

1. Install and run the app as described under [Download](#download).
2. **First run:** a Setup screen appears. Click **Browse...** and point
   it at a folder of music files (WAV, AIFF, FLAC, Ogg Vorbis, MP3,
   AAC/M4A, or WMA), that's the only required field. Optionally Browse
   to a folder of short sound-effect files to import onto the soundboard,
   and paste in the three values from the [Discord bot setup](#setting-up-your-discord-bot)
   above if you want to actually stream to Discord. Then click
   **Save & Launch**.

   Leaving the Discord fields blank is fine, the app runs in
   **local-monitor-only mode**: mic, playlist, and soundboard mixed and
   played through your own speakers, nothing sent to Discord. That's a
   good way to try it out before setting up a bot at all. Everything you
   enter is saved, so this screen only needs filling in once. A
   **Settings** button on the Player window brings it back later if you
   want to change folders or Discord details.
3. **Wear headphones.** Mic input is mixed live into the same output as
   the music and soundboard, so without headphones you'll get feedback.

## The windows

Inkwyrd is laid out as five separate windows you can arrange however
suits your screen, rather than one fixed panel.

- **Player** - the main window. Now playing, transport, and everything
  that shapes playback.
- **Playlist** - the tracks in whichever playlist is selected.
- **Library** - your playlists, and every track the app knows about.
- **Voice FX** - noise suppression and your microphone plugin chain.
- **Soundboard** - the button grid.

**They snap together.** Drag a window near another one, or near a screen
edge, and it pulls itself flush. Resizing snaps the same way. Drag a
window away to pull it off the group.

**The Player window carries the group.** Dragging it moves everything
docked to it. Dragging any other window detaches just that one, so you
can rearrange without taking the whole layout apart.

**Only the Player has a minimise button**, and minimising it takes every
open window down with it - and brings them all back together. The other
four have an X, which hides them; the four buttons along the bottom of
the Player window (**Playlist**, **Library**, **Voice FX**,
**Soundboard**) bring any of them back.

Positions, sizes and which windows were open are all remembered between
sessions.

## Using it

### The Player window

At the top is the **now-playing display**: the track's artist and title,
the time elapsed and total, a live spectrum of everything being sent
out, and a **seek bar** - click or drag anywhere along it to move
through the track.

Artist and title come from the file's own tags, the same ones the
Library and Playlist windows show. A file with no tags falls back to its
filename: `Artist - Title` splits into both, and anything else shows the
whole name as the title.

**The transport row:** **Pause** (keeps your place), **Stop** (silences
everything and starts the list from the top next time), **Fade out**
(rides the music down to silence and then stops. Useful for ending a scene, for exmaple),
**Skip**, **Shuffle**, and the **Master** fader, which is one control
over everything the app sends out: your speakers *and* Discord.

**The row below** is how the app behaves rather than what it's doing now:
**Mic** mute, **Monitor**, whether tracks **Crossfade** into each other
and over how long, **Loop track**, and how long **Fade out** takes. All
of it is remembered between sessions.

**Loop track** repeats whatever is playing instead of moving on, for a
single ambient bed you want running all session. The slider next to it
sets the silence between repeats: leave it on **No gap** and the track
goes straight back round. With Crossfade on, it dissolves into itself
and loops seamlessly. Skip still moves to the next track; looping only
governs what happens when a track reaches its own end.

Fade out only takes the *music* down, not your microphone. Fading
yourself out mid-sentence isn't what a button next to Stop should do. Use
the Master fader if you want to take absolutely everything down.

**Monitor is off when the app starts.** It controls whether the mix also
comes out of *your own* speakers. When you're in the Discord call you
already hear everything through the bot, so leaving it on would play
every track twice, slightly offset. If you're running without a Discord
bot, turn Monitor on.

### The Library window

The top half is **your playlists**. Keep as many as you like (one per
scene, mood or session). Click one to look at its tracks in the Playlist
window; **double-click, or hit Play, to switch to it.** The music
crossfades across rather than cutting. **New**, **Rename** and
**Delete** manage the list. To see where they're stored (readable JSON
files), use **Open playlists folder** in Settings.

The bottom half is **All Tracks**: every track Inkwyrd knows about,
independent of which playlists happen to use it. It stays put while you
click between playlists.

All Tracks is a table of **Title, Artist, Album and Genre**, read from
each file's embedded tags. Click a column header to sort by it, and
click again to reverse. Sorting by Album keeps each record in
track-number order. The first launch after adding music reads the tags
in the background, so rows may show filenames for a moment before they
fill in.

**Add files...** and **Add folder...** put tracks into this library.
Add folder either keeps the folder linked (files you add to it later
show up automatically) or takes a one-time copy of what's in it. You can
also **drag files and folders straight in from Windows Explorer**.

To get tracks into a playlist, select them here and click
**Add to playlist**, or **drag them onto the Playlist window**.

**Remove** takes a track out of the library. It does *not* touch any
playlist already using it - a track can disappear from All Tracks and
still play fine in a playlist that has it.

**Every track has its own volume and its own fade length**, reached by
clicking the small bar in its **Vol** column.

**Volume** is for the track that was exported hotter than everything else
and makes everyone jump when shuffle lands on it. Pull it down once and
it stays down. The small notch on each bar is normal volume.

**Fade into next** sets how long *this* track takes to hand over to
whatever follows it, overriding the global Crossfade length. It's for the
track that ends on a long tail and wants a slow hand-off, or the one that
stops dead and wants a quick one. Leave it on **Default** and it follows
the global setting; tracks with a fade of their own say so on their row.

Both settings belong to the *file*, so a track that appears in several
playlists is fixed in all of them at once, and the volume applies
straight away if that track is playing.

### The Playlist window

Shows the tracks in whichever playlist is selected in the Library, with
the playing one marked, in **Title** and **Artist** columns. The columns
don't sort: the list is always in the playlist's own order, which is
the order it plays in with Shuffle off. Double-click a track to jump to
it. Drop files
here (from the Library or from Explorer) to add them to that playlist,
and **Remove from playlist** (or the Delete key) takes one out.

Adding tracks to the playlist you're currently listening to never
interrupts it: the track playing carries on, and the new tracks join the
running order without it jumping back to the top.

### The Soundboard window

A grid of programmable buttons, like a Stream Deck. Click an empty one to
pick a sound for it, or **drag sound files straight onto a button** from
Explorer. Click a filled button to fire it, sounds can overlap.
**Right-click** any button to rename it, give it a colour or a picture,
swap its sound or clear it. Buttons stay where you put them, so adding a
new sound never shuffles the board around. **+** and **-** change how
many buttons there are (a button with a sound on it is never removed),
and **Import folder...** drops everything in a folder onto the free
buttons.

**Each button has its own volume too**, as a bar along its bottom edge.
Click the *bar* to open a slider; click anywhere else on the button to
fire the sound as usual.

**Buttons can have a picture.** Right-click one and choose *Set a
picture*, or just drag an image file onto a button that already has a
sound. The picture is dimmed behind the button's name so the label stays
readable. PNG, JPEG, GIF, BMP and WebP.

Button names are what a Stream Deck sends to trigger a sound, so
renaming one means updating that button in the Stream Deck app to match.

### The Voice FX window

**Noise suppression** sits at the top, and is **off by default**. It
removes steady background noise - fan, hiss, room tone - from the gaps
between your words.

Leave it off if your mic is already quiet. It genuinely helps a noisy
one, and genuinely hurts a clean one: on a quiet mic it takes more from
your voice than from the noise. It also adds about 40ms of delay to your
voice. The line under the toggle says which state you're in.

Below that is your **microphone plugin chain**.

Click **Add VST3...** to pick the plugins you want. It opens at your
system VST3 folder, and you can select several at once. Only what you
pick appears in the list, the app doesn't trawl through everything
installed, because most of it won't be anything you'd put on a mic.
Your list is remembered between sessions, and **Forget** takes something
off it.

Clicking a plugin in your list adds it to the live chain **and opens the
plugin's own window**, so you can set it up and pick presets exactly as
you would in a DAW. Clicking a plugin's name in the *Live voice chain*
reopens that window later; **Remove** takes it out of the chain. Plugins
run on your mic in the order listed.

Plugin settings are saved as the plugin's own presets, the same way you'd
save them in a DAW - the chain itself isn't stored between sessions.

Noise suppression runs *before* your plugins, so they shape your voice
rather than your room.

---

Changing a Discord bot token/server/channel via Settings takes effect on
the next launch, not immediately. Everything else applies right away.

## Features

- Five detachable windows that snap magnetically to each other and to
  screen edges, drag as a group from the main window, and remember
  where you left them.
- As many named playlists as you like, with equal-power crossfade both
  between tracks and when you jump from one playlist to another, no
  manual DJing during a session. Shuffle is per-playlist.
- A master track library, separate from any playlist, that you build
  from linked folders (which stay up to date as you add files), one-off
  folder imports, individual tracks, or files dragged in from Windows
  Explorer - then push tracks into whichever playlists need them.
- A now-playing display with a live spectrum of the outgoing mix and a
  seek bar.
- A programmable soundboard: a grid of assignable buttons you arrange
  yourself, each with its own name, colour, volume and optional picture,
  layered independently of the music (up to 16 sounds can overlap at
  once). Assign by drag and drop, by picking a file, or by importing a
  whole folder at once.
- Volume control at every level: a master fader over everything, a trim
  per soundboard button, and a trim per track so one loud export can't
  ambush the table when shuffle reaches it.
- Live microphone processing through your own VST3 plugin chain (EQ,
  compression, noise gates, whatever you already own), added and
  removed on the fly while a session is running.
- Optional noise suppression on the mic (RNNoise), and optional
  automatic muting of your own Discord client while your mic is live
  here, so your voice doesn't arrive in the call twice.
- Everything (music, soundboard, and processed mic) is mixed in one
  place and streamed to Discord through the app's own bot connection.
  No virtual audio cable, no separate DAW routing.
- Broad format support: WAV, AIFF, FLAC, Ogg Vorbis, MP3, AAC/M4A, and
  WMA.
- Fully implements Discord's mandatory end-to-end-encrypted voice
  protocol (DAVE), the same one the official Discord client uses.

## Known limitations (beta)

- The installer isn't code-signed, so Windows SmartScreen will flag it
  on first run (see [Download](#download) above).
- The Stream Deck plugin needs to be built from source and requires
  physical Stream Deck hardware to fully test.
- Changing your Discord bot token/server/channel via Settings takes
  effect on the next launch, not immediately. The app won't drop an
  active Discord connection to reconnect with new details mid-session.
- Automatic Discord muting needs its own one-time setup (see
  [above](#optional-muting-yourself-in-discord-automatically)) and the
  redirect URI has to be `https://inkwyrd.com/rpc`, first in the list if
  you have more than one.
- Because Inkwyrd draws its own window frames, Windows Snap Layouts (the
  hover-over-maximise flyout) and Win+arrow snapping don't work on its
  windows. Inkwyrd's own snapping does.
- Soundboard buttons can't be dragged from one position to another yet -
  to move a sound, assign it to the button you want and clear the old
  one.

If you hit a bug, please open an issue on this repo with what you were
doing and (if possible) a screenshot of the status line/error.
