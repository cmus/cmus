==============================
CMus - C\* Music Player Manual
==============================

.. contents::

Command Line Options
==========================

See ``cmus --help`` for more information.

Remote Control
==============

CMus can be controlled via UNIX-socket using ``cmus-remote`` command. This is
very useful feature because it allows you control CMus without having to
switch to the desktop where CMus window is.

See ``cmus-remote --help`` for more information.

Views
=====

There are 5 views in cmus:

* Artist/Album/Track Tree View (1)
* Shuffle List (2)
* Sorted List (3)
* Play Queue (4)
* Directory Browser (5)

To switch between the views use keys '1' - '5'. Views 1-3 display current
playlist.  View 3 can be sorted (see sort_ option).

View 4 displays play queue. Tracks in the play queue are played first and
removed from the queue immediately after playing starts. After last track from
play queue has been played CMus starts playing tracks in the playlist.

View 5 allows you to browse filesystem, add tracks to playlist, enqueue
tracks, delete files and even play tracks directly from the browser.  You can
also 'cd' into a playlist.

Normal Mode
==========================

Global Keys (Views 1-5)
--------------------------

=================  ===========
Key                Description
=================  ===========
F1                 show help window
z                  skip back in playlist
x                  play
c                  pause
v                  stop
b                  skip forward in playlist
C                  toggle continue
r                  toggle repeat
m                  toggle playlist mode (all, artist, album)
p                  toggle play mode (tree (view 1), shuffle (view 2), sorted (view 3))
t                  toggle time elapsed/remaining
q                  quit
:                  enter command mode
left, h            skip 5 seconds back in song
right, l           skip 5 seconds forward in song
1                  switch to artist/album/track tree view
2                  switch to shuffle view
3                  switch to sorted view
4                  switch to play queue view
5                  switch to directory browser view
up, k              move up
down, j            move down
page up, ctrl-b    move page up
page down, ctrl-f  move page down
home, g            go to top of the window
end, G             go to bottom of the window
\-                 volume down
+, =               volume up
{                  left channel down
}                  right channel down
[                  left channel up
]                  right channel up
=================  ===========

Playlist Keys (Views 1-3)
--------------------------

=======  ===========
Key      Description
=======  ===========
del, D   remove selected artist, album or track from playlist
e        append selected artist, album or track to the play queue
E        prepend selected artist, album or track to the play queue
i        jump to current track
u        remove non-existent files from playlist and update tags for changed files
enter    play selected track
space    show/hide albums for the selected artist
tab      switch window in the artist+album/track view
=======  ===========

Play Queue Keys (View 4)
--------------------------

=======  ===========
Key      Description
=======  ===========
del, D   remove selected track from the queue
=======  ===========

Directory Browser Keys (View 5)
-------------------------------

=========  ===========
Key        Description
=========  ===========
del, D     remove selected file
a          add file/directory to playlist
e          append selected file/directory to the play queue without adding to playlist
E          prepend selected file/directory to the play queue without adding to playlist
i          toggle showing of hidden files
enter      cd to selected directory/playlist or play selected file
backspace  cd to parent directory
=========  ===========

Command Mode
==========================

Press ':' any time to enter command mode. The command mode works much like
VIM_'s command mode.  Tabulator expansion works for files/dirs, commands and
options. There's command history too (up/down arrow keys). Press 'ESC' to
leave command mode and return to `Normal Mode`_.

You don't have to type whole command name if it is unambiguous.  For example
``:a somefile.mp3``.

Commands
--------------------------

Use the ``:set`` command to set options.

===============================  ===========
Command                          Description
===============================  ===========
:load filename                   Clear playlist and then load a new one. Simple one track/line lists and .pls playlists are supported.
:save [filename]                 Save playlist.  Default filename is the last used one.
:add dir/file/playlist/url       Add dir/file/playlist/url to playlist. This command can be used to join playlists.
:cd [directory]                  Change directory.  Default directory is ``$HOME``.
:clear                           Clear playlist.
:enqueue\ dir/file/playlist/url  Add dir/file/playlist/url to the play queue.
:shuffle                         Reshuffle playlist.
:seek [+-]POS                    Seek top POS (seconds). POS can be suffixed with 'm' (minutes) or 'h' (hours).
:set OPTION=VALUE                Set option (See Options_).
===============================  ===========

Options
--------------------------

======================  ===========
Option                  Description
======================  ===========
output_plugin           output plugin (alsa, arts, oss)
buffer_seconds          size of player buffer in seconds (1-10)
dsp.\*, mixer.\*        output plugin options (use tab to cycle through all possible options)
color\_\*               user interface colors (See `User Interface Colors`_)
format_current          format of the line showing currently played track
format_playlist         format of text in views 2-4
format_title            format of terminal window title
format_track_win        format of text in track window (view 1)
altformat\_\*           format strings used when file has no tags
_`sort`                 comma separated list of sort keys for the sorted view (3). Valid keys: artist, album, title, tracknumber, discnumber, date, genre, filename)
status_display_program  script to run when player status changes (See `Status Display`_)
======================  ===========

Format Characters
~~~~~~~~~~~~~~~~~~~~~~~~~~

