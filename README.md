# Inkwyrd Audio

**Beta.** A Windows app for running the sound of a tabletop game - D&D or
anything else - over Discord.

You get music playlists that crossfade on their own, a soundboard of
effects and looping ambience, **scenes** that change the whole mood in
one press, and your own microphone run through noise suppression and
your VST3 plugins. It's all mixed in one place and sent straight into
your Discord voice channel through the app's own bot. No virtual audio
cable, no DAW, no routing.

It also works with no Discord at all, playing through your own speakers,
which is the easiest way to try it.

Inspired by Kenku FM and Winamp.

## Contents

- [What it can do](#what-it-can-do)
- [Download and install](#download-and-install)
- [Quick start](#quick-start)
- **Guides**
  - [Finding your way around](#finding-your-way-around)
  - [Playing music](#playing-music)
  - [Building your library](#building-your-library)
  - [Previewing a track](#previewing-a-track)
  - [Making and using playlists](#making-and-using-playlists)
  - [Setting a track's volume and fade](#setting-a-tracks-volume-and-fade)
  - [Editing tags and cover art](#editing-tags-and-cover-art)
  - [The soundboard](#the-soundboard)
  - [Looping ambience](#looping-ambience)
  - [Scenes](#scenes)
  - [Your microphone](#your-microphone)
  - [Ducking the music while you talk](#ducking-the-music-while-you-talk)
  - [Connecting to Discord](#connecting-to-discord)
  - [Muting yourself in Discord automatically](#muting-yourself-in-discord-automatically)
  - [Using a Stream Deck](#using-a-stream-deck)
  - [Skins](#skins)
  - [Settings, updates and your files](#settings-updates-and-your-files)
- [Keyboard shortcuts](#keyboard-shortcuts)
- [Troubleshooting](#troubleshooting)
- [Known limitations](#known-limitations)

## What it can do

**Music**
- As many playlists as you like, with smooth crossfades between tracks
  and when you switch from one playlist to another. Shuffle is set per
  playlist.
- A library of every track you've added, viewable as a sortable table
  or by folder, with a search box.
- **Preview** any track in your own headphones without the table hearing
  it.
- Per-track volume and fade length, so one loud file can't ambush the
  table.
- Loop a single track, fade the music out, and a master volume over
  everything.
- A tag editor for titles, artists, albums and cover art, written into
  the files themselves.

**Soundboard and ambience**
- A grid of buttons you arrange yourself, each with its own name,
  colour, picture and volume. Up to 16 sounds can overlap.
- Buttons can **loop**, for rain, taverns and wind under a scene.
- A **Killswitch** that silences every sound effect at once without
  touching the music.

**Scenes**
- One press sets the playlist, the looping ambience and optionally the
  volume. The whole room crossfades together, and anything already
  right is left alone.

**Your voice**
- Your mic runs through noise suppression and your own VST3 plugins (EQ,
  compression, gates) before it reaches the call.
- Optional **ducking**: the music dips while you talk.
- Optional automatic muting of your own Discord client, so your voice
  doesn't reach the call twice.

**Control and comfort**
- A **Stream Deck** plugin for skip, shuffle, mic, sound effects, fade
  out, the Killswitch and scenes.
- Keyboard shortcuts on the Player window.
- Six windows that snap together and remember where you left them.
- **Skins**: change the colours, fonts and logo, or write your own.

**Under the hood**
- Plays WAV, AIFF, FLAC, Ogg Vorbis, MP3, AAC/M4A and WMA.
- Implements Discord's end-to-end-encrypted voice protocol (DAVE), the
  same one the official Discord client uses.

## Download and install

Get the newest release from the
[Releases page](https://github.com/Troyificus/InkWyrd-Audio/releases).
Take the one at the **top of the list**: GitHub's "Latest" label skips
pre-releases, so it doesn't point at the newest version while this is a
beta.

Each release comes in two forms, the same program in both:

- **The portable ZIP** (`InkwyrdAudio-Portable-...zip`) is the simplest
  and the one to start with. Unzip it anywhere and run
  `Inkwyrd Audio.exe`. Nothing is installed, and you remove it by
  deleting the folder.
- **The installer** (`InkwyrdAudio-Setup-...exe`) adds a Start menu
  entry, a desktop shortcut and an uninstaller. It installs for your own
  Windows account only, with no admin rights needed, and upgrades
  replace the old version in place.

Your settings, playlists, soundboard and scenes live in
`%APPDATA%\Inkwyrd Audio` whichever you choose, so you can switch
between the two without losing anything.

> **Windows warnings.** Windows will probably show **"Windows protected
> your PC"** the first time you run either one, and your browser may say
> the file **"isn't commonly downloaded."** Both happen because this is a
> small project without a paid code-signing certificate yet. Click
> **More info -> Run anyway** to continue.
>
> **If Microsoft Defender blocks a download as a threat, please don't
> override it.** [Open an issue](https://github.com/Troyificus/InkWyrd-Audio/issues)
> instead. Microsoft's automatic machine-learning check (detection names
> ending in `!ml`) has flagged Inkwyrd's unsigned downloads before, while
> other antivirus products flag few or none of them, and its verdict has
> changed between builds of identical code. It has been reported to
> Microsoft as a false positive. Code signing is the long-term fix.
>
> Every release's notes list the SHA-256 checksum of both downloads.
> Check yours matches with `Get-FileHash <file>` in PowerShell.

## Quick start

This gets you from nothing to music playing, without setting up Discord.

1. **Wear headphones.** Your mic is mixed into the same output as the
   music, so speakers will feed back.
2. **Run Inkwyrd Audio.** A Setup screen appears the first time.
3. **Choose a music folder.** Under *Music folder*, click **Browse...**
   and pick a folder of music. It's the only thing Setup needs.
   Optionally pick a folder of short sound effects to put on the
   soundboard. Leave the Discord fields empty for now.
4. **Click Save & Launch.** Inkwyrd makes a playlist from your music
   folder and starts playing it.
5. **Turn Monitor on.** On the Player window, click **Monitor: Off** so
   it reads **Monitor: On**. With no Discord connection, Monitor is what
   lets you hear the mix yourself.
6. **Open the other windows** from the buttons along the bottom of the
   Player: **Playlist**, **Library**, **Voice FX**, **Soundboard**,
   **Scenes**.
7. **Look at your library.** In the Library window, the lower list is
   **All Tracks**: your music folder's tracks are already in it. That's
   where you search, preview and edit them. Add more music with **Add
   folder...** or **Add files...**.

When you're ready to play over Discord, follow
[Connecting to Discord](#connecting-to-discord).

## Finding your way around

Inkwyrd is six windows you can arrange however suits your screen:

| Window | What it's for |
|---|---|
| **Player** | The main window: what's playing, the transport buttons, volume and playback settings |
| **Playlist** | The tracks in the selected playlist |
| **Library** | Your playlists (top) and every track you've added (bottom) |
| **Voice FX** | Noise suppression and your microphone plugins |
| **Soundboard** | The grid of sound-effect buttons |
| **Scenes** | One-press setups for the whole room |

- **Open or hide a window** with the buttons along the bottom of the
  Player window. A window's X also hides it; the same button brings it
  back.
- **Windows snap together.** Drag one near another, or near the edge of
  the screen, and it pulls itself flush. Resizing snaps the same way.
- **Dragging the Player moves everything docked to it.** Dragging any
  other window pulls just that one away, so you can rearrange without
  taking the layout apart.
- **Only the Player can be minimised**, and minimising it takes every
  open window with it, then brings them all back together.
- **Closing the Player quits the app.**
- Positions, sizes and which windows were open are remembered.

**The warning banner** on the Player window is where Inkwyrd tells you
about anything that would otherwise fail silently: the audio device
didn't open, a playlist has no playable files, tracks are missing from
disk, or a file couldn't be read.

**When a newer version is out**, an **Update available** link appears
at the top of the Player window, next to Settings. Click it to open the
release page in your browser.

## Playing music

Everything here is on the **Player** window.

**The display** at the top shows the track's artist, title and cover
art, the time played and total, a live spectrum of what's being sent
out, and a **seek bar**. Click or drag the seek bar to move through the
track.

**The transport buttons:**
- **Play / Pause** - Pause keeps your place.
- **Stop** - silences the music and starts the list from the top next
  time.
- **Fade out** - lowers the music to silence, then stops. Good for
  ending a scene. It fades only the music, never your microphone.
- **Skip** - crossfades to the next track.
- **Shuffle** - on or off for the playlist that's playing.

**Master** is the volume of everything Inkwyrd sends out: your speakers
*and* Discord.

**The row below** sets how playback behaves. All of it is remembered:
- **Mic: Live / Muted** - whether your microphone is in the mix.
- **Monitor: On / Off** - whether the mix also comes out of **your own**
  speakers. Turn it **on** when you're not using Discord. Leave it
  **off** when you're in the Discord call, because you already hear
  everything through the bot, and Monitor would play it all twice,
  slightly out of step.
- **Crossfade** - whether tracks blend into each other, and over how
  many seconds.
- **Loop track** - repeats the current track instead of moving on, for
  one ambient piece you want running all session. The slider sets a
  silence between repeats; leave it on **No gap** to loop straight
  round. Skip still moves on.
- **Fade out** - how long the Fade out button takes.

**Keyboard:** click the Player window, then use **Space** to play or
pause, **S** to stop, **Right arrow** to skip, **Up/Down** for the master
volume, **M** for the mic and **Esc** for the Killswitch. See
[Keyboard shortcuts](#keyboard-shortcuts).

## Building your library

The **Library** window's lower half, **All Tracks**, is every track
you've added to Inkwyrd, whether or not it's in a playlist. It's where
you search, preview, tag and pick tracks for playlists.

**To add music:**
- Click **Add files...** to pick individual tracks, or **Add folder...**
  to add everything in a folder and its subfolders.
- Or drag files and folders straight in from Windows Explorer.

**Two ways to look at it**, switched with **Table** and **Folders** next
to the heading. Whichever you used last is remembered.

- **Table** shows **Title, Artist, Album, Genre and Length** from each
  file. Click a column heading to sort, and click again to reverse.
  Sorting by Album keeps each album in track order; sorting by Length
  puts the shortest (or longest) first. Right after adding music, rows
  may show filenames and blank lengths for a moment while the files are
  read.
- **Folders** groups tracks by the folders they're in, in the same order
  Windows Explorer uses, with a count on each folder. **Selecting a
  folder selects everything in it**, which is the quick way to put a
  whole album into a playlist.

**To search**, type in the box above the list. It matches title,
artist, album, genre and **filename**, so untagged tracks are still
findable. Words match in any order: "drake blue" finds "Blue Drake". In
the Folders view, the folders containing matches open up so you can see
where they are. **Esc** or the **x** clears the search, and the tree goes
back the way you had it. The heading shows how many tracks match, for
example "All Tracks (12 of 84)".

**To remove tracks**, select them and click **Remove** (or press
Delete). That takes them out of the library only. It never deletes the
files, and doesn't touch any playlist already using them.

## Previewing a track

A preview plays a track **in your ears only**, to check it's the one you
want, without the table hearing it.

1. Open the **Library** window.
2. In **All Tracks** (the lower list), **move your mouse over a track.**
   A small **play symbol** appears at the left of that row. This works
   in both the Table and Folders views.
3. **Click the play symbol.** The track starts, and the symbol turns into
   a **stop button inside a pulsing ring**, so you can see what's
   previewing. The heading also says what's playing.
4. **Click it again to stop.** It also stops on its own at the end of
   the track.

While a preview plays, the playlist **pauses** and picks up again
afterwards. A preview never reaches Discord, and you hear it even when
Monitor is off.

> Can't see a play symbol? Make sure **All Tracks** has tracks in it.
> If it's empty, click **Add folder...** and add your music.

## Making and using playlists

The **Library** window's upper half is your playlists. Keep as many as
you like: one per location, mood or session.

**To make a playlist**, click **New**. A box asks for its name; type it
and press **Enter**.

**To put tracks in it:**
1. Click the playlist to select it.
2. Select tracks in **All Tracks** below. Ctrl+click picks several, and
   in the Folders view a folder picks everything inside it.
3. Click **Add to playlist**, or right-click and choose **Add to
   playlist**, or **drag them onto the Playlist window**. You can drag
   from either view, or straight from Windows Explorer.

**To play a playlist**, double-click it, or select it and click
**Play**. If something is already playing, it crossfades across rather
than cutting. Switching back to a playlist later in the same session
resumes it where it left off.

**Clicking a playlist** (single click) just shows its tracks in the
Playlist window, without changing what's playing, so you can look
through one list while another plays.

**Rename** and **Delete** manage the list. **Refresh** re-reads a
playlist's linked folder (see below).

**The Playlist window** shows the selected playlist's tracks - title,
artist and length - in the order they play when Shuffle is off, with the
playing one marked.
- **Double-click a track** to jump to it.
- **Remove from playlist**, or the Delete key, takes a track out.
- Right-click a track to **Edit tags...** or remove it.
- Adding tracks to the playlist that's playing never interrupts it: the
  current track carries on and the new ones join the running order.

**The playlist made from your Setup music folder is linked to that
folder.** Music you add to the folder later joins the playlist; click
**Refresh** to pick it up straight away. Tracks in a linked playlist
can't be removed one at a time, because they come from the folder.

To see where playlists are stored (readable JSON files), use **Open
playlists folder** in Settings.

## Setting a track's volume and fade

Every track has its own **volume** and its own **fade length**. Both
belong to the file, so a track in several playlists is fixed in all of
them at once.

1. In the Library's **Table** view, find the track.
2. Click the small bar in its **Vol** column.
3. Set:
   - **Volume** - for the track that's much louder or quieter than the
     rest. The small notch on the bar is normal volume. A bar shown in
     the warning colour is boosted above normal.
   - **Fade into next** - how long *this* track takes to hand over to
     the next one, overriding the global Crossfade length. Useful for a
     track with a long tail, or one that stops dead. **Default** follows
     the global setting. Tracks with their own fade say so on their row.

A change applies straight away, even to the track that's playing.

## Editing tags and cover art

Tags are what a music file says about itself: title, artist, album and
so on. Inkwyrd shows them everywhere, and can change them.

1. **Right-click a track** in the Library or the Playlist window.
2. Choose **Edit tags...**.
3. Change any of: title, artist, album, album artist, year, genre,
   track and disc numbers, BPM, comment, composer, publisher, and the
   **cover art** (**Replace...** or **Remove**).
4. Click **Save**.

Changes are written into the file itself, so every other music program
sees them too, and the Library updates straight away.

**Editing several tracks at once:** select them first, then right-click.
Fields that differ between them show `<keep>` and are left alone unless
you type in them. So you can fix an album's artist without flattening
thirteen different titles. Only the fields you actually change are
written.

**Your files are handled carefully.** Inkwyrd never edits a file in
place. It copies it, tags the copy, checks the copy still plays, and
only then swaps it in. If anything fails, the original is untouched.

A track that's **currently loaded in the player can't be tagged**,
because Windows won't let a file in use be replaced. Press **Stop** and
save again. A track being previewed is fine: the preview stops itself.

## The soundboard

The **Soundboard** window is a grid of buttons for sound effects.

**To put a sound on a button:**
- Click an **empty button** (marked **+**) and pick a file. Pick
  several and they fill the following empty buttons.
- Or **drag sound files onto a button** from Windows Explorer.
- Or click **Import folder...** to put a whole folder's sounds onto the
  free buttons.

**To play a sound**, click its button. Sounds can overlap, and the same
button can be pressed repeatedly.

**To rearrange the board**, drag a button onto another to swap them, or
onto an empty one to move it there. Everything moves with the button,
including its name, so Stream Deck keys keep working.

**Right-click a button** to:
- **Rename** it. The name is what a Stream Deck key uses to find it, so
  update the key if you rename.
- Set its **Volume** (or click the thin bar along its bottom edge).
- Choose a **Colour**.
- **Set a picture** (PNG, JPEG, GIF, BMP or WebP). You can also drag an
  image onto a button that already has a sound. The picture is dimmed so
  the name stays readable.
- **Loop this sound** - see [Looping ambience](#looping-ambience).
- **Replace** the sound or **Clear** the button.

**+** and **-** at the top add or remove buttons. A button with a sound
on it is never removed.

**Killswitch** (top of the window, or **Esc** on the Player) instantly
silences every sound effect playing, loops included. It never touches
the music: use **Stop** or **Fade out** on the Player for that.

A button whose file has gone missing says **(file missing)** rather than
silently doing nothing.

## Looping ambience

Any soundboard button can loop instead of playing once. That's what you
want for rain, a crackling fire, a tavern crowd or wind: sounds that
should run under the music until you say otherwise.

1. **Right-click** the button on the Soundboard.
2. Choose **Loop this sound**. A small loop mark appears on it.
3. **Click the button to start it.** It's outlined while it's running.
4. **Click it again to stop it.**

The same press starts and stops it from a Stream Deck too, and the
Killswitch stops it. Each loop keeps its own volume, so you can set an
ambience bed quietly under the music and leave it.

Loops are what [scenes](#scenes) switch on and off.

## Scenes

A scene is one press that sets the whole room: **which playlist plays,
which looping sounds are running, and (if you choose) the master
volume.** For example "Tavern", "Road", "Combat" or "Storm".

Open the **Scenes** window from the **Scenes** button on the Player.

**To make a scene:**
1. Set the room up the way you want it: play the playlist, start the
   looping sounds, set the volume.
2. In the Scenes window, click **+ Save current as scene**.
3. A dialog opens, already filled in from what's playing. Give it a
   name, untick anything you don't want in it, and pick a colour.
4. Choose what it does to the music:
   - **Play a playlist** - the usual choice.
   - **Fade the music out** - for a deliberately quiet scene.
   - **Leave the music alone** - for ambience-only scenes like "it
     starts raining", which shouldn't interrupt the track.
5. Tick **Set the master volume to** only if you want this scene to
   change the volume. It's off by default, because the master volume is
   also what Discord hears.
6. Click **Save**.

**To use a scene, click it.** Here's what happens:
- The music crossfades to the scene's playlist. **If that playlist is
  already playing, it's left alone**, so pressing Combat during combat
  never restarts the fight music.
- The scene's loops fade in and any other running loops fade out. **A
  loop both scenes share keeps going without a break**, so rain carries
  on from Road into Storm.
- If the scene sets the volume, it glides there. Grab the Master slider
  yourself and the glide stops.
- Sound effects that aren't loops are never touched.
- **Pressing the scene you're already in puts it back.** If the
  Killswitch stopped the ambience, one press brings it back without
  restarting anything that's still right.

The scene you last pressed is outlined. A scene change takes as long as
the Player's crossfade setting, and never less than a second.

**Right-click a scene** to:
- **Update from what's playing now** - the easiest way to change one.
  Set the room up again, then update.
- **Edit...** - change it without having to play it first.
- **Move earlier / Move later** - reorder.
- **Delete...** - removes the scene only. The playlist and soundboard
  are untouched.

**If something a scene uses disappears** (its playlist is deleted, or a
soundboard button is cleared or stops looping), its button says how
many things are missing and hovering over it says which. The rest of
the scene still works. **Renaming a soundboard button updates every
scene that uses it**, and moving buttons around the board changes
nothing.

## Your microphone

Your mic is mixed in with the music and sent to Discord. The **Mic**
button on the Player (or **M**) turns it on and off, and the setting is
remembered.

The **Voice FX** window shapes how you sound.

**Noise suppression** (at the top, off by default) removes steady
background noise - a fan, hiss, room tone - from the gaps between your
words. Turn it on for a noisy mic and leave it off for a quiet one: on a
clean mic it takes more from your voice than from the noise. It adds
about 40ms of delay. The line under the switch says which state you're
in.

**Your plugin chain** runs your mic through VST3 plugins you already own
(EQ, compression, a noise gate and so on).
1. Click **Add VST3...**. It opens at your system VST3 folder. Pick the
   plugins you want; you can select several. They're added to your list,
   which is remembered.
2. Click a plugin in your list to put it in the **live chain**. Its own
   window opens so you can set it up or pick a preset, as you would in a
   DAW.
3. Click a plugin's name in the live chain to reopen its window, or
   **Remove** to take it out. Plugins run in the order listed.
4. **Forget** takes a plugin off your list.

Save your settings as each plugin's own presets. The chain itself isn't
remembered between sessions. Noise suppression runs before your
plugins, so they shape your voice rather than your room.

## Ducking the music while you talk

With ducking on, the music and sound effects dip while you're speaking
and come back when you stop, so you can narrate without riding the
Master slider.

1. Open **Settings** from the Player.
2. Under **Duck the music while my mic is live**, tick **Enable**.
3. Set:
   - **Drop the music by** - how far it dips. **-12 dB** is a good start:
     clearly under your voice but not gone.
   - **Speaking is louder than** - what counts as speech. Raise it if
     background noise keeps the music dipped when you're not talking.
4. Click **Save & Apply**. It takes effect straight away.

The timing is fixed: it dips fast enough not to clip your first word,
and waits a moment before coming back so the music doesn't surge up
between sentences. It listens to your mic *after* noise suppression and
your plugins, so a noise gate you already use decides what counts as
speech. A muted mic never ducks anything, and your voice itself is
never ducked.

## Connecting to Discord

Inkwyrd plays into Discord as **its own bot**, so you need a bot of your
own. It's free and takes about five minutes.

**Create the bot:**
1. Go to the [Discord Developer Portal](https://discord.com/developers/applications)
   and sign in.
2. Click **New Application**, give it a name (for example "Table
   Audio"), and create it.
3. Open the **Bot** tab. Click **Reset Token** and copy the token. Keep
   it private: anyone with it can control the bot.
4. You don't need any of the "Privileged Gateway Intents". The bot only
   joins voice channels; it never reads messages.

**Invite it to your server:**
5. Open **OAuth2 -> URL Generator**. Under **Scopes** tick `bot`; under
   **Bot Permissions** tick `Connect` and `Speak`. Copy the URL at the
   bottom.
6. Open that URL in your browser, pick your server, and click
   **Authorize**. The bot appears in your member list (offline until
   Inkwyrd connects it).

**Get the server and channel IDs:**
7. In Discord, turn on **User Settings -> Advanced -> Developer Mode**.
8. Right-click your server's icon and **Copy Server ID**.
9. Right-click the voice channel the bot should join and **Copy Channel
   ID**.

**Put them into Inkwyrd:**
10. Open **Settings**, paste the **Bot token**, **Server (guild) ID** and
    **Voice channel ID**, and click **Save & Apply**.
11. Inkwyrd connects straight away, and the status line at the top of
    the Player shows how it's going. **Changing** these details later,
    once it has connected, takes effect the next time Inkwyrd starts; it
    offers to restart for you.

**Once you're connected, turn Monitor off.** You're in the call too, so
you already hear everything through the bot.

## Muting yourself in Discord automatically

When your mic is live in Inkwyrd, your voice reaches the call twice:
once from your own Discord client and again through the bot. Inkwyrd can
mute **you** in Discord whenever your mic is live here, and unmute you
when it isn't.

This is optional. If you only use Inkwyrd for music and effects, skip
it. It uses the **same** Discord application as your bot:

1. **Add a redirect.** In the Developer Portal, open your app, go to
   **OAuth2 -> Redirects**, click **Add Redirect**, enter
   `https://inkwyrd.com/rpc` exactly, and **Save Changes**. Nothing is
   ever sent there - Discord just requires one. It must be that exact
   address, and first in the list if you have several.
2. **Fully quit Discord** - right-click it in the system tray and choose
   **Quit Discord** - then start it again. Discord only reads your
   application's settings when it starts.
3. **Copy the client secret:** Developer Portal -> your app -> **OAuth2**
   -> **Client Secret** -> **Reset Secret**, then copy it. Keep it
   private.
4. **In Inkwyrd**, open **Settings**. Under *Mute me in Discord while my
   mic is live*, paste the client secret, tick **Enable**, and click
   **Authorise...**. Discord shows a consent box: click **Authorize**.
   The status line under the field confirms it.
5. Click **Save & Apply**.

It mutes your own Discord client on your own machine, so it needs no
special server permissions and works in any server.

## Using a Stream Deck

Inkwyrd has a plugin for Elgato's Stream Deck. Every key works while
Inkwyrd is running.

| Action | What it does |
|---|---|
| **Skip Track** | Crossfades to the next track |
| **Toggle Shuffle** | Turns shuffle on or off |
| **Toggle Mic Mute** | Turns your mic on or off |
| **Music Fade Out** | Same as the Player's Fade out button, taking the same time |
| **Soundboard** | Plays one soundboard sound, or starts/stops a loop |
| **Soundboard Killswitch** | Silences every sound effect; the music keeps playing |
| **Scene** | Switches to a scene |

**Installing it.** The plugin isn't in the ZIP or installer yet - it's
built from this repository's source. You need the Stream Deck app
(version 6.5 or later) and [Node.js](https://nodejs.org/).

1. Download this repository (**Code -> Download ZIP** on GitHub, or
   `git clone`).
2. Open a terminal in its `streamdeck-plugin` folder and run:
   ```
   npm install
   npm run build
   npx @elgato/cli link com.inkwyrd.audiodeck.sdPlugin
   ```
3. Quit the Stream Deck app from its tray icon and start it again.
   **Inkwyrd Audio Deck** appears in its list of actions.

**Setting up the keys.** Drag actions onto keys. For a **Soundboard**
key, select it and type the button's name **exactly as it reads on the
Soundboard** (capitals and spaces count). For a **Scene** key, type the
scene's name (capitals don't matter).

**What the key tells you:**
- A **tick** means Inkwyrd received the press.
- A **warning triangle** means Inkwyrd isn't running, or a Soundboard or
  Scene key has no name set. A press that didn't reach Inkwyrd is
  dropped. It never fires later when Inkwyrd starts.
- A tick with nothing happening usually means a mistyped name.

There's more detail in [streamdeck-plugin/README.md](streamdeck-plugin/README.md).

## Skins

A skin changes Inkwyrd's colours, fonts, corner rounding, title bar
height and logo.

**To change skin**, open **Settings** and pick one under **Skin**. It
applies straight away. Three examples are included - **Amber**,
**Midnight** and **High Contrast** - and **Inkwyrd (built-in)** always
takes you back.

**To make your own:**
1. Pick the skin closest to what you want, then click **Export
   current...**. That saves it into your skins folder and opens the
   folder.
2. Edit the `skin.json` inside it.
3. Click **Reload** in Settings to see your changes.

A skin is a folder under `%APPDATA%\Inkwyrd Audio\skins`, holding a
`skin.json` and optionally a logo image:

```
skins\Amber\skin.json
skins\Amber\logo.png
```

Everything in the file is optional. Anything you leave out uses
Inkwyrd's own value, so a short file is fine.

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

Colours are `#rrggbb`, or `#aarrggbb` for transparency.

| Colour key | Where you see it |
|---|---|
| `background` | Behind everything, and the deepest areas inside a window |
| `panelDeep` | List and table backgrounds, the cover art slot |
| `panel` | A window's main body |
| `panelRaised` | Buttons, table headings, menus, empty soundboard buttons |
| `titleBar` | The bar across the top of each window |
| `titleBarText` | The window name and buttons on that bar |
| `titleBarSubtle` | The smaller second line ("AUDIO PLAYER") |
| `text` | Normal text |
| `textDim` | Captions, hints and column headings |
| `accent` | The playing track, the seek bar, sliders, selected items, the logo |
| `accentSoft` | Selected rows, scrollbars, pressed buttons |
| `outline` | Borders around panels, buttons and fields |
| `outlineFaint` | The fine lines between rows in a list |
| `warning` | Things needing attention: a missing file, a boosted track |
| `danger` | The close button when you hover over it |

**Fonts** name a font already installed on the computer; nothing is
bundled. `title` is for headings, `label` for ordinary text, and
`digits` for the time readout (a monospaced font stops the numbers
jumping about). A font that isn't installed falls back to the default.

**`cornerRadius`** rounds panels, buttons and fields (`0` for square).
**`titleBarHeight`** is kept between 28 and 80 pixels, so a window can
always be grabbed.

**`logo`** names an image next to your `skin.json` (PNG, JPEG, GIF or
SVG) to use instead of the drawn ink bottle, in every title bar and on
the Player's display. SVGs are drawn without blur, filters or text, so
keep them to plain shapes.

**If something's wrong with a skin**, Inkwyrd keeps working. A file it
can't read leaves the previous look in place and says why under the
Skin picker. A single colour it can't understand keeps that one colour
and uses the rest of your file.

## Settings, updates and your files

**Settings** opens from the button at the top of the Player window. It
floats over the other windows while it's open.

| Setting | What it does |
|---|---|
| **Music folder** | Required on first run. Becomes your first playlist, linked to the folder |
| **Sound effects folder** | Optional. Its sounds are added to free soundboard buttons |
| **Discord bot** | Token, server and channel - see [Connecting to Discord](#connecting-to-discord) |
| **Mute me in Discord** | See [Muting yourself in Discord automatically](#muting-yourself-in-discord-automatically) |
| **Duck the music** | See [Ducking the music while you talk](#ducking-the-music-while-you-talk) |
| **Tell me when a newer release exists** | Checks GitHub once at startup |
| **Playlist files** | Opens the folder your playlists are saved in |
| **Skin** | See [Skins](#skins) |

Click **Save & Apply** to keep your changes. **Closing Settings with its
X discards them.** Everything applies straight away, except changing
Discord details after Inkwyrd has already connected: that takes effect
the next time it starts, and it offers to restart. Your version number
is shown at the bottom right.

**Updates.** With the update check on, Inkwyrd asks GitHub once at
startup whether a newer release exists. If one does, an **Update
available** link appears at the top of the Player window: click it to
open the release page in your browser, or hover over it to see the
address first. **It never downloads or installs anything** - you get the
new version from that page yourself.

**Your files** all live in `%APPDATA%\Inkwyrd Audio` (paste that into
Explorer's address bar to open it): settings, playlists, the
soundboard, scenes, your track library, per-track volumes, cached tags,
your plugin list and skins. Copy that folder to back everything up.

**Inkwyrd never overwrites a file it can't safely read.** If one was
written by a newer version of Inkwyrd, or has been damaged, it's left
exactly as it is and the warning banner tells you. Changes to that part
of the app won't be saved until you go back to the newer version or fix
the file, so nothing in it is lost. (The one exception is the tag cache,
which is rebuilt, because tags can always be read again from your
tracks.)

## Keyboard shortcuts

**Player window** (click it first):

| Key | Does |
|---|---|
| **Space** | Play / pause |
| **S** | Stop |
| **Right arrow** | Skip to the next track |
| **Up / Down** | Master volume up / down |
| **M** | Mic on / off |
| **Esc** | Killswitch: silence every sound effect |

**Elsewhere:**

| Where | Key | Does |
|---|---|---|
| Library search box | **Esc** | Clear the search |
| Library track list | **Delete** | Remove the selected tracks from the library |
| Playlist window | **Delete** | Remove the selected track from the playlist |
| Naming a new playlist | **Enter** | Save the name |

Shortcuts only work while Inkwyrd's window has focus. For control while
you're in another program, use a [Stream Deck](#using-a-stream-deck).

## Troubleshooting

**I can't hear anything.**
If you're not connected to Discord, turn **Monitor** on - without
Discord it's the only way to hear the mix. Also check the **Master**
slider, and the Player's warning banner (it says if the audio device
failed to open, or a playlist has no playable files).

**I hear everything twice, slightly out of step.**
Monitor is on while you're in the Discord call. Turn **Monitor** off:
you already hear the bot in the call.

**People hear my voice twice.**
Your own Discord mic and Inkwyrd's are both live. Set up
[automatic muting](#muting-yourself-in-discord-automatically), or mute
yourself in Discord.

**There's feedback or echo from my mic.**
Wear headphones. Your mic is mixed into the same output as the music.

**I can't find how to preview a track.**
Hover over a track in the Library's **All Tracks** list and click the
play symbol that appears. If All Tracks is empty, click **Add
folder...** and add your music first. See
[Previewing a track](#previewing-a-track).

**My Discord changes didn't do anything.**
Once Inkwyrd has connected, changes to the bot token, server or channel
apply the next time it starts. Accept the restart it offers, or close
and reopen it.

**"Can't tag this track."**
It's loaded in the player. Press **Stop**, then save again.

**The warning banner says a file is being kept as it is.**
That file was written by a newer version of Inkwyrd, or is damaged. It
hasn't been touched, but changes to that part of the app won't be saved.
Go back to the newer version, or restore the file from a backup. See
[Settings, updates and your files](#settings-updates-and-your-files).

**A Stream Deck key shows a warning triangle.**
Inkwyrd isn't running, or the key needs a sound or scene name. See
[Using a Stream Deck](#using-a-stream-deck).

**Windows or Defender blocked the download.**
See the note under [Download and install](#download-and-install).

## Known limitations

- The downloads aren't code-signed yet, so Windows warns about them on
  first run.
- The Stream Deck plugin has to be built from source.
- Changing your Discord bot token, server or channel after Inkwyrd has
  connected takes effect the next time it starts. It won't drop a live
  connection to reconnect mid-session.
- Keyboard shortcuts only work while Inkwyrd has focus. Global hotkeys
  are planned.
- Because Inkwyrd draws its own window frames, Windows Snap Layouts
  (the flyout over the maximise button) and Win+arrow snapping don't
  work on its windows. Inkwyrd's own snapping does.

**Found a bug?** Please [open an issue](https://github.com/Troyificus/InkWyrd-Audio/issues)
saying what you were doing, with a screenshot of the Player's status
line or warning banner if you can.
