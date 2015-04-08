#C Music Player Tutorial

##Contents

1. [Starting Cmus](#step-1-starting-cmus)
2. [Adding Music](#step-2-adding-music)
3. [Playing Tracks From The Library](#step-3-playing-tracks-from-the-library)
4. [Managing The Queue](#step-4-managing-the-queue)
5. [The Playlist](#step-5-the-playlist)
6. [Find that track](#step-6-find-that-track)
7. [Customization](#step-7-customization)
8. [Quit](#step-8-quit)
9. [Further Reading](#step-9-further-reading)

##Step 1: Starting Cmus

When you first launch cmus (just type `cmus` in a terminal and press <kbd>Enter</kbd>) it will open to the album/artist view, which looks something like this:

```
+---------------------------------------------------------------------+
| Artist / Album             Track                            Library |
|                          |                                          |
|                          |                                          |
|                          |                                          |
|                          |                                          |
|                          |                                          |
|                          |                                          |
|                          |                                          |
|                                                                     |
| . 00:00 - 00:00 vol: 100                     all from library | C   |
|                                                                     |
+---------------------------------------------------------------------+
```

This is the view where your artists and albums will be displayed.

##Step 2: Adding Music

Press <kbd>5</kbd> to switch to the file-browser view so we can add some music. You should see something like this:

```
+---------------------------------------------------------------------+
| Browser - /home/jasonwoof                                           |
| ../                                                                 |
| Desktop/                                                            |
| MySqueak/                                                           |
| audio-projects/                                                     |
| audio/                                                              |
| bin/                                                                |
| config/                                                             |
|                                                                     |
| . 00:00 - 00:00 vol: 100                     all from library | C   |
|                                                                     |
+---------------------------------------------------------------------+
```

Now, use the :arrow_up: and :arrow_down: keys, <kbd>Enter</kbd> and <kbd>Backspace</kbd> to navigate to where you have audio files stored. To add music to your cmus library, use the arrow keys to highlight a file or folder, and press <kbd>a</kbd>. When you press </kbd>a</kbd> cmus will move you to the next line down (so that it is easy to add a bunch of files/folders in a
row) and start adding the file/folder you pressed <kbd>a</kbd> on to your library. This can take a while if you added a folder with a lot in it. As files are added, you will see the second time in the bottom right go up. This is the total
duration of all the music in the cmus library.

>**Note**: cmus does not move, duplicate or change your files. It just remembers where they are and caches the metadata (duration, artist, etc.)

Just to be on the safe side, lets save. Type `:save` and press <kbd>Enter</kbd>.

>**Note**: Cmus automatically saves your settings and library and everything when you quit, so you probably won't use the save command much.

##Step 3: Playing Tracks From The Library

Press <kbd>2</kbd> to go to the simple library view. You should see something like
this:

```
+---------------------------------------------------------------------+
| Library ~/.config/cmus/lib.pl - 31 tracks sorted by artist album di |
| Flying Lizards         . Money (That's What I Want)           02:31 |
| Jason Woofenden        . VoR Theme                       2009 01:20 |
| Keali'i Reichel      06. Wanting Memories                1994 04:28 |
| Molly Lewis            . Tom Cruise Crazy                     03:13 |
| NonMemory              . pista1                          2009 03:18 |
| NonMemory            01. pista1                    2009-04-21 04:13 |
| Ray Charles          06. Halleluja I Love Her So              02:33 |
|                                                                     |
| . 00:00 - 2:16:25 vol: 100                   all from library | C   |
|                                                                     |
+---------------------------------------------------------------------+
```

Use the :arrow_up: and :arrow_down: keys to select a track you'd like to hear, and press
<kbd>Enter</kbd> to play it. Here's some keys to control play:

- Press <kbd>c</kbd> to pause/unpause
- Press :arrow_right:/:arrow_left: to seek by 10 seconds
- Press <kbd>Ctrl+<</kbd>/<kbd>Ctrl+></kbd> seek by one minute

cmus has some great options to control what plays next (if anything) when the track ends. The state of these settings are shown in the bottom right corner. The first of these shows what collection of tracks (currently "all from library") we are playing. Press <kbd>m</kbd> to cycle through the different options for this setting. To the right of that (past the "|") cmus shows the state of three toggles. Only toggles which are "on" are shown, so now we only see the <kbd>C</kbd>. Here are the toggles:

>**<kbd>C</kbd> - _Continue_**: 
>When this is off, cmus will always stop at the end of the track. You can toggle this setting by pressing <kb>Shift-C</kbd>.

>**<kbd>R</kbd> - _Repeat_**: 
>If this is on (and continue is on), when cmus reaches the end of the group of tracks you're playing (selected with the <kbd>m</kbd> key) it will start again from the beginning. Press <kbd>r</kbd> to toggle this setting.

>**<kbd>S</kbd> - _Shuffle_**:
>When this is on, cmus will choose a random order to play all the tracks once. Press <kbd>s</kbd> to toggle this option.

##Step 4: Managing The Queue

Lets say you're listening to a song, and you want to select which song will play next, without interrupting the currently playing song. No problem! Just go to the song you want to hear next (in any of the views) and press <kbd>e</kbd>. The
queue is FIFO, meaning if you queue up another track, it will play after the one you already had queued up.

>**Note**: The queue is not affected by the **shuffle** option described above.

Press <kbd>4</kbd> to view/edit the queue. This view works and looks a lot like the simple library view. The main difference is that you can change the order of the tracks with the <kbd>p</kbd> and <kbd>P</kbd> keys. You can press <kbd>Shift-D</kbd> to remove a track from the queue.

When cmus is ready to play another track (it's reached the end of a track and the "continue" setting is on) it will remove the top entry from the queue and start playing it.

##Step 5: The Playlist

The playlist works like another library (like view <kbd>2</kbd>) except that (like the queue) you manually set the order of the tracks. This can be quite useful if you want to create a mix of specific tracks or if you want to listen to an audio book without having the chapters play when you're playing "all from library".

The playlist is on view <kbd>3</kbd>. But before we go there, lets add some tracks. Press <kbd>2</kbd> to go to the simple library view, go to a track you want and press <kbd>y</kbd> to add it to the playlist. The only visual feedback you'll get that anything happened is that the highlight will move down one row. Add a few more so you have something to work with.

Now press <kbd>3</kbd> to go to the playlist.

Just like the queue, you can use the <kbd>p</kbd>, <kbd>P</kbd> and <kbd>D</kbd> keys to move and delete tracks from the playlist.

>**Note**: Changing the view (e.g. by pressing <kbd>3</kbd>) does not affect what cmus will play next. To put cmus into "play from the playlist" mode, press <kbd>Enter</kbd> on one of the tracks in the playlist. To switch modes without interrupting the currently-playing song, you can press <kbd>Shift-M</kbd>.

##Step 6: Find that track

This step shows various ways you can find track(s) you're looking for.

**Search**: Press <kbd>2</kbd> to be sure you're on the simple library view, then press <kbd>/</kbd> to start a search. Type a word or two from the track you're looking for. cmus will search for tracks that have all those words in them. Press <kbd>Enter</kbd> to get the keyboard out of the search command, and <kbd>n</kbd> to find the next match.

**Tree View**: Press <kbd>1</kbd> to select the tree view. Scroll to the artist, press <kbd>Space</kbd> to show their albums, scroll to the album you want, then press <kbd>TAB</kbd> so the keyboard controls the right column. Press <kbd>TAB</kbd> again to get back to the left column.

**Filters**: See the reference manual (see Further Reading below) for a detailed description on how to quickly (and temporarily) hide most of your music.

##Step 7: Customization

Cmus has some very cool settings you can tweak, like changing the way tracks
are displayed (e.g. to display disk numbers), enabling replaygain support or
changing the keybindings.

Press *7* for a quick overview of the current keybindings and settings.

To change a setting or keybind, just select it (up/down keys) and press enter.
This will put the command for the current setting in the command now (bottom
left of your screen), which you can edit to put in a new value/key.

Please see the reference manual (see Further Reading below) for a detailed
description of all the commands and settings available.


##Step 8: Quit

When you're done, type *:q* and press Enter to quit. This will save your
settings, library, playlist and queue.


##Step 9: Further Reading

Cmus comes with a great reference manual. Now that you've got the basics down
it should be intelligible. Try *man cmus* in a terminal. If that's not
installed, try opening up `cmus.txt` from the `Doc` directory, or read the latest
version online:

`https://github.com/cmus/cmus/blob/master/Doc/cmus.txt`

There are more commands and features not covered here like loading and saving
playlists, controlling cmus remotely with `cmus-remote`, etc.