=========  ===========
Character  Description
=========  ===========
%a         artist
%l         album
%D         disc number
%n         track number
%t         title
%g         genre
%y         year
%d         duration
%f         path and filename
%F         filename
%=         start align right (use at most once)
%%         literal '%'
=========  ===========

You can use printf style formatting (width, alignment, padding).

Examples
~~~~~~~~~~~~~~~~~~~~~~~~~~

::

	:set format_trackwin= %02n. %t (%y)%= %d
	:set format_current= %n. %-30t %40F (%y)%= %d

To see current value of an option type ``:set option=<TAB>``.

ID3 Tags
========

Some MP3s encode tags using different character set than specified in the
frame. In other words those MP3s are broken but because this is so common
problem cmus has an option (mad.charset) to change character set used for those broken MP3s.

You need to edit ``~/.config/cmus/config`` manually, this can't be set using
``:set`` command. Default value is ISO-8859-1.

::

	mad.charset = "cp1251"

**Note:** If you change this option you need to remove
``~/.cache/cmus/trackdb.*`` files because they contain tags encoded in the old
character set.

Searching
=========

=======  ===========
Key      Description
=======  ===========
/WORDS   search forward
?WORDS   search backward
//WORDS  search forward (see below)
??WORDS  search backward (see below)
/        search forward for the latest used pattern
?        search backward for the latest used pattern
n        search next
N        search previous
=======  ===========

WORDS is list of words separated by spaces.  Search is case insensitive and
works in every view.                                                    

In views 1-4 words are compared to artist, album and title tags.  Use
//WORDS and ??WORDS to search only artists/albums in view 1 or titles in
views 2-4.  If the file doesn't have tags words are compared to filename
without path.

In view 5 words are compared to filename without path.

Streaming
=========

CMus supports Shoutcast/Icecast streams (Ogg and MP3).  To add stream
to playlist use ``:add`` command or ``cmus-remote``.

::

	:add http://example.com/path/to/stream

Status Display
==========================

CMus can run external program which can be used to display player status on
desktop background (using root-tail for example), panel etc.

For example if you use WMI_ you can write a script that displays currently
playing file on the wmi statusbar using wmiremote command::

	:set status_display_program=cmus-status-display

To disable status display set ``status_display_program`` to empty string.

Example Script (cmus-status-display)
------------------------------------

::

	#!/bin/bash
	#
	# cmus-status-display
	#
	# Usage:
	#   in cmus command ":set status_display_program=cmus-status-display"
	#
	# This scripts is executed by cmus when status changes:
	#   cmus-status-display key1 val1 key2 val2 ...
	#
	# All keys contain only chars a-z. Values are UTF-8 strings.
	#
	# Keys: status file url artist album discnumber tracknumber title date
	#   - status (stopped, playing, paused) is always given
	#   - file or url is given only if track is 'loaded' in cmus
	#   - other keys/values are given only if they are available
	#  

	output()
	{
		# write status to /tmp/cmus-status (not very useful though)
		echo "$*" >> /tmp/cmus-status 2>&1

		# WMI (http://wmi.modprobe.de/)
		#wmiremote -t "$*" &> /dev/null
	}

	while [[ $# -ge 2 ]]
	do
	  eval _$1=\"$2\"
	  shift
	  shift
	done

	if [[ -n $_file ]]
	then
		output "[$_status] $_artist - $_album - $_title ($_date)"
	elif [[ -n $_url ]]
	then
		output "[$_status] $_title"
	else
		output "[$_status]"
	fi


User Interface Colors
==========================

Change ``color_*`` options to customize colors. 

Example::

	:set color_statusline_bg=4

**Tip:** type ``:set color_<tab>`` to cycle through all color option
variables.

Colors
--------------------------

======  =====
Value   Color
======  =====
-1      default color. use this if you want transparency
0       black
1       red
2       green
3       brown (or yellow)
4       blue
5       magenta
6       cyan
7       gray
8       dark gray
9       bright red
10      bright green
11      bright yellow
12      bright blue
13      bright magenta
14      bright cyan
15      white
16-255  more colors, not supported by every terminal
======  =====

**Note:** On terminals supporting only 16 colors you can use colors 8-15 for
foreground only.

==============  ==============
Terminal Type   Number of Colors Supported
==============  ==============
gnome-terminal  16
rxvt-unicode    88
xterm           256
GNU Screen      as many as the terminal inside which screen is running
==============  ==============

Files
==========================

~/.config/cmus/config
  configuration options

~/.config/cmus/playlist.pl
  automatically saved playlist

~/.cache/cmus/trackdb.dat, ~/.cache/cmus/trackdb.idx
  cached tags

~/.cache/cmus/ui_curses_cmd_history
  command mode history

~/.cache/cmus/ui_curses_search_history
  search mode history

You can override location of these files by setting ``XDG_CONFIG_HOME`` and/or
``XDG_CACHE_HOME`` environment variables.

Bugs
==========================

If you configured cmus with ``DEBUG=2`` then debugging information will be
written to ``/tmp/cmus-debug`` file. After a crash last lines of these files
should contain useful information.

Using GDB
--------------------------

Run ``gdb cmus core`` and type ``backtrace`` to see at which line cmus
crashed.

Author
==========================

Timo Hirvonen <tihirvon AT gmail.com>

.. _VIM: http://www.vim.org
.. _WMI: http://wmi.modprobe.de
