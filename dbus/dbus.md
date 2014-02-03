# D-Bus interface documentation

This file describes the D-Bus interface of cmus.  cmus is reachable on the
session bus under the name `net.sourceforge.cmus` and exposes the object
`/net/sourceforge/cmus` with the interface `net.sourceforge.cmus`.

## Functions

* **`hello()`** To check if cmus is available.

### Info functions

The following functions take no arguments and return one value.  If the function
is not applicable (e.g. `artist()` if no track is selected or if the `artist`
tag isn't set in the playing file,) a default value of `""` for strings, `0`
for integers, or `false` for booleans is returned.

* **`bool   has_track()`**    Whether a track is selected.
* **`bool   repeat()`**       Whether repeat is on.
* **`bool   shuffle()`**      Whether shuffle is on.
* **`int32  pos()`**          The seek position of the current track.
* **`int32  volume()`**       The volume.
* **`string list_artists()`** A list of all artists separated by newlines.
* **`string query_old()`**    The output of `cmus-remote -Q`.
* **`string status()`**       `playing`, `stopped`, or `paused`.
* **`string version()`**      The cmus version.

The following functions return the corresponding tags of the current file.

* **`bool   compilation()`**
* **`int32  date()`**
* **`int32  discnumber()`**
* **`int32  duration()`**
* **`int32  original_date()`**
* **`int32  tracknumber()`**
* **`string album()`**
* **`string albumartist()`**
* **`string artist()`**
* **`string filename()`**
* **`string genre()`**
* **`string title()`**

### Action functions.

The following functions take zero or one argument and perform an action inside
cmus.  If the request is malformatted an error might be returned.  Otherwise the
functions respond with an empty reply.  See `man cmus` for the documentation of
the cmus commands.

* **`add_to_library(string file)`**  `:add -l $file`
* **`add_to_playlist(string file)`** `:add -p $file`
* **`add_to_queue(string file)`**    `:add -q $file`
* **`clear_library()`**              `:clear -l`
* **`clear_playlist()`**             `:clear -p`
* **`clear_queue()`**                `:clear -q`
* **`cmd(string cmd)`**              `:$cmd`
* **`load(string file)`**            `:load $file`
* **`next()`**                       `:player-next`
* **`pause()`**                      `:player-pause`
* **`play()`**                       `:player-play`
* **`prev()`**                       `:player-prev`
* **`seek(string pos)`**             `:seek $pos`
* **`set_volume(string vol)`**       `:vol $vol`
* **`stop()`**                       `:player-stop`
* **`toggle_repeat()`**              `:toggle repeat`
* **`toggle_shuffle()`**             `:toggle shuffle`

## Signals

cmus emits the following signals.

* **`exit()`**          When cmus exits.
* **`status_change()`** When the player starts playing, stops playing, or pauses.
* **`track_change()`**  When the track changes.
* **`vol_change()`**    When the volume changes.
