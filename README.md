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

Each release comes in two forms, the same program in both.

**The portable ZIP** (`InkwyrdAudio-Portable-...zip`) is the simplest,
and the one to start with. Unzip it anywhere and run
`Inkwyrd Audio.exe`. Nothing is installed, nothing is written outside
the folder you unzipped, and you remove it by deleting that folder.

**The installer** (`InkwyrdAudio-Setup-...exe`) is worth it if you'd
rather have a Start menu entry, a desktop shortcut and an uninstaller.
It installs for your own Windows account only, with no admin rights
needed, and upgrades replace the old version in place.

Your settings and playlists live in `%APPDATA%\Inkwyrd Audio` whichever
you choose, so you can switch between them without losing anything.

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

1. Get the app and run it as described under [Download](#download).
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

**Keyboard shortcuts** work whenever the Player window is focused:
**Space** play/pause, **S** stop, **M** mic on/off, **Right arrow** skip,
**Up/Down** master volume, **Esc** stop every soundboard sound.

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

Fade out is on the Stream Deck too, as **Music Fade Out**, and takes as
long as the slider here says - change it here and the Stream Deck key
follows.

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

**The search box** above the list narrows it as you type, matching on
title, artist, album, genre and filename - so untagged tracks are still
findable by what they're called on disk. Words match in any order, so
"drake blue" finds "Blue Drake". The caption says how many of how many
you're looking at, **Esc** or the **x** clears it, and the filter is
never remembered between sessions. It narrows the Folders view too:
folders with nothing matching in them drop out, and the ones left
**open themselves so you can see where the matches live**. Clearing the
search puts the tree back the way you had it.

The bottom half is **All Tracks**: every track Inkwyrd knows about,
independent of which playlists happen to use it. It stays put while you
click between playlists.

All Tracks has two views, switched with the **Table** and **Folders**
buttons next to its heading. Whichever you last used is remembered.

**Table** shows **Title, Artist, Album and Genre**, read from each file's
embedded tags. Click a column header to sort by it, and click again to
reverse. Sorting by Album keeps each record in track-number order. The
first launch after adding music reads the tags in the background, so rows
may show filenames for a moment before they fill in.

**Folders** shows the same tracks grouped by the folders they actually
live in, in the same order Windows Explorer would, with a count on each
folder. Folders that lead to a single folder are joined into one row, so
a path like `Artist\Album` isn't several clicks deep for nothing.
**Selecting a folder selects everything in it**, which is the quick way
to put a whole album into a playlist: click the folder, then **Add to
playlist**. Dragging onto the Playlist window works from the table view
only.

**Add files...** and **Add folder...** put tracks into this library.
Add folder either keeps the folder linked (files you add to it later
show up automatically) or takes a one-time copy of what's in it. You can
also **drag files and folders straight in from Windows Explorer**.

To get tracks into a playlist, select them here and click
**Add to playlist**, or **drag them onto the Playlist window** - from
either view, and dragging a folder in the Folders view takes everything
in it.

**To preview a track**, hover over it: a small play symbol appears at the
left of its row, in both the Table and Folders views. Click that to audition it, and the symbol becomes a stop
button inside a pulsing ring so it's obvious what's playing. Click it
again to stop. Only you hear a preview - it never reaches Discord, and it
plays even with Monitor off. The playlist pauses while it runs and picks
up again afterwards.

**Right-click a track** for Edit tags..., Add to playlist and Remove
from library - the same menu in both views. A track's volume and fade
are set by clicking its **Vol** column in the Table view.

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

### Editing tags

**Right-click a track in the Library or the Playlist window and choose
Edit tags...** to change what the file itself says it is: title, artist,
album, album artist, year, genre, track and disc numbers, BPM, comment,
composer and publisher, plus the cover art. Changes are written into the
file, so every other music program sees them too.

**Select several tracks first** to edit them together. Fields that differ
across the selection show `<keep>` and are left alone unless you type in
them - so you can fix an album's artist without flattening thirteen
different titles. Only the fields you actually edit are written.

Your files are handled carefully: Inkwyrd never edits a file in place. It
copies it, tags the copy, checks the copy still reads, and only then puts
it in place of the original. If anything fails, the original is untouched.

A track that's **loaded in the player can't be tagged** - Windows won't
let a file being played be replaced. Press Stop and save again. A track
being previewed is fine: the preview stops itself.

### The Soundboard window

A grid of programmable buttons, like a Stream Deck. Click an empty one to
pick a sound for it, or **drag sound files straight onto a button** from
Explorer. Click a filled button to fire it, sounds can overlap.
**Drag a button onto another to swap them.** That's how you rearrange
the board - drop one onto an empty button to move it there. Everything
travels with the button, including its name, so Stream Deck buttons keep
working afterwards.

**A button can loop.** Right-click it and choose *Loop this sound*, and
it repeats until you press it again instead of playing once - which is
what you want for rain, a tavern, wind under a scene. A looping button
is outlined while it's running and carries a small loop mark, so you can
see at a glance what's still going. The same press stops it, from the
board, from a Stream Deck, or via the Killswitch.

**Killswitch** silences every soundboard sound playing right now, loops
included, without touching the music - for when the wrong effect goes
out to the table. **Esc** does the same thing from the Player window,
and there's a **Soundboard Killswitch** Stream Deck action for it too.
Use **Stop** or **Fade out** on the Player if you want the music to stop
as well.

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

### Ducking the music while you talk

Settings has **Duck the music while my mic is live**, off until you turn
it on. With it on, the music and sound effects drop while you're
speaking and come back when you stop, so you can narrate over a bed
without riding the master fader.

Two numbers, both in dB like every other level here:

- **Drop the music by** - how far down it goes. -12 dB is a good
  starting point: clearly under your voice, not gone.
- **Speaking is louder than** - what counts as speech. Raise it if a
  noisy room holds the music down when you're not talking.

The timing is fixed and deliberately not adjustable: it ducks quickly
enough not to clip your first word, and waits a moment before coming
back, so the music doesn't surge up between sentences.

**It reads your mic after noise suppression and your plugin chain**, so
whatever you already use to clean up your voice decides what counts as
speech. A muted mic never ducks anything. Your voice itself is never
ducked, only the music under it.

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

## Skins

Inkwyrd's colours, fonts, a couple of sizes and the logo all come from a
skin, and you can write your own.

Open **Settings** and look under **Skin**. Three examples are already
there - **Amber**, **Midnight** and **High Contrast**. Pick one and the
whole app changes straight away, no restart.

### Making your own

Click **Export current...** in Settings. That writes what's on screen
now into your skins folder as a starting point, and opens the folder.
Edit the `skin.json` inside it, click **Reload**, and your changes
appear.

A skin is a folder under `%APPDATA%\Inkwyrd Audio\skins`, holding a
`skin.json` and optionally an image for the logo:

```
skins\Amber\skin.json
skins\Amber\logo.png
```

Everything in the file is optional. Leave anything out and Inkwyrd uses
its own value, so a short file is fine, and a skin written today keeps
working when a later version adds a new colour.

```json
{
  "schemaVersion": 1,
  "name": "Amber",
  "colours": { "accent": "#ffb340", "text": "#efc98a" },
  "fonts": { "title": "Segoe UI Semibold", "label": "Segoe UI", "digits": "Consolas" },
  "metrics": { "cornerRadius": 6, "titleBarHeight": 46 },
  "logo": "logo.png"
}
```

Colours are `#rrggbb`, or `#aarrggbb` if you want transparency.

### The colours, and what each one paints

| Key | Where you see it |
|---|---|
| `background` | Behind everything, and the deepest areas inside a window |
| `panelDeep` | List and table backgrounds, the album art slot |
| `panel` | A window's main body |
| `panelRaised` | Buttons, table headers, menus, empty soundboard pads |
| `titleBar` | The title bar across the top of each window |
| `titleBarText` | The window name and buttons on that bar |
| `titleBarSubtle` | The smaller second line ("AUDIO PLAYER") |
| `text` | Normal text |
| `textDim` | Captions, hints and column headings |
| `accent` | The playing track, the seek bar, sliders, selected items, the logo |
| `accentSoft` | Selected rows, scrollbars, pressed buttons |
| `outline` | Borders around panels, buttons and fields |
| `outlineFaint` | The fine lines between rows in a list |
| `warning` | Things needing attention: a missing file, a boosted track |
| `danger` | The close button when you hover it |

### Fonts, sizes and the logo

**Fonts** name a font family already installed on the machine - nothing
is bundled. `title` is for headings, `label` for ordinary text and
`digits` for the time readout, which wants a monospaced font so the
numbers don't jiggle. A name nobody has falls back to the default rather
than breaking.

**`cornerRadius`** rounds panels, buttons and fields; `0` gives square
corners. **`titleBarHeight`** is capped between 28 and 80 pixels - a
title bar too small to grab would leave a window you can't move.

**`logo`** names an image next to your `skin.json` (PNG, JPEG, GIF or
SVG) to use in place of the drawn ink bottle, in every title bar and on
the player's display. SVGs are drawn without blur, filters or text, so
stick to plain shapes. Leave `logo` out to keep the drawn mark.

### If something's wrong with a skin

Inkwyrd keeps working. A file it can't read leaves the previous look
alone and says why under the Skin picker; a single colour it can't
understand keeps that one colour and uses the rest of your file. "Inkwyrd
(built-in)" in the list always takes you back.

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
  yourself by dragging, each able to loop as an ambience bed, and each
  with its own name, colour, volume and optional picture,
  layered independently of the music (up to 16 sounds can overlap at
  once). Assign by drag and drop, by picking a file, or by importing a
  whole folder at once.
- A search box over the whole library, matching tags or filename in any
  word order, filtering both the table and the folder tree - and
  opening the tree to show where matches live.
- Optional ducking: the music drops while your mic is live and comes
  back when you stop talking, so you can narrate over it.
- An optional check for newer releases at startup. It only ever tells
  you; it never downloads anything.
- A panic control - one button, one key, or one Stream Deck press -
  that silences every soundboard sound at once without stopping the
  music.
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

If you hit a bug, please open an issue on this repo with what you were
doing and (if possible) a screenshot of the status line/error.
