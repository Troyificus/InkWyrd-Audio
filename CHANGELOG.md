# Changelog

Every release of Inkwyrd Audio so far, newest first.

The releases before **0.1.1-beta** were published as GitHub Releases and
have since been removed - there were forty-six of them and the list had
become unreadable. Their notes are kept here so the history of what
changed, and when, isn't lost. **Their download links and checksums are
gone with them**, so those sections have been stripped: the only builds
you can download are the current ones on the
[releases page](https://github.com/Troyificus/InkWyrd-Audio/releases).

Numbering changed at 0.1.1-beta - see the release process section of
CLAUDE.md. Everything below `0.1.1-beta` uses the older
`0.1.0-beta.NN` scheme, where the leading `0.1.0` never moved.

## v0.1.5-beta - the bot that joined too early

*2026-10-04*

Fixes a silence that had an obvious-looking cause and a completely
different real one.

### "The bot's in the channel but nobody can hear anything"

If Inkwyrd joined the voice channel **before anyone else was in it**, the
music would play, the Monitor button would prove it was playing, and
Discord would get nothing at all. Rejoining didn't help. The only way
through was to be in the voice channel yourself before starting Inkwyrd.

Here's what was actually happening. Discord encrypts voice with a group
key, and it doesn't finish setting that up until there's somebody to
share it with, so a bot that joins an empty channel waits. Inkwyrd knew
that and waited, which is right. What it didn't know is that **Discord
ends a call whose channel stays empty**, and when it did, Inkwyrd carried
on waiting on a connection that no longer existed. Discord offered it a
fresh connection four times over; nothing was listening.

**Inkwyrd now watches its voice connection and rebuilds it whenever it
drops.** Join an empty channel, go and make a cup of tea, and when your
players arrive the sound is there. It also covers Discord moving bots
between its own servers mid-session, which it does unannounced and which
had the same effect.

Retries back off and keep going, so there's no window where it gives up
and needs restarting. If you're watching the status line you'll see
"Reconnecting to the voice channel..." rather than silence.

**One thing to know:** this is the hardest part of the app to test, and
it can only really be proven on a live call. If you see it drop and fail
to come back, the log will say so (**Settings -> Open log folder**), and
that's worth an issue.

## v0.1.4-beta - why the bot won't join

*2026-10-04*

When a bot won't join a voice channel, Discord says nothing at all. This
release works out why and tells you.

### "It connects, but the bot never joins the channel"

If Discord won't let a bot into a voice channel, it doesn't refuse. It
sends no error, no warning, nothing: the join just never happens. Until
now all Inkwyrd could say was "Timed out waiting for voice server info,
check the server/channel IDs", which names two things to check and helps
with neither.

It doesn't have to guess. Discord already sends the full list of channels
in your server, with their names and types, before the join is even
attempted. Inkwyrd now keeps that list and uses it, so instead of a
timeout you get the actual reason:

- **Is it a private channel?** The usual answer, and the hardest to spot.
  A private channel only lets in the roles and people named in its own
  permissions, and a bot you've just invited isn't one of them. The
  Connect permission on the invite link covers the server as a whole and
  doesn't override a single channel's settings. Inkwyrd now says so, and
  tells you where to fix it.
- **Did you copy a text channel's ID?** Easy to do, since it's the one
  you can click into. Inkwyrd names the channel you actually picked, so
  it's obvious which one it was.
- **A category, a forum, or a Stage channel?** Each gets its own
  explanation rather than a silent failure.
- **Is the ID in that server at all?** Copying from a different server,
  or copying the server's own ID, gives you a number that looks right and
  isn't.
- **Is the channel full?** A bot can't go over a user limit.

The setup guide in the README now covers private channels at the step
where you copy the channel ID, and there's a troubleshooting entry for
it.

## v0.1.3-beta - the double paste, fixed properly

*2026-10-03*

0.1.2-beta tried to stop Discord details being pasted in twice. It didn't
work. This one fixes it properly, and clears up two things the attempt
broke along the way.

### It now repairs a double paste instead of trying to prevent it

If a paste lands next to what's already in a box rather than replacing
it, you end up with the same value in there twice, end to end. Discord
rejects a token like that, which leaves your bot sitting offline with
nothing on screen to say why. The token box shows dots rather than
characters, so there's nothing to see either.

0.1.2-beta tried to stop that happening. It still happened.

So Inkwyrd now **fixes it instead of preventing it**. If any of your
Discord details have the same value in twice, it keeps the single correct
copy and tells you it did. That happens when you press Save, and once
automatically when Inkwyrd starts.

**That second one matters if you're affected right now.** A saved token
that's doubled can't connect at all, so there's no way to get at it
through the app. Just start this version and it sorts itself out. The log
will say which fields it repaired (never what they contain).

Halving a doubled value gives back exactly what you pasted, so nothing is
guessed at, and it can't go off on a value that's actually fine.

### Two things 0.1.2-beta got wrong

**Boxes stayed highlighted after you clicked away.** Clicking from one
Discord box to another left the first one still showing a highlighted
block, so two looked active at once. The highlight now clears when you
move on.

**Settings sat on top of everything.** It was meant to stay above
Inkwyrd's own windows so it wouldn't get lost behind them. Instead it
floated over your browser, your game and Discord too, until you closed
it. It now steps behind as soon as you switch to another program, and
comes back to the front when you return to Inkwyrd.

## v0.1.2-beta - a setup trap that left bots offline

*2026-10-03*

A hotfix for a setup trap that could leave your bot sitting offline, with
nothing on screen to say why.

### If your bot won't connect, this is probably why

Settings fills its boxes with whatever you saved last time. Click into
one of them and paste, and Windows put the new text *next to* what was
already there instead of replacing it. Do that with your bot token and
you end up with the token stored twice over, end to end, which Discord
rejects.

The token box shows dots rather than characters, so there was no way to
see it had happened. The server and channel IDs could double up the same
way, and 36 digits looks much like 18 at a glance.

**The fix:** clicking into any of those boxes now selects what's in it,
so pasting replaces it, the way you'd expect.

**And Inkwyrd now checks before saving.** Paste something that can't be
right and it says so straight away, including the specific case above:
"That bot token looks like it was pasted twice." Paste the Application ID
where the token goes (they sit on neighbouring pages in Discord's
Developer Portal) and it tells you that too, instead of letting you find
out minutes later.

**If you're already affected**, open Settings, click each of the Discord
boxes in turn and paste again. The click now selects the old contents, so
a single paste is enough to put it right.

### It now tells you what Discord actually said

When a connection failed, Inkwyrd said "Timed out waiting for Discord
gateway". That reads like a network problem, and sends you off checking
your internet, your firewall and your channel ID, when Discord had
plainly said the token was wrong.

It now reports the real reason: a rejected token says so, privileged
intents say so, rate limiting says so. It also gives up as soon as
Discord hangs up, rather than sitting there for ten seconds waiting for
an answer that already arrived.

## v0.1.1-beta - ready for other people to run

*2026-09-28*

The release that makes Inkwyrd ready for people other than me to run.

Nothing here changes what the app does at the table. It's the groundwork
for a public beta: the program now starts on computers where it simply
wouldn't before, it leaves something behind when it crashes, and it has a
licence.

### It now starts on a clean PC

Inkwyrd needs Microsoft's Visual C++ runtime, and **neither download
included it**. On a PC that happened to have it - most machines that run
games do - everything worked. On one that didn't, the app died before it
opened, showing a Windows error naming a `.dll` and never mentioning
Inkwyrd at all.

Both downloads now carry that runtime. There's nothing to install first.

If you ever had Inkwyrd refuse to open with an error about a missing
`MSVCP140.dll`, this was why, and this fixes it.

### When something goes wrong, there's now a record

- **Logs live in `%APPDATA%\Inkwyrd Audio\logs`**, and Settings has an
  **Open log folder** button to take you there.
- **The previous run is kept** as `log-previous.txt`. Reopening Inkwyrd
  after a crash used to wipe the only evidence of it before you could
  report anything.
- **A crash now writes a report** into the log, with the time and the
  version, instead of the app vanishing silently.
- Logs never contain your Discord bot token.

If you hit a problem, attaching those two files to a bug report is the
most useful thing you can do.

### Reporting things got easier

There are now proper forms for a
[bug report](https://github.com/Troyificus/InkWyrd-Audio/issues/new?template=bug_report.yml)
and a
[feature request](https://github.com/Troyificus/InkWyrd-Audio/issues/new?template=feature_request.yml).
The feature one asks what you're trying to do *at the table*, which is
usually more useful than a description of the button you had in mind.

### A security fix worth mentioning

Inkwyrd listens on your own machine for Stream Deck key presses. Because
of how browsers work, a web page you happened to have open could reach
that and send commands - stopping your sounds in the middle of a session,
for instance. It can't read anything, but it shouldn't be able to do that
either. Web pages are now refused. Your Stream Deck is unaffected.

### Inkwyrd has a licence now

**It's free to use, for anything, on as many of your own computers as you
like** - that hasn't changed and won't. The source stays public so you can
see exactly what the program does before running it, which matters while
the downloads aren't code-signed.

What's new is that it's written down: you can use it, you can't
redistribute it or publish your own version of it. See
[LICENSE](https://github.com/Troyificus/InkWyrd-Audio/blob/main/LICENSE).
If you want to do something it doesn't allow, ask - it's a small project
and the answer may well be yes.

### About the version number

**This is `0.1.1-beta`, not `beta.34`.** Every release so far was
`0.1.0-beta.something`, so the version never actually moved off `0.1.0`.
From now on it's an ordinary version number: the last digit for fixes,
the middle for new beta versions, and `1.0.0` when it's finished.

**The old releases have been cleared out** - there were forty-six and the
list was unreadable. What changed in each one is kept in
[CHANGELOG.md](https://github.com/Troyificus/InkWyrd-Audio/blob/main/CHANGELOG.md).

Your settings, playlists, scenes, soundboard and skins are untouched by
any of this - upgrade over the top as usual.
## v0.1.0-beta.33 - sounds that fade, and sounds that play by themselves

*2026-09-24*

Soundboard sounds can fade in and out, play by themselves at random, and scenes remember both.

### Fade in and fade out

Right-click any soundboard button and choose a length under **Fade in**
and **Fade out** - from half a second to ten seconds - or **Off**.

- **Fade in** happens every time the sound starts: a click, a Stream Deck
  key, a random play or a scene.
- **Fade out** happens when a loop is stopped. On a sound that plays
  once, the end of the sound fades away instead of cutting off.
- The **Killswitch** still stops everything at once.

### Sounds that play by themselves

Right-click a sound effect and choose **Play randomly** - the seagull
over the waves, distant thunder, a creak in an old house:

- **Low** - every 2 to 5 minutes
- **Medium** - every 45 seconds to 2 minutes
- **High** - every 15 to 45 seconds

Each gap is random within its range, so it never falls into a pattern,
and the first one comes sooner so you can tell it's on. A small **die**
shows on the button while it's playing randomly. **Clicking the button
still plays it straight away**, and it never plays over itself. Choose
**Off**, or press the Killswitch, to stop it.

Random play is for sounds that play once - a looping button can't play
randomly.

### Scenes remember both

Saving a scene now records which sounds are playing randomly (and how
often), and each sound's fades. The scene keeps its own copy, so the
same rain can swell in slowly in "Storm" and cut in at once in "Combat" -
change them in the scene's **Edit** dialog without touching the button.

In a scene, a fade can be **Scene transition** (the scene's normal
change time), **Off** (cut straight in or out) or a length. Your
existing scenes behave exactly as before.

### Also in this release

- The installer's third-party notices now list **TagLib** (the library
  that reads and writes track tags), with where its source is.

### Before you upgrade

This version saves the soundboard and scenes in a newer format. If you go
back to an older beta afterwards, it will leave those two files alone
rather than edit them - nothing is lost, but the older beta can't change
them.

## v0.1.0-beta.32 - pixel skins in every colour, and skins that stay up to date

*2026-09-24*

Pixel-art skins in every colour scheme, and included skins that stay up to date.

### Seven skins, two styles

Every colour scheme now comes in both a flat and a pixel-art version.
Pick one under **Settings -> Skin**:

| Colours | Flat | Pixel art |
|---|---|---|
| Green | Inkwyrd (built-in) | Pixel Phosphor |
| Amber | Amber | **Pixel Amber** (new) |
| Blue | Midnight | **Pixel Midnight** (new) |
| Black, white and yellow | High Contrast | **Pixel High Contrast** (new) |

The pixel-art skins draw the buttons, sliders and window frames from
pictures and use the Silkscreen pixel font.

### Included skins now get updated

When a new version of Inkwyrd improves one of its included skins, your
copy is replaced with the new one - **but only if you haven't changed
it**. If you've edited any of its files, Inkwyrd leaves it exactly as it
is. If you've deleted one, it stays deleted.

This release uses that straight away: if you had **Pixel Phosphor** from
beta.31, it's updated to the version with the Silkscreen font.

Skins can also carry a `version` number of their own now - the README's
**Skins** section has the details for anyone making skins.

## v0.1.0-beta.31 - sprite skins and a pixel-art look

*2026-09-23*

A pixel-art skin, skins that can draw the controls themselves, and two window fixes.

### Pixel Phosphor

A new example skin in the spirit of late-90s media players: bevelled
buttons, lit toggles, pixel icons on the transport, a scanline display and
the **Silkscreen** pixel font throughout. Pick it under **Settings -> Skin**.

It's written into your skins folder the first time you start this version,
next to Amber, Midnight and High Contrast.

### Skins can now draw the controls

A skin can now include a sprite sheet: one image holding pictures for
buttons, sliders, tick boxes, scroll bars, window frames, the title bar and
the Player's display. Each picture stretches from its middle and keeps its
corners, so the windows stay resizable. Anything a skin doesn't draw keeps
the normal look.

A skin can also bring its **own font files**, so it looks right on
computers that don't have the font installed.

The README's **Skins** section explains the format. To make your own,
start from Pixel Phosphor's folder: open its `sprites.png` and redraw it.

### Fixes and small changes

- **No more white flash when resizing a window.** Dragging a window's
  corner used to show a white band at the growing edge.
- **No more thin grey line between docked windows.**
- **Shuffle, Mic, Monitor, Crossfade and Loop light up while they're on.**
  A muted mic lights up amber, so it stands out.
- Buttons and captions on the Player now size themselves to their text, so
  a wide skin font never looks squashed.

## v0.1.0-beta.30 - reorder your playlists

*2026-09-22*

Reorder a playlist in the Playlist window.

### Moving tracks

The order in the Playlist window is the order the playlist plays in with
Shuffle off, and you can now change it:

- **Drag a selected track** (or several) up or down and drop it where
  you want. A line shows where it will land, and the list scrolls if you
  drag past the top or bottom.
- **Or press the Up / Down arrow keys** to walk the selected track
  through the list. It stays selected, so you can keep pressing.

Select first, then drag: dragging a track that *isn't* selected starts a
selection instead, the way it does in File Explorer.

Several tracks selected move together and keep their order.

**Tracks that came from a linked folder** - the playlist Inkwyrd made
from your Setup music folder - play in the folder's own order and can't
be moved one at a time. Add tracks individually if you want to arrange
them yourself.

The playlist that's currently playing can be reordered too, without
interrupting the track that's playing.

## v0.1.0-beta.29 - multi-select in playlists, and a mic volume

*2026-09-22*

Select many tracks in a playlist at once, and a volume for your mic.

### Selecting tracks in the Playlist window

You can now select as many tracks as you like, the usual ways:

- **Click and drag** down or up the list to select a run of tracks.
  Drag past the top or bottom and the list scrolls.
- **Click one, then Shift+click another** to select everything between.
- **Ctrl+click** to pick tracks one at a time.
- **Ctrl+A** selects the whole playlist.

**Remove from playlist** (or Delete) then takes out everything selected,
and right-click **Edit tags...** edits them all together. Removing more
than one track asks first, since there's no undo. The files themselves
are never deleted, and stay in your library.

### A volume for your mic

A new **Mic** slider on the Player, to the left of Master, sets how loud
**your voice** is without touching the music or sound effects - for when
you're too loud in the call. It's remembered between sessions.

It doesn't change what ducking counts as speech, so turning yourself
down won't stop the music dipping when you talk.

## v0.1.0-beta.28.1 - music in the Library from the start, and a clickable update link

*2026-09-22*

Two fixes.

### Your music shows up in the Library straight away

On a new install, the music folder you choose in Setup became a playlist
and started playing - but its tracks **never appeared in the Library's
All Tracks list**. That list is where you preview, search and edit
tracks, so on a fresh install none of that was possible until you added
the folder again by hand.

Now the Setup folder's tracks go straight into All Tracks.

**If you set Inkwyrd up before this version**, the next launch adds your
Setup folder's tracks to All Tracks once, automatically. Anything
already there is left as it is.

### The update notice is a link you can click

When a newer version is out, an **Update available** link now appears at
the top of the Player window, next to Settings. Click it to open the
release page in your browser; hover over it to see the address first.
Before, it was a line of text in the warning banner that couldn't be
clicked or copied.

The link only ever opens this project's own GitHub pages.

## v0.1.0-beta.28 - track lengths

*2026-09-22*

Track lengths in the Library and Playlist windows.

### Length columns

- **Library:** the All Tracks table has a new **Length** column. Click
  its heading to sort by length - shortest first, click again for
  longest first.
- **Playlist window:** each track now shows its length next to the title
  and artist.

Lengths show as `3:07`, or `1:02:45` for anything an hour or longer.

**Your existing library fills in by itself.** The first time this version
starts, it measures each track once in the background, so lengths appear
over a few moments rather than all at once. After that they're
remembered, and only new or changed files are measured again.

A blank length means the track hasn't been measured yet - or, rarely,
that nothing could tell how long the file is.

## v0.1.0-beta.27.1 - your files are never overwritten by a version that couldn't read them

*2026-09-21*

A data-safety fix. Worth updating to, especially if you ever run more than one version of Inkwyrd.

### Your files are no longer overwritten by a version that couldn't read them

Inkwyrd refuses to load a file it can't safely read - one written by a
**newer** version, or one that's been damaged or hand-edited into
something unreadable. That part worked. But the **first change you made
afterwards** overwrote the file anyway, because every change saves.

So running an older build after a newer one could wipe out your
soundboard, your track library or your track volumes the moment you
clicked something. This affected:

- the soundboard
- the track library
- per-track volumes and fades
- the tag cache (for a newer version's cache)

**Now those files are left exactly as they are**, however many changes
you make.

### You're told when this happens

A protected file means changes to it can't be saved, and without a
warning that would look like the app forgetting what you did. Before
this release, only playlist problems ever reached the screen.

Now **all of these show in the warning banner on the Player window**,
and say plainly that your changes won't be saved until the file is fixed
(or, for a newer version's file, in this version).

The tag cache is the one deliberate exception for a *damaged* file: it's
only a cache of tags that can be re-read from your tracks, so a damaged
one is simply rebuilt.

## v0.1.0-beta.27 - Scenes

*2026-09-21*

Scenes: one press to set the whole room.

### Scenes

A new **Scenes** window - open it from the new **Scenes** button along
the bottom of the Player. A scene sets **which playlist is playing,
which looping soundboard sounds are running, and optionally the master
volume**, all in one press. "Tavern", "Road", "Combat", "Storm".

**To make one, set the room up and save it.** Play the playlist you
want, start your looping sounds, set the volume, then click
**+ Save current as scene**. The dialog opens already filled in from
what's playing - name it, untick anything you don't want, pick a colour.

A scene can **play a playlist**, **fade the music out**, or **leave the
music alone** (for ambience-only scenes like "it starts raining").
**Setting the master volume is off unless you tick it**, and when a
scene does change it, it glides rather than jumps. Grab the fader
yourself and the glide lets go.

**What pressing a scene does:**

- The music crossfades to the scene's playlist - **unless that playlist
  is already playing**, in which case it's left alone. Pressing Combat
  during combat never restarts the fight music.
- The scene's loops fade in and any other running loops fade out. **A
  loop both scenes share keeps going without a hiccup.**
- Sound effects are never touched.
- **Pressing the scene you're already in puts it back** - after the
  Killswitch, one press brings the ambience back.

Right-click a scene to **update it from what's playing now**, **edit**
it, reorder it or delete it. If something a scene uses goes away, its
button says so and the rest of the scene still works. Renaming a
soundboard button updates every scene that uses it.

### On the Stream Deck

A new **Scene** action: place it, then type the scene's name in its
settings. Capitals don't matter. Rebuild the plugin (`npm run build` in
`streamdeck-plugin`) and restart the Stream Deck app to pick it up.

### Fixes found along the way

- **Picking a new playlist during a Fade out no longer stops it.** The
  fade used to carry on and take the new music down with the old. The
  new music now takes over smoothly from wherever the fade had reached.
- **Tooltips now appear.** The app had never been set up to show them,
  so none had ever appeared - including the explanations on the Settings
  screen's ducking and update options.

## v0.1.0-beta.26 - Music Fade Out key, and the Soundboard Killswitch

*2026-09-21*

A new Stream Deck key, and a clearer name for the panic control.

### Music Fade Out on the Stream Deck

A new **Music Fade Out** action does exactly what the Player's Fade out
button does: fades the music down and stops it, leaving the soundboard
alone. It takes as long as the Player's Fade out slider says, so
changing the slider changes the key too - there's no second setting to
keep in step.

### "Stop all" is now the Killswitch

The panic control silences every soundboard sound - loops included - and
deliberately never touches the music. "Stop all" and "Stop All Sounds"
promised the opposite, so it's now **Killswitch** on the Soundboard
window and **Soundboard Killswitch** on the Stream Deck. **Esc** on the
Player still does the same thing.

**Your existing Stream Deck key keeps working.** Only the name changed,
not what the key points at. If a key you placed earlier still shows its
old title, drag a fresh one from the list or edit its title.

### Updating the Stream Deck plugin

The plugin still ships from source (see `streamdeck-plugin/README.md`).
If it's already linked, rebuild it with `npm run build` in
`streamdeck-plugin`, then quit and reopen the Stream Deck app so it
picks up the new action.

## v0.1.0-beta.25 - looping buttons and a board you can rearrange

*2026-09-20*

Two soundboard changes: buttons that loop, and buttons you can move.

### Looping buttons - ambience without a playlist

Right-click a soundboard button and choose **Loop this sound**. It now
repeats until you press it again, instead of playing once - which is
what rain, a tavern or wind under a scene actually needs. Before this,
anything continuous had to be a whole playlist, so you couldn't have
rain running *under* combat music.

A running loop is outlined and carries a small loop mark, so you can see
at a glance what's still going. Pressing it again stops it - from the
board, from a Stream Deck button, or with **Stop all** / **Esc**.

Each looping button keeps its own volume, so you can set an ambience bed
underneath the music and leave it there.

### Drag buttons to rearrange the board

**Drag a button onto another to swap them**, or onto an empty one to
move it there. This was a known limitation - moving a sound meant
assigning it somewhere new and clearing the old button.

Everything travels with the button, **including its name**, so any
Stream Deck buttons you've set up keep working after a rearrange.

## v0.1.0-beta.24 - ducking, update check, search reveals folders

*2026-09-20*

Ducking, an update check, and a smarter folder view while searching.

### Duck the music while you talk

New in Settings, off until you turn it on: **Duck the music while my mic
is live**. The music and sound effects drop while you're speaking and
come back when you stop, so you can narrate over a bed without riding
the master fader.

Two numbers, both in dB. **Drop the music by** is how far down it goes
(-12 dB is a good start - clearly under your voice, not gone), and
**Speaking is louder than** is what counts as speech; raise it if a
noisy room holds the music down when you're not talking. The timing is
fixed: quick enough not to clip your first word, and slow enough coming
back that the music doesn't surge between sentences.

It reads your mic *after* noise suppression and your plugin chain, so
whatever already cleans up your voice decides what counts as speech. A
muted mic never ducks anything, and your voice itself is never ducked.

### Search now opens the folders it found things in

In the **Folders** view, searching opens the folders holding matches
instead of leaving them collapsed, so you can see where a track actually
lives. Clearing the search puts the tree back the way you had it.

### A check for newer releases

At startup, Inkwyrd asks GitHub once whether there's a newer release and
mentions it in the banner if so. You can turn it off in Settings.

**It only ever tells you.** It never downloads or runs anything - which
matters for a project whose unsigned builds already get false-positive
antivirus flags.

## v0.1.0-beta.23 - panic button and Library search

*2026-09-20*

Two additions aimed at running a session rather than editing one.

### A panic button

**Stop all** on the Soundboard window silences every sound playing right
now - up to 16 can overlap, and until now one wrong press had no undo.
It deliberately leaves the music alone: firing the wrong effect and
ending the session are different emergencies, and Stop already covers the
second.

The same thing is on **Esc** whenever the Player window is focused, and
as a new **Stop All Sounds** action in the Stream Deck plugin.

### A search box in the Library

Type above All Tracks to narrow the list as you go. It matches on title,
artist, album, genre **and filename**, so tracks you never got round to
tagging are still findable by what they're called on disk. Words match in
any order - "drake blue" finds "Blue Drake".

The caption says how many of how many you're looking at, **Esc** or the
**x** clears it, and it narrows the **Folders** view too, dropping
folders with nothing matching inside them. The filter is never remembered
between sessions: a library that opened already filtered would look like
one that had lost most of its music.

## v0.1.0-beta.22.3 - closing Settings keeps the app running

*2026-09-19*

A one-line fix for beta.22.3.

### Fix

- **Closing Settings with the X no longer closes the program.** It puts
  Settings away and leaves everything else running. Anything you typed
  but didn't save is still discarded, as before. On first run, where
  there's nothing behind Settings yet, the X still exits.

## v0.1.0-beta.22.2 - table preview button fixed

*2026-09-17*

A one-line fix for beta.22.2.

### Fix

- **The preview play symbol works in the Table view.** Clicking it did
  nothing there (it only worked in the Folders view), because the click
  was being measured against the wrong position.

## v0.1.0-beta.22.1 - art at full strength, preview on hover

*2026-09-17*

Fixes from real use of beta.22.

### Fixes

- **Album art shows at full strength in the player.** It was being drawn
  almost transparent, which made it look like the glow was covering it.
  The glow now only appears behind the ink-bottle mark, when a track has
  no art.
- **The preview symbol appears when you hover over a track**, in both the
  Table and Folders views. In beta.22 it only showed on a selected row in
  the Table view, so in Folders view preview seemed to have gone. Click
  the play symbol to preview; it becomes a stop button in a pulsing ring,
  and clicking that stops it.
- **The Library's right-click menu is the same in both views**: Edit
  tags..., Add to playlist, Remove from library. Volume and fade is no
  longer on it. Set those by clicking a track's **Vol** column in the
  Table view.

## v0.1.0-beta.22 - preview on the row, and nine fixes

*2026-09-14*

﻿> **Superseded by [v0.1.0-beta.22.1](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.22.1).**

Fixes and small additions from real use of beta.21.

### Previewing

**Select a track in the Library and a play symbol appears at the left of
its row.** Click it to audition the track; it becomes a stop button in a
pulsing ring, so what's playing is obvious, and clicking it again stops.
Previewing has moved out of the right-click menu and out of the Playlist
window - there's one place to start a preview and the same place to stop
it.

### Fixes

- **Right-click menus open where you click**, instead of at the bottom of
  the window.
- **Album art shows in the player** when the track has any, in place of
  the ink-bottle mark. The artist line now matches the title's font and
  colour instead of looking like unrelated text.
- **Settings floats over the app** rather than making every other window
  disappear while it's open.
- **Restart in Settings actually restarts**, instead of just closing. The
  old relauncher died along with the app that started it.
- **Saving Settings no longer resurrects a playlist** named after your
  music folder, or starts playing it. That seeding now only happens on a
  genuine first run, with no playlists at all.

### Additions

- **Keyboard shortcuts** on the Player window: **Space** play/pause,
  **S** stop, **M** mic on/off, **Right arrow** skip, **Up/Down** master
  volume.
- **New playlists ask for a name straight away**, and **Enter** commits
  it. No more creating "New playlist" and hunting for Rename.
- **Monitor and Mic are remembered** between sessions, including when the
  Stream Deck is what changed them.

## v0.1.0-beta.21 - tag editor, preview, and dragging that works

*2026-09-14*

Tag your music from inside Inkwyrd, audition tracks before playing them, and drag from the Library into a playlist.

### A tag editor

**Right-click a track in the Library or Playlist window ÔåÆ Edit tags...**
Change the title, artist, album, album artist, year, genre, track and
disc numbers, BPM, comment, composer, publisher and the cover art. The
changes go into the file itself, so everything else that reads your music
sees them too.

**Select several tracks first** to edit them together. Fields that differ
show `<keep>` and stay as they are unless you type in them, so you can
fix an album's artist without flattening thirteen different titles.

**This is the first version that writes to your music files, so it does
it carefully.** Inkwyrd never edits a file in place: it copies it, tags
the copy, checks the copy still opens, and only then puts it in place of
the original. If anything goes wrong, your original is untouched. The
automated tests decode real MP3 and FLAC files before and after tagging
and compare every single audio sample ÔÇö tagging changes no audio.

A track **loaded in the player can't be tagged**, because Windows won't
let a file being played be replaced. Press Stop and save again. A track
being previewed is fine ÔÇö the preview stops itself.

### Preview

Select a track in the Library and press **Preview**. Only you hear it: it
never reaches Discord, and it plays even with Monitor off. The playlist
pauses while the preview runs and picks up again when it ends.

### Dragging into a playlist now works

Dragging a track from the Library onto the Playlist window was supposed
to work since track libraries were added. It never did ÔÇö the drag was
being handled somewhere it could never be seen. It works now, from the
table and from the folder view, where dragging a folder drags every track
in it.

## v0.1.0-beta.20.1 - the version is on the Settings screen

*2026-09-13*

A small addition to [beta.20](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.20): **Settings now shows which version you're running**, at the bottom right.

Handy when reporting something, since "0.1.0" was the same for every beta and answered nothing.

Everything else is unchanged from beta.20, including the new skins.

## v0.1.0-beta.20 - write your own skins

*2026-09-12*

You can write your own skins now.

### Skins

Open **Settings** and look under **Skin**. Three are already there ÔÇö
**Amber**, **Midnight** and **High Contrast**. Pick one and the whole
app changes straight away, with no restart.

A skin sets Inkwyrd's **colours, fonts, corner radius, title bar height**
and can replace the **logo** with an image of its own.

**To make your own,** click **Export current...**. That writes what's on
screen into your skins folder as a starting point and opens it. Edit the
`skin.json` inside, click **Reload**, and your changes appear.

Everything in a skin file is optional ÔÇö leave a colour out and Inkwyrd
uses its own, so a three-line skin is fine, and a skin written today
keeps working when a later version adds a colour. If a file can't be
read, the app keeps the look it had and says why under the picker;
"Inkwyrd (built-in)" always takes you back.

The README lists every colour name and what it paints.

## v0.1.0-beta.19 - a folder view for the Library

*2026-09-12*

The Library can now show your music the way it sits on disk.

### A folder view

The **All Tracks** list has two views now, switched with the **Table** and
**Folders** buttons next to its heading. Whichever you used last is
remembered.

**Folders** groups your tracks by the folders they actually live in, in
the same order Windows Explorer shows them, with a count on each folder.
A folder that only leads to another folder is joined into one row, so a
path like `Artist\Album` isn't several clicks deep for nothing.

**Selecting a folder selects everything in it**, which makes adding a
whole album one click: pick the folder, then **Add to playlist**.

**Table** is unchanged: Title, Artist, Album and Genre, sortable by any
column. Dragging tracks onto the Playlist window still works from the
table view only.

## v0.1.0-beta.18 - tags everywhere, and a portable download

*2026-09-11*

Tags reach the Player and Playlist windows, and there's now a portable download.

### Two ways to download

- **Installer** (`InkwyrdAudio-Setup-v2-0.1.0-beta.18.exe`): installs
  for your account, with a Start menu entry and an uninstaller.
- **Portable ZIP** (`InkwyrdAudio-Portable-0.1.0-beta.18.zip`): unzip
  anywhere and run `Inkwyrd Audio.exe`. To remove it, delete the folder.

Both are the same program. Settings and playlists live in
`%APPDATA%\Inkwyrd Audio` either way, so you can switch between them.

**Microsoft Defender may block either download. Please don't override
it.** Defender blocked beta.17's installer on at least one machine. We
checked it as a possible real infection rather than assuming it wasn't:

- **This installer:** 3 of 71 antivirus products on VirusTotal flag it,
  all with generic automatic labels. None names an actual malware
  family.
- **The program inside it:** 1 of 64 flags it: Microsoft's automatic
  machine-learning check (`Wacatac.B!ml`), and nothing else. An earlier
  build of the same code scanned completely clean, so that check's
  verdict flips between builds of identical source.
- **The machine it was built on:** passed a full offline scan.

It has been reported to Microsoft as a false positive. The ZIP avoids
the checks that flag installers specifically, but not Microsoft's, which
currently flags the program itself too. Code signing is the long-term
fix.

### The Player shows the artist

The now-playing display reads the same tags as the Library, so artist
and title match everywhere. Untagged files still fall back to their
filename.

### The Playlist window has Title and Artist columns

The columns don't sort. The list stays in the playlist's own order,
which is the order it plays in with Shuffle off.

### Also

- **Tags are read for tracks added mid-session**, and for tracks that
  are only in a playlist. Before, both kept showing filenames until the
  next launch.
- **Fixed a stray horizontal scrollbar** under the track tables once a
  list got long enough to scroll.
- **Table headers match the green theme** instead of plain grey.
- **Both downloads now say who made them** (right-click, Properties,
  Details) and have an icon.

## v0.1.0-beta.17 - metadata and a sortable track table

*2026-09-10*

Inkwyrd now reads your files' embedded tags.

### Real titles, not filenames

The Library and Playlist windows show the actual **Title** tag. Files with no
tags still fall back to the filename.

### All Tracks is now a sortable table

Columns for **Title, Artist, Album and Genre** — click any header to sort by it,
click again to reverse.

Sorting by Album keeps each record in **track-number order** rather than
scrambling it alphabetically. Your selection follows the sort, so picking tracks
and then re-ordering doesn't leave you with a different set selected.

The volume bar has its own column, so it's much easier to hit than when it
floated at the right-hand end of the row. It deliberately doesn't sort — it's a
control, not a value.

### Also

- **"All tracks" is now "All Tracks."**
- **Open folder** has moved out of the playlist buttons into **Settings**, as
  "Open playlists folder" — it's a once-in-a-while thing and didn't earn a
  permanent spot next to the buttons you use every session.

### Notes

**The first launch after upgrading will scan your library.** It runs in the
background once the windows are up, so nothing blocks — you'll see filenames
briefly and rows filling in as it goes. Tag cells that haven't been read yet are
dimmed rather than blank. Results are cached, so it only happens once, and after
that only for files you add or change.

**Untagged files stay on their filename**, which is correct rather than a
failure. If a track shows no artist or album, Windows can't see tags on it
either.

### Coming next

A **folder-tree view** of the library, as an alternative to this table.

## v0.1.0-beta.16 - the now-playing display

*2026-09-10*

﻿> **Superseded by [v0.1.0-beta.17](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.17).**

The first fully code-drawn component: the now-playing display.  ## What's new  The top of the Player window is no longer stock controls wearing a green palette — it's drawn from scratch:  - **Art slot** with the ink-bottle mark and a glow that pulses with the music. - **ARTIST / TITLE / TIME** readout, with the time in a lit monospace display. - **Live spectrum**, 48 logarithmically-spaced bands fed from the actual master   mix — so it shows what Discord is being sent, not just what's playing locally. - **A seek bar you can drag.** Click or scrub anywhere on it to move through the   track. That's genuinely new — there was no way to seek before.  Still no image files anywhere. The logo is drawn in code and is an approximation; send me the real SVG and it becomes the real thing.  ## Notes  **Artist comes from the filename.** Tracks named `Artist - Title` split correctly; anything else shows just the title with `--` for artist. Nothing reads embedded tags yet, and guessing would be worse than leaving it blank.  **The Player window will grow on first launch.** Your saved height predates this display, and honouring it would bury the transport off the bottom. Your position and width are kept.  **The other four windows are unchanged** — still the beta.15 skin over stock widgets. They get the same treatment next.

## v0.1.0-beta.15 - the dark green skin

*2026-09-10*

﻿> **Superseded by [v0.1.0-beta.16](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.16).**

The black-and-dark-green skin, built from the design mockup.  ## What's in it  - **Custom title bars** carrying the ink-bottle mark, INKWYRD, and each window's   own subtitle — AUDIO PLAYER, AUDIO LIBRARY, PLAYLISTS, VOICE FX, SOUNDBOARD. - **The whole palette** across all five windows: near-black grounds, dark green   panels, light green text, a brighter green for anything live. - **Every control restyled** — buttons, sliders, toggles, text fields, scrollbars,   lists, menus. - **Now-playing rows** get a bright bar down their left edge as well as brighter   text, so a row that is both playing and selected still reads correctly. - **Volume bars** are green, except boost, which stays amber — it's the one state   on those bars that can clip, and making it match everything else would hide it.  ## What's deliberately not in it yet  This drop is the skin over the existing layout. The mockup also shows things the app has no concept of yet, and those are the next drop rather than half-started here:  - album art - the spectrum visualiser - the seek / progress bar - the Library window's folder tree  ## Two things worth knowing  **The logo is drawn in code and is an approximation.** Redrawing artwork from a screenshot gets the gesture, not the detail. If you have the original SVG or PNG, send it over and it becomes the real thing.  **The palette was read by eye, not sampled** — the mockup came through as an image, not a file. Everything lives in one table, so if a colour looks wrong against your original it's a one-line change. Tell me which and I'll adjust.  ## Also  The windows now draw their own title bars, which a native Windows caption cannot do. That does **not** change dragging: Windows still runs the move loop, so the magnetic snapping and group-dragging work exactly as before — verified by real drag tests, both snapping flush and carrying the docked group. The one thing you lose by not having a native caption is Windows Snap Layouts (hover-maximise) and Win+arrow snapping on these five windows.

## v0.1.0-beta.14.1 - windows no longer get lost behind other apps

*2026-09-10*

﻿> **Superseded by [v0.1.0-beta.15](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.15).**

Fixes the windows-disappearing bug reported against beta.14.  ## What was wrong  Two symptoms, one cause:  - Windows vanishing while the app was dragged around, especially over Discord. - Not all of them coming back after minimising and restoring.  The satellite windows (Playlist, Library, Voice FX, Soundboard) were independent top-level windows with no z-order relationship to the Player window. Clicking the Player's title bar to drag it raised **only** the Player — so dragging it over another app left the satellites at their old depth, behind that app. They hadn't gone anywhere; they were covered. Restoring from minimise had the same shape: showing a window doesn't raise it, so any satellite that had been below another app stayed below it.  That's why it was intermittent — it depended entirely on where the other app happened to sit in the window stack.  ## The fix  The satellites are now Win32 **owned** windows of the Player window. An owned window always sits above its owner, the whole group rises together when any of them is clicked, and Windows hides and restores them with the owner.  In practice: the layout now behaves as one thing. Click any part of it and all of it comes forward, in front of Discord or anything else.  Verified by reproducing the fault deterministically first — raising Discord and then clicking the Player put Discord between the Player and its satellites every single time — and then confirming both the drag and the minimise/restore case come back clean.  ## Also  `INKWYRD_NO_DISCORD=1` now runs the app without connecting the bot. That's a testing flag, not something you need; it exists so the UI can be checked without knocking a live session out of its voice channel.  Nothing else changed. Noise suppression and Discord auto-mute are exactly as they shipped in beta.14 — see those notes for setup.

## v0.1.0-beta.14 - noise suppression + auto-mute in Discord

*2026-09-10*

﻿> **Superseded by [v0.1.0-beta.14.1](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.14.1).**

Two new things on the microphone path, both opt-in and both off by default.  ## Noise suppression (RNNoise)  A toggle at the top of the **Voice FX** window. Removes steady background noise — fan, hiss, room tone — between your words. It runs before your VST chain, so the plugins shape your voice rather than your room.  **Read the hint under the toggle before switching it on.** Measured on real speech:  | your mic | background in the pauses | during speech | |---|---|---| | noisy (5 dB SNR) | −48 dB | **+5.1 dB better** | | average (10 dB SNR) | −49 dB | +0.5 dB | | already quiet (20 dB SNR) | −39 dB | **−9.2 dB worse** |  So it genuinely helps a noisy mic and genuinely hurts a clean one. That is why it defaults to off rather than on. It also adds about 40 ms of delay to your voice, and costs about 2.5% of one CPU core.  ## Mute me in Discord while my mic is live  New optional section in **Settings**. When your mic goes live in Inkwyrd, it mutes you in Discord, so your voice doesn't arrive twice — once from your own client and again through the bot. When you mute the mic here, your previous Discord setting is put back exactly as it was.  Setting it up is a one-time job, and needs two things from the Discord Developer Portal for **your own** application:  1. **A redirect URI.** OAuth2 → Redirects → add `https://inkwyrd.com/rpc` →    Save. Nothing is ever sent to that address; OAuth just requires one to    exist. **Restart Discord afterwards** — the desktop client caches your    application's settings at startup and won't see a new redirect until it    does. 2. **Your client secret.** OAuth2 → Client Secret. Paste it into Settings,    tick **Enable**, click **Authorise...**, then click **Authorize** in the    dialog Discord puts on screen.  You don't need to enter a client ID — it's read from the bot token you've already configured.  This mutes your own client locally. It needs no server permissions and works in any server, including ones you don't run.  ## Notes  - Both features are off unless you turn them on. If you only use Inkwyrd as   a music bot, nothing here changes for you. - The download is about twice the size of beta.13 (10 MB vs 5 MB). That's   RNNoise's trained model compiled into the app — there's no runtime   download and no extra files to install.

## v0.1.0-beta.13 - stronger magnets, resize snapping, reopen buttons

*2026-09-10*

﻿> **Superseded by [v0.1.0-beta.14](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.14).**

Stronger magnetism, snapping while you resize, and a button to bring back every window.  ## Windows  - **The magnet is much stronger, and it now pulls while you drag.** Previously windows settled into place a moment after you let go; now they visibly snap as you move them, from about twice the distance. - **Resizing snaps too.** Stretch a window's edge towards a neighbour and it lands exactly flush — the opposite edge stays where it is. - **You can always pull a window back off.** Fixed a real problem in the process: a window that had snapped to something couldn't be dragged away from it at all. - **Four activator buttons** on the Now Playing window — Playlist, Library, Voice FX, Soundboard. Closing Playlist or Library with its X used to strand it with no way back. - Each window now remembers whether it was open across a trip through Settings, instead of Playlist and Library being forced open.  ## Reminder of how it's meant to work  - Drag a **satellite** window to detach it from a group. - Drag the **Now Playing** window to move everything docked to it at once. - Minimising **Now Playing** takes every open window down with it, and brings them all back.  ---  Windows will likely show a **"Windows protected your PC"** SmartScreen warning — this is an unsigned beta, not a sign anything is wrong. Click **More info → Run anyway**. Installs for your user only, no admin needed.

## v0.1.0-beta.12 - detachable magnetic windows + track library

*2026-09-10*

Magnetic windows you can actually pull apart, a master track library, and the outstanding fixes from beta.10 testing.

### Windows

- **Satellites detach again.** In beta.11 every window towed its neighbours, so a docked satellite could never be pulled off the group. Now only the **Now Playing** window carries docked windows with it — drag a satellite to detach it, drag the main window to move everything at once.
- Satellites have **no minimise button**, only an X (which hides them). The only minimise button is on Now Playing, and minimising it takes every open window down with it — including from the taskbar or Win+D — bringing them all back together on restore.

### Library and playlists

The split now matches what the windows are called:

- **Library window** — your playlists on top, and underneath a **master list of every track** the app knows about. That list stays put when you click between playlists.
- **Playlist window** — the contents of whichever playlist you've *selected*. Selecting is still just browsing; Play (or double-click) is what starts audio.
- **Add files… / Add folder…** now add to your **library**. Get tracks into a playlist by dragging them from the master list onto the Playlist window, or with **Add to playlist**.
- **Deleting individual tracks finally exists** — *Remove* takes them out of the library, *Remove from playlist* (or the Delete key) takes one out of a playlist. Removing from the library never touches a playlist that already uses the track.

Your existing playlists are seeded into the library automatically on first launch; nothing is lost, and folder-linked playlists keep working exactly as before.

### Also fixed

- The Voice FX hint promised an "Edit" button that never existed — clicking a plugin's **name** is what opens it.

### Fixed in beta.11.1

- A crash where dragging a window against another sent both off-screen and took the app down with them.

---

Windows will likely show a **"Windows protected your PC"** SmartScreen warning — this is an unsigned beta, not a sign anything is wrong. Click **More info → Run anyway**. Installs for your user only, no admin needed.

## v0.1.0-beta.9

*2026-09-04*

# Inkwyrd Audio v0.1.0-beta.9

### Loop a single track

**Loop track** is on the settings row, next to Crossfade. Turn it on and
whatever is playing repeats instead of moving on — for a single ambient
bed you want running all session.

The slider beside it sets the silence between repeats, from **No gap** up
to 10 seconds:

- **No gap** sends the track straight back round. With **Crossfade** on
  it dissolves into itself, so the loop is seamless rather than obviously
  restarting.
- **Any gap** lets the track play right out, holds silence for that long,
  and then starts it again.

Skip still moves to the next track — looping only decides what happens
when a track reaches its own end. Pressing Play during a gap starts the
track again straight away rather than waiting the rest of it out.

### Renaming a playlist now updates immediately

Renaming a playlist left the old name on the list until you clicked
something else. Fixed.

### Also fixed: playback could stop with Crossfade off

If you had **Crossfade** switched off in beta.8, playback could stop
after a track finished instead of moving on to the next one. It depended
on timing, so it wouldn't have happened every time.

The cause is worth explaining, because it's the same thing that made
looping worth building carefully: the audio player stops itself the
instant a track ends, and the code that decides "time to start the next
one" read a stopped track as "nothing to do here" rather than "this one
has finished". With a crossfade running it never came up, because the
handover starts seconds before the end. With crossfading off the handover
is only fractions of a second before the end, which is a small enough
window to miss.

### Install

Run the installer below. It installs for your own Windows account only,
no admin rights needed. Windows SmartScreen will warn about it (this
build isn't code-signed) — **More info → Run anyway**.

Install straight over your existing version. Playlists, settings and your
soundboard are all kept.

## v0.1.0-beta.8.1

*2026-09-04*

﻿> **Superseded by [v0.1.0-beta.9](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.9)** - please use that instead.

# Inkwyrd Audio v0.1.0-beta.8.1  **Fixes playback going silent in beta.8.** If you're on beta.8, update.  ## What was wrong  In beta.8, the moment one track finished fading into the next, the new track was set to zero volume. From the outside that looked like: start a track, it crossfades straight into the next one, and then everything goes quiet a few seconds later.  It was a mistake in beta.8's rework of how the two decks' volumes are worked out — the code that sets them was being run one line too early, while the crossfade still counted as "in progress", so it handed the incoming track the volume the *outgoing* one should have had at the end of a fade: silence.  ## What else changed  Nothing. This is the fix and its test, no new features.  The test is worth mentioning, because the reason this got out is that every existing check drove the audio engine by calling its methods and looking at the result — and none of them ever let a crossfade actually *run to completion*, which is the exact moment the bug happened. There's now a check that plays real audio, lets a crossfade finish on its own, and confirms sound is still coming out the other side. It was confirmed to fail on the broken version and pass on this one.  ## Install  Run the installer below. It installs for your own Windows account only, no admin rights needed. Windows SmartScreen will warn about it (this build isn't code-signed) — **More info → Run anyway**.  Install straight over your existing version. Playlists, settings and your soundboard are all kept.

## v0.1.0-beta.8

*2026-09-04*

﻿> **Superseded by [v0.1.0-beta.8.1](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.8.1)** - this version has a bug that silences playback when a crossfade finishes. Please use 8.1 instead.

# Inkwyrd Audio v0.1.0-beta.8  Transport controls, and control over how tracks hand over to each other.  ## Stop, and Fade out  The transport row now has three separate things rather than one:  - **Pause** — stops, keeps your place. Play carries on from where it was. - **Stop** — silences everything and forgets where it was. Play starts   the list again from the top. - **Fade out** — rides the music down to silence and then stops, for   ending a scene. Pressing Play or Pause during the fade cancels it and   comes straight back up to level.  Fade out takes the *music* down, not your microphone — fading yourself out mid-sentence isn't what a button next to Stop should do. The Master fader is there if you want to take absolutely everything down.  ## Crossfade is now optional, and adjustable  A second row of controls sits under the transport: **Crossfade** on or off, and how long it takes (0.5–15 seconds). With it off, a track runs to its end and the next one starts immediately.  There's also a setting for how long **Fade out** takes. Everything on that row is remembered between sessions.  ## Every track can have its own fade length  Click a track's volume bar and you'll find **Fade into next** underneath the volume slider. It sets how long *that* track takes to hand over to whatever follows it, overriding the global Crossfade length.  It's for the track that ends on a long tail and wants a slow hand-off, or the one that stops dead and wants a quick one. Leave it on **Default** and it follows the global setting. Tracks that have their own fade say so on their row, so you can see at a glance which ones you've changed.  Like the volume trim, the fade belongs to the *track*, so it works the same way in every playlist that contains it — and it keeps working under shuffle, because it describes the track rather than a particular pairing of tracks.  Your existing per-track volumes carry over automatically.  ## Install  Run the installer below. It installs for your own Windows account only, no admin rights needed. Windows SmartScreen will warn about it (this build isn't code-signed) — **More info → Run anyway**.  Install straight over your existing version. Playlists, settings and your soundboard are all kept.

## v0.1.0-beta.7

*2026-09-04*

﻿> **Superseded by [v0.1.0-beta.8](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.8)** - please use that instead.

# Inkwyrd Audio v0.1.0-beta.7  Volume control, at every level — plus pictures on your soundboard buttons.  ## A master fader  Top right, next to **Voice FX**. One control over everything the app sends out: your own speakers *and* Discord. Its position is remembered between sessions, so turning it down mid-session doesn't come back up to full next time you launch.  ## Every track has its own volume  Each track in a playlist now has a small bar at the right of its row. **Click the bar** to open a slider.  This is for the track that was exported hotter than everything else and makes everyone jump when shuffle lands on it. Pull it down once and it stays down.  The setting belongs to the **file**, not to the playlist — so a track that appears in several playlists is fixed in all of them at once, and it works for playlists built from a linked folder too. If you adjust the track that's currently playing, you hear it immediately.  ## Every soundboard button has its own volume  Each button has a thin bar along its bottom edge. **Click the bar** to open a slider; click anywhere else on the button to fire the sound as usual.  ## Reading the bars  The bars work the same way in both places:  - The small **notch** is normal volume — the level the file was recorded   at. - A **dim** bar ending at the notch means untouched. - A bar **short of** the notch has been turned down. - An **amber** bar past the notch has been turned up.  ## Pictures on soundboard buttons  Right-click a button and choose **Set a picture**, or just drag an image file straight onto a button that already has a sound. The picture is dimmed behind the button's name so the label stays readable.  PNG, JPEG, GIF, BMP and WebP.  ## A note on downgrading  Your soundboard file now records the per-button volumes and pictures, so an older version of Inkwyrd Audio won't read it. It won't damage it either — an older build leaves the file alone and starts with an empty board rather than rewriting it and throwing your settings away. Updating again brings everything back.  ## Install  Run the installer below. It installs for your own Windows account only, no admin rights needed. Windows SmartScreen will warn about it (this build isn't code-signed) — **More info → Run anyway**.  Install straight over your existing version. Playlists, settings and your soundboard are all kept.

## v0.1.0-beta.6.1

*2026-09-04*

﻿> **Superseded by [v0.1.0-beta.7](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.7)** - please use that instead.

# Inkwyrd Audio v0.1.0-beta.6.1  A startup fix. **The app no longer freezes for ~18 seconds when you launch it.**  ## What was wrong  Inkwyrd Audio scanned your VST3 plugins on every single launch, and it did it on the same thread that draws the interface — so the whole window was unresponsive while it ran. On a machine with 40 plugins that measured at just over 18 seconds, every time.  ## What changed  **Your plugin list is now remembered between launches.** Restoring it takes about 7 milliseconds instead of 18 seconds, so a normal launch does no scanning at all.  **When a scan does happen it runs in the background.** That's only on your very first launch after updating, or when you ask for one. The **Voice FX** button reads "Scanning..." and is unavailable while it runs; everything else — playlists, soundboard, Discord — keeps working normally.  **New: a Rescan button** in the Voice FX panel, for when you install new plugins and want them picked up.  Your plugin list is now also sorted alphabetically rather than in whatever order the folder happened to be read in.  ## What's left  Startup is now about 4.5 seconds rather than 18. The remainder is opening your audio device (~3 s) and Windows creating the app's first window (~1.5 s) — both unavoidable platform costs rather than work being repeated needlessly. The app now records these timings in its log, so if a launch is ever unusually slow the log will say which step it was.  ## Install  Run the installer below. It installs for your own Windows account only, no admin rights needed. Windows SmartScreen will warn about it (this build isn't code-signed) — **More info → Run anyway**.  Install straight over your existing version. Playlists, settings and your soundboard are all kept. Your first launch after updating will do one background scan to build the plugin cache; every launch after that skips it.

## v0.1.0-beta.6

*2026-09-03*

﻿> **Superseded by [v0.1.0-beta.6.1](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.6.1)** - please use that instead.

# Inkwyrd Audio v0.1.0-beta.6  Fixes from the first real session with beta.5, plus the Stop button that should have been there all along.  ## Monitor no longer turns itself on  **Monitor is now off whenever the app starts, without exception.**  It was previously switched *on* automatically when no Discord bot was configured — so launching with a missing or cleared bot token started playing music out of your speakers immediately. That was meant to stop local-only mode being silently silent; it just produced a different surprise.  Now it stays off, and the app *tells you* when that means you won't hear anything: an orange line appears offering to turn it on. With a Discord bot set up, Monitor off is the correct normal state — you hear the mix through the call — so nothing nags you about it.  ## A Stop button  There was no way to stop playback at all — only Skip and Shuffle — so a playlist that started on launch could only be silenced by quitting.  **Play/Stop** now sits at the left of the transport row. Stop keeps your place; Play carries on with the same track. Stopping during a crossfade settles it cleanly rather than leaving two tracks stuck part-way.  ## Deleting the playing playlist now stops it  Deleting the playlist you were listening to left the audio running with nothing on screen owning it. It now stops.  ## Discord settings usually apply without a restart  If no Discord connection has been made yet this session — which is the case whenever you launch with an empty or cleared bot token — pasting in your credentials and saving now connects **straight away**. The app used to insist on a restart it didn't actually need.  When a restart genuinely is required (you change credentials after already connecting), the app now offers a **Restart now** button instead of just telling you to do it yourself. The Settings button reads **Save & Apply** rather than "Save & Launch" when you're not setting up for the first time.  ## Dialogs can no longer open on the wrong screen  Message boxes were being centred on your *primary* monitor rather than on the app's own window. On a multi-monitor setup that could put a confirmation dialog on a different screen — and because it's modal, every click on the main window would be ignored until it was answered. The app looks frozen; everything else works fine.  Every dialog is now anchored to the app window. If you saw the app become unresponsive, this is a plausible cause, though not a confirmed one.  ## Known: the app freezes for ~15–20 seconds at startup  New internal instrumentation caught this: scanning your VST3 plugins blocks the whole interface while it runs. The app hasn't hung — it's working — but it can't be clicked until the scan finishes. A fix is in progress.  ## Install  Run the installer below. It installs for your own Windows account only, no admin rights needed. Windows SmartScreen will warn about it (this build isn't code-signed) — **More info → Run anyway**.  Install straight over your existing version; no need to uninstall first. Playlists, settings and your soundboard are all kept.

## v0.1.0-beta.5

*2026-09-03*

﻿> **Superseded by [v0.1.0-beta.6](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.6)** - please use that instead.

# Inkwyrd Audio v0.1.0-beta.5  Two features in this one: **drag and drop from Explorer**, and a **programmable soundboard**.  ## Drag and drop into playlists  Drag audio files or folders straight from Windows Explorer onto the left-hand panel.  - Drop onto a **playlist row** to add to that playlist, even if it isn't   the one you have selected. - Drop **anywhere else** on the panel to add to the selected playlist. - Dropped folders ask the same "keep the folder linked / add these tracks   once" question the **Add folder...** button does — asked once for a   whole batch, not once per folder. - Files the app can't play are filtered out, and a drop with nothing   playable in it tells you so instead of quietly doing nothing.  **Adding tracks to the playlist you're listening to no longer disturbs it.** Previously this would have sent playback back to the top of the list (or re-randomised everything still to come, with shuffle on). The track playing carries on, and new tracks join the running order.  ## Programmable soundboard  The SFX board is now a grid of assignable buttons rather than a straight listing of whatever was in your sound-effects folder.  - **Click an empty button** to pick a sound for it, or **drag sound files   onto a specific button** from Explorer. - **Right-click** any button to rename it, give it a colour, swap its   sound, or clear it. - Buttons **stay where you put them** — adding a new sound no longer   shifts everything along by one. - **+** and **−** change how many buttons there are. A button with a   sound on it is never removed. - **Import folder...** drops everything in a folder onto the free   buttons, and now searches **subfolders** too. - A sound whose file has moved or gone shows as **(file missing)** rather   than vanishing, so you can see what happened and fix it.  Your existing sound-effects folder is imported onto the board automatically the first time you run this, keeping its order and its names.  ### If you use the Stream Deck plugin  **Your existing buttons keep working, unchanged.** A button's name is what a Stream Deck sends to trigger it, and the migration keeps every sound's old filename-based name. The one thing to know: if you *rename* a button in Inkwyrd Audio, update the matching Stream Deck button to the new name.  ## Also fixed  - Adding tracks through the **Add files...** / **Add folder...** buttons   never actually reached the audio engine — they only updated the list on   screen. **Refresh** now reaches it too, so re-scanning a linked folder   picks up new files mid-session instead of carrying on with the old ones. - Opening **Settings** while a file picker or dialog was open could crash   the app. - Changing your sound-effects folder no longer rebuilds the board and   throws away an arrangement you set up by hand — it only adds what's new.  ## Install  Run the installer below. It installs for your own Windows account only, no admin rights needed. Windows SmartScreen will warn about it (this build isn't code-signed) — **More info → Run anyway**.  You can install straight over an existing version; there's no need to uninstall first. Your playlists, settings and soundboard are kept.

## v0.1.0-beta.3

*2026-09-03*

﻿> **Superseded by [v0.1.0-beta.5](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.5)** - please use that instead.

First feature release since the audio fixes - hence **beta.3** rather than another dot.  ### Multiple playlists Keep as many playlists as you like - one per scene, mood or session. Click one to browse its tracks; **double-click (or Play) to switch to it**, and the music **crossfades across** rather than cutting. Shuffle is remembered per playlist and only ever picks from that list.  Build a playlist from: - **a linked folder** - stays up to date, so files you add to it later appear automatically - **a one-time folder import** - takes what's there now, so you can remove individual tracks - **individual files**  Playlists are saved as readable JSON, one file each, in `%APPDATA%\Inkwyrd Audio\Playlists\` - easy to back up or share. **Open folder** takes you there.  ### New layout Playlists and their tracks on the left, the soundboard as a button grid on the right, transport and status across the top. VST3 plugins and your live mic chain moved behind the **Voice FX...** button - unchanged, just no longer taking up a third of the screen.  ### Your existing setup carries over Your current music folder is converted automatically into a playlist on first launch. Same tracks, same shuffle, nothing to redo.  ### Also fixed - Hammering **Skip** during a crossfade no longer swallows presses (worth judging by ear - it cuts the fade short). - Changing your soundboard folder used to leave the old sounds still triggerable from a Stream Deck while invisible in the app. - A typo'd sound name on a Stream Deck button could abort the app in debug builds.  Your existing **Stream Deck plugin keeps working unchanged** - sound names are still the trigger key.  ### Before you install - **Close Inkwyrd Audio first if it's running.** - SmartScreen: **More info -> Run anyway**.

## v0.1.0-beta.2.7

*2026-09-02*

---

Hotfix on top of [v0.1.0-beta.2.6](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.2.6).

#### Local monitoring now starts switched off
Previously it only switched off once the Discord connection completed, which left a window - potentially a long one, if the bot was waiting for someone to join the channel - where the host heard every track **twice**: once from the app directly, once through the bot.

It now starts off whenever Discord is configured, so if you're already in the call when you open the app there's no doubling at all. The **Monitor** button still turns it back on whenever you want it.

If you have **no** Discord credentials set, monitoring stays on - local playback is the only way to hear anything in that mode.

#### Before you install
- **Close Inkwyrd Audio first if it's running.**
- SmartScreen: **More info -> Run anyway**.

## v0.1.0-beta.2.6

*2026-09-02*

---

Hotfix on top of [v0.1.0-beta.2.5](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.2.5).

#### Fixed: bot sits in the channel but never actually plays anything
If the bot joined an **empty** voice channel and you joined afterwards, the voice-encryption handshake failed and no audio was ever transmitted - the bot stayed in the channel with no green ring around it.

The cause was in the encryption handshake: the group membership check didn't include the bot's *own* user ID, so Discord's Welcome message was rejected outright. This only ever affected that one path - joining a channel that **already** had someone in it uses a different route and worked fine, which is exactly why it survived earlier testing. Unfortunately that broken path is the normal way you'd start a session: open the app first, players join after.

#### Also in this build
- Opus bitrate is now 64kbps (was 128k), matching what Discord's own client typically uses. Host-selectable bitrate is planned.

#### Before you install
- **Close Inkwyrd Audio first if it's running.**
- SmartScreen: **More info -> Run anyway**.

## v0.1.0-beta.2.5

*2026-09-02*

---

> **Download updated 2 Sep 2026** - the installer attached to this release was rebuilt to drop the Opus bitrate from 128kbps to 64kbps (matching what Discord's own client typically uses; 128k is a lot of sustained upstream for a home connection). If you downloaded this release before that date, grab it again. Host-selectable bitrate is planned.

---

Small hotfix on top of [v0.1.0-beta.2.4](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.2.4).

#### Clearer connection status
The old message - *"waiting for someone to join before audio can start"* - was misleading: music playback **has** already started at that point, it just isn't being sent to Discord yet, which contradicted the Now Playing line right above it. It now distinguishes playing from sending:

- **"In the voice channel - nobody else here yet, so nothing is being sent to Discord."**
- **"Connected - streaming to Discord."** once audio is actually flowing.

#### Before you install
- **Close Inkwyrd Audio first if it's running.**
- SmartScreen: **More info -> Run anyway**.

## v0.1.0-beta.2.4

*2026-09-02*

---

Hotfix on top of [v0.1.0-beta.2.3](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.2.3).

#### The host no longer hears the app's own audio
You're in the Discord call too, so you already hear the music and soundboard through the bot. Playing it out of your speakers as well meant hearing everything **twice**, slightly offset - which sounds exactly like an annoying delay. Local playback is now switched off automatically when Discord connects, so you hear what your players hear. A **Monitor** button lets you turn it back on (handy for setting up without Discord).

#### The bot no longer leaves when it joins an empty channel
Discord's voice encryption (DAVE) is a *group* key exchange, and Discord only forms the group once someone else is in the channel. A bot joining an empty channel therefore never completed the handshake - and the app responded by hanging up, which is the worst possible thing when you've started the app before your players arrive. It now stays in the channel and waits, and starts audio the moment someone joins.

#### Also
- Music/soundboard streaming threads run at a higher priority, which should help with dropouts.

#### Before you install
- **Close Inkwyrd Audio first if it's running.**
- No admin rights needed. SmartScreen: **More info -> Run anyway**.

## v0.1.0-beta.2.3

*2026-09-02*

---

Hotfix on top of [v0.1.0-beta.2.2](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.2.2). Two confirmed bugs from beta testing.

#### Fixed: severely delayed mic audio in Discord
The audio sender consumed exactly 20ms of audio per pass but then slept a further 20ms *after* doing the Opus encode, encryption and network send - so it drained slower than audio arrived, and the queue grew until it hit its ~2 second cap and stayed there. (Windows makes it worse: its default timer granularity means a "20ms" sleep is often ~31ms.) Frames are now paced against a self-correcting clock deadline, with a guard that resyncs if delay ever builds up.

#### Fixed: no music, with no explanation
The music folder scan was **not recursive**, so pointing it at an album or library folder whose tracks live in subfolders loaded zero files - and nothing in the UI said so. It now scans subfolders, and if a folder yields no playable audio you get an explicit warning instead of silence.

#### Before you install
- **Close Inkwyrd Audio first if it's running** - only one instance runs at a time.
- No admin rights needed. SmartScreen will warn (not code-signed yet): **More info -> Run anyway**.
- Settings carry over; no need to uninstall first.

## v0.1.0-beta.2.2

*2026-09-02*

---

Hotfix on top of [v0.1.0-beta.2.1](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.2.1) - a real connection bug found from beta log data.

#### Fixed: bot joins the voice channel then immediately leaves
The voice gateway was closing with `4003 "Not authenticated"` right after we sent our Identify. Cause: the heartbeat thread was started **before** the Identify was sent, and it fires its first heartbeat immediately - so it raced the Identify onto the socket. Discord's voice gateway drops any connection that sends a payload before Identify.

Because it's a thread race it was intermittent, which is why the failure looked different run to run. The GUI conversion made it much likelier to lose the race, since the connection sequence now runs on a background thread while the UI thread is busy.

#### Also in this build (from beta.2.1)
- Connection diagnostics are written to `%APPDATA%\Inkwyrd Audio\log.txt` - please include this if you report a problem.
- An orange banner now appears if your audio device fails to open, instead of failing silently.

#### Still being investigated
A separate DAVE (voice encryption) handshake stall seen in one earlier log - our MLS key package is sent and Discord doesn't reply. Not addressed here; if you hit it, the log will show `Timed out waiting for the DAVE handshake`.

#### Before you install
- **Close Inkwyrd Audio first if it's running** - only one instance runs at a time, so a second launch does nothing.
- No admin rights needed. Windows SmartScreen will warn (not code-signed yet): **More info -> Run anyway**.
- Your saved settings carry over; no need to uninstall first.

## v0.1.0-beta.2.1

*2026-08-31*

---

Hotfix on top of [v0.1.0-beta.2](https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.2) - no new features, so per our versioning convention this is .2.1 not .3.

#### What changed
- Real diagnostic logging: connection issues (Discord gateway, voice gateway, DAVE handshake) now write to %APPDATA%\Inkwyrd Audio\log.txt, since this being a GUI app means there's no console window to see them in otherwise. If you hit a connection problem, that file is the most useful thing to share.
- If your audio device fails to open, the app now shows an orange warning banner on the main screen instead of failing completely silently (this would explain "connected but no sound at all" in some cases).

#### Still investigating
Two real reports from beta.2 testing: a DAVE handshake timeout (bot joined then left immediately), and on a retry, the bot staying joined but silent. The connection code itself was checked line-by-line against the proven console-app version and matches exactly, so the next step is real log data from a repro - the logging added here is aimed directly at that.

#### Before you install
- No admin rights needed - installs just for your Windows account.
- Windows will likely show a SmartScreen warning since this beta isn't code-signed yet - click **More info -> Run anyway**.

## v0.1.0-beta.2

*2026-08-30*

---

Second public beta of Inkwyrd Audio - **now a real GUI app**, not a console app. This replaces beta.1 entirely.

**What's new**: no more environment variables. First launch opens a Setup screen - click Browse to pick a music folder, optionally a soundboard folder, and optionally paste in your Discord bot details (see the [README](https://github.com/Troyificus/InkWyrd-Audio#readme) for how to get those). Everything is saved, so this is a one-time step. The main screen has Skip/Shuffle/Mute buttons, soundboard buttons, and Add/Remove for VST3 plugins in your live mic chain.

#### Before you install
- No admin rights needed - installs just for your Windows account.
- Windows will likely show a SmartScreen "Windows protected your PC" warning, since this beta isn't code-signed yet. Click **More info -> Run anyway**.
- This is still a **beta**. Please open an issue if you hit a bug - a screenshot of the status line helps a lot.

#### Known limitations
- Changing your Discord bot token/server/channel via Settings takes effect on next launch, not immediately.
- No seek bar or track list yet, just a "Now playing" name.
- Stream Deck plugin is build-from-source only for now.

## v0.1.0-beta.1

*2026-08-30*

---

First public beta of Inkwyrd Audio - a standalone Windows app for running D&D sessions over Discord.

**Read the [README](https://github.com/Troyificus/InkWyrd-Audio#readme) first** - it covers setting up your own free Discord bot (about 5 minutes) and getting the app running.

#### What's in this build
- Music playlists with shuffle/crossfade
- On-demand soundboard
- Live mic processing through your own VST3 plugins
- Streamed to Discord via the app's own bot connection (WAV, AIFF, FLAC, Ogg Vorbis, MP3, AAC/M4A, WMA supported)
- Optional Elgato Stream Deck integration (build from source for now)

#### Before you install
- No admin rights needed - installs just for your Windows account.
- Windows will likely show a SmartScreen "Windows protected your PC" warning, since this beta isn't code-signed yet. Click **More info -> Run anyway**.
- This is a **beta** - console-only interface for now, no GUI. Please open an issue if you hit a bug, ideally with the console output.
