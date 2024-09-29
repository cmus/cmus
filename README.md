# cmus — C\* Music Player

https://cmus.github.io/

[![Build Status](https://github.com/cmus/cmus/actions/workflows/build.yml/badge.svg)](https://github.com/cmus/cmus/actions/workflows/build.yml)

Copyright © 2004-2008 Timo Hirvonen <tihirvon@gmail.com>

Copyright © 2008-2017 Various Authors


## Configuration

    $ ./configure

By default, features are auto-detected. To list all configuration options, run
`./configure --help`. Some common autoconf-style options like `--prefix` are
also available.

After running configure you can see from the generated `config.mk` file
what features have been configured in (see the `CONFIG_*` options).

The packages containing dependencies on common distributions are listed below. All dependencies other than pkg-config and ncurses, iconv, and elogind/systemd are for optional input/output plugins. It is assumed that libc headers, a C compiler, git, and GNU Make are available.

| Distro            | Dependencies |
| :--               | :--          |
| **Debian/Ubuntu** | apt install pkg-config libncursesw5-dev libfaad-dev libao-dev libasound2-dev libcddb2-dev libcdio-cdda-dev libdiscid-dev libavformat-dev libavcodec-dev libswresample-dev libflac-dev libjack-dev libmad0-dev libmodplug-dev libmpcdec-dev libsystemd-dev libopusfile-dev libpulse-dev libsamplerate0-dev libsndio-dev libvorbis-dev libwavpack-dev |
| **Fedora/RHEL**   | dnf install 'pkgconfig(ncursesw)' 'pkgconfig(alsa)' 'pkgconfig(ao)' 'pkgconfig(libcddb)' 'pkgconfig(libcdio_cdda)' 'pkgconfig(libdiscid)' 'pkgconfig(libavformat)' 'pkgconfig(libavcodec)' 'pkgconfig(libswresample)' 'pkgconfig(flac)' 'pkgconfig(jack)' 'pkgconfig(mad)' 'pkgconfig(libmodplug)' libmpcdec-devel 'pkgconfig(libsystemd)' 'pkgconfig(opusfile)' 'pkgconfig(libpulse)' 'pkgconfig(samplerate)' 'pkgconfig(vorbisfile)' 'pkgconfig(wavpack)' |
| **+ RPMFusion**   | dnf install faad2-devel libmp4v2-devel |
| **Arch Linux**    | pacman -S pkg-config ncurses libiconv faad2 alsa-lib libao libcddb libcdio-paranoia libdiscid ffmpeg flac jack libmad libmodplug libmp4v2 libmpcdec systemd opusfile libpulse libsamplerate libvorbis wavpack |
| **Alpine**        | apk add pkgconf ncurses-dev gnu-libiconv-dev alsa-lib-dev libao-dev libcddb-dev ffmpeg-dev flac-dev jack-dev libmad-dev libmodplug-dev elogind-dev opus-dev opusfile-dev pulseaudio-dev libsamplerate-dev libvorbis-dev wavpack-dev |
| **Termux**        | apt install libandroid-support ncurses libiconv ffmpeg libmad libmodplug opusfile pulseaudio libflac libvorbis libwavpack |
| **Homebrew**      | brew install pkg-config ncurses faad2 libao libcddb libcdio libdiscid ffmpeg flac jack mad libmodplug mp4v2 musepack opusfile libsamplerate libvorbis wavpack |


## Building

    $ make

Or on some BSD systems you need to explicitly use GNU make:

    $ gmake


## Installation

    $ make install

Or to install to a temporary directory:

    $ make install DESTDIR=~/tmp/cmus

This is useful when creating binary packages.

Remember to replace `make` with `gmake` if needed.


## Manuals

    $ man cmus-tutorial

And

    $ man cmus


## IRC Channel

Feel free to join IRC channel #cmus on Libera.chat and share you experience,
problems and issues. Note: This is an unofficial channel and all people hanging
around there are for the love of cmus.


## Reporting Bugs

Bugs should be reported using the GitHub [issue
tracker](https://github.com/cmus/cmus/issues). When creating a new issue, a
template will be shown containing instructions on how to collect the necessary
information.

Additional debug information can be found in `~/cmus-debug.txt` if you
configured cmus with maximum debug level (`./configure DEBUG=2`). In case of a
crash the last lines may be helpful.


## Git Repository

https://github.com/cmus/cmus

    $ git clone https://github.com/cmus/cmus.git


## Hacking

cmus uses the [Linux kernel coding
style](https://www.kernel.org/doc/html/latest/process/coding-style.html). Use
hard tabs. Tabs are _always_ 8 characters wide. Keep the style consistent with
rest of the code.

Bug fixes and implementations of new features should be suggested as a
[pull request](https://github.com/cmus/cmus/pulls) directly on GitHub.

