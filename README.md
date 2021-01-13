*Warning: cmus is not actively maintained. For details, please see [#856](https://github.com/cmus/cmus/issues/856)*

cmus — C\* Music Player
=======================

https://cmus.github.io/

[![Build Status](https://travis-ci.org/cmus/cmus.svg?branch=master)](https://travis-ci.org/cmus/cmus)

Copyright © 2004-2008 Timo Hirvonen <tihirvon@gmail.com>

Copyright © 2008-2017 Various Authors


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

To subscribe to cmus-devel@lists.sourceforge.net or view the archive visit
http://lists.sourceforge.net/lists/listinfo/cmus-devel.

The mailing list now serves as an archive for old releases and issues.
Please use the GitHub [issues](https://github.com/cmus/cmus/issues)
page for any problems, suggestions, or bug reports.


Reporting Bugs
--------------

Bugs should be reported using the GitHub [issue tracker](https://github.com/cmus/cmus/issues).
When creating a new issue, a template will be shown containing instructions on how to collect
the necessary information.

Additional debug information can be found in `~/cmus-debug.txt` if you configured cmus with
maximum debug level (`./configure DEBUG=2`). In case of a crash the last lines may be helpful.


Git Repository
--------------

https://github.com/cmus/cmus

    $ git clone https://github.com/cmus/cmus.git


Hacking
-------

cmus uses the [Linux kernel coding style](https://www.kernel.org/doc/html/latest/process/coding-style.html).
Use hard tabs.  Tabs are _always_ 8 characters wide.  Keep the style consistent with rest of the
code.

Bug fixes and implementations of new features should be suggested as a
[pull request](https://github.com/cmus/cmus/pulls) directly on GitHub.

