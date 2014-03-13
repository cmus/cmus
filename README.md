cmus — C\* Music Player
=======================

https://cmus.github.io/

[![Build Status](https://travis-ci.org/cmus/cmus.png?branch=master)](https://travis-ci.org/cmus/cmus)

Copyright © 2004-2008 Timo Hirvonen <tihirvon@gmail.com>

Copyright © 2008-2014 Various Authors


Configuration
-------------

List available optional features

    $ ./configure --help

Auto-detect everything

    $ ./configure

To disable some feature, arts for example, and install to `$HOME` run

    $ ./configure prefix=$HOME CONFIG_ARTS=n

After running configure you can see from the generated `config.mk` file
what features have been configured in (see the `CONFIG_*` options).

*Note*: For some distributions you need to install development versions
of the dependencies.  For example if you want to use 'mad' input plugin
(mp3) you need to install `libmad0-dev` (Debian) or `libmad-devel` (RPM)
package. After installing dependencies you need to run `./configure`
again, of course.

If you want to use the Tremor library as alternative for decoding
Ogg/Vorbis files you have to pass `CONFIG_TREMOR=y` to the configure
script:

    $ ./configure CONFIG_VORBIS=y CONFIG_TREMOR=y

The Tremor library is supposed to be used on hardware that has no FPU.


Building
--------

    $ make

Or on some BSD systems you need to explicitly use GNU make:

    $ gmake


Installation
------------

    $ make install

Or to install to a temporary directory:

    $ make install DESTDIR=~/tmp/cmus

This is useful when creating binary packages.

Remember to replace `make` with `gmake` if needed.


Manuals
-------

    $ man cmus-tutorial

And

    $ man cmus


Mailing List
------------

To subscribe to cmus-devel@lists.sourceforge.net visit
http://lists.sourceforge.net/lists/listinfo/cmus-devel

The list is open but moderated (you can post to the list without
subscribing but it's not recommended because I have to accept each email
from non-subscribed users).  Traffic of the list is extremely low.
Please use the [issues](https://github.com/cmus/cmus/issues) page for any problems, suggestions, or bug reports.

Reporting Bugs
--------------

After a crash send bug report with last lines of `~/cmus-debug.txt` to
cmus-devel@lists.sourceforge.net.  The file exists only if you
configured cmus with maximum debug level (`./configure DEBUG=2`).


Git Repository
--------------

https://github.com/cmus/cmus

    $ git clone https://github.com/cmus/cmus.git


Hacking
-------

cmus uses the [Linux kernel coding style](http://www.kernel.org/doc/Documentation/CodingStyle).
Use hard tabs.  Tabs are _always_ 8 characters wide.  Keep the style consistent with rest of the
code.

Use `git format-patch` to generate patches from your commits.
Alternatively you can use `diff -up` if you don't want to use git.
