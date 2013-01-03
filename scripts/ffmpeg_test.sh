#!/bin/bash
# vim: set expandtab shiftwidth=4:
#
# Copyright 2010-2013 Various Authors
# Copyright 2012 Johannes Weißl
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.

# many (!) FFmpeg versions will be installed here, at least 25GB
FFMPEG_BUILD_DIR=$HOME/cmus_ffmpeg_test/ffmpeg_builds

# ffmpeg/libav source will be cloned into this directory
FFMPEG_SRC_DIR=$HOME/cmus_ffmpeg_test/ffmpeg_src

# source code of cmus is expected here
CMUS_SRC_DIR=$HOME/cmus_ffmpeg_test/cmus_src

# cmus versions will be installed here
CMUS_BUILD_DIR=$HOME/cmus_ffmpeg_test/cmus_builds

FFMPEG_CLONE_URL=git://source.ffmpeg.org/ffmpeg.git
LIBAV_CLONE_URL=git://git.libav.org/libav.git

# headers of ffmpeg that are relevant to cmus compilation
HEADERS="avcodec.h avformat.h avio.h mathematics.h version.h"

# argument to make -j
MAKE_J=$(grep -c "^processor" /proc/cpuinfo 2>/dev/null || echo 1)

print_usage () {
    echo "Usage: $progname build_ffmpeg | build_libav | build_cmus | test_cmus"
    echo
    echo "build_{ffmpeg,libav}:"
    echo " 1. clone/pull source into $FFMPEG_SRC_DIR/{ffmpeg,libav}"
    echo " 2. build and install (necessary) revisions into $FFMPEG_BUILD_DIR"
    echo "    can take days and needs up to 25 GB hard disk (!)"
    echo "    you can use ctrl-c to stop the script and run it later to continue"
    echo
    echo "build_cmus:"
    echo " 1. expects cmus source in $CMUS_SRC_DIR"
    echo " 2. build cmus for every revision in $FFMPEG_BUILD_DIR and install"
    echo "    to $CMUS_BUILD_DIR"
    echo
    echo "test_cmus:"
    echo " test ffmpeg plugin of every cmus build in $CMUS_BUILD_DIR"
}

function get_commits () {
    for name in "$@" ; do
        find -type f -name "$name" -exec git log --follow --pretty=format:"%H%n" {} \;
    done
    for tag in $(git tag) ; do
        git show "$tag" | sed -n "s/^commit //p"
    done
}

function uniq_stable () {
    nl -ba | sort -suk2 | sort -n | cut -f2-
}

DONE=
trap 'DONE=1' SIGINT

function build_to_prefix () {
    prefix=$1
    cur=$2
    all=$3
    cur_name=$4
    build_cmd=$5
    echo -n "[$((cur*100/all))%] "
    if [ -e "$prefix.broken" ] ; then
        echo "skip $cur_name, broken"
    elif [ -e "$prefix.part" ] ; then
        echo "skip $cur_name, is being build"
    else
        if [ -e "$prefix" ] ; then
            echo "skip $cur_name, already built"
        else
            echo -n "build and install to $prefix: "
            echo $build_cmd >"$prefix.log"
            (mkdir -p "$prefix.part" && eval $build_cmd && mv "$prefix.part/$prefix" "$prefix" && rm -rf "$prefix.part") >>"$prefix.log" 2>&1 && echo "ok" ||
                    (touch "$prefix.broken" ; echo "FAILED:" ; echo $build_cmd)
        fi
    fi
    [ -n "$DONE" ] && rm -rvf "$prefix" "$prefix".part "$prefix".broken
}

function build_revisions () {
    name=$1
    url=$2
    mkdir -p "$FFMPEG_SRC_DIR" "$FFMPEG_BUILD_DIR"
    FFMPEG_SRC_DIR=$FFMPEG_SRC_DIR/$name
    if [ -e "$FFMPEG_SRC_DIR" ] ; then
        echo "pull $url in $FFMPEG_SRC_DIR"
        pushd "$FFMPEG_SRC_DIR" >/dev/null
        git reset --hard origin/master >/dev/null
        git clean -fxd >/dev/null
        git pull >/dev/null
    else
        echo "clone $url in $FFMPEG_SRC_DIR"
        git clone "$url" "$FFMPEG_SRC_DIR" >/dev/null
        pushd "$FFMPEG_SRC_DIR" >/dev/null
    fi
    commits=$(get_commits $HEADERS | uniq_stable)
    commits_count=$(echo $commits | wc -w)
    i=0
    for c in $commits ; do
        i=$((i+1))
        git reset --hard "$c" >/dev/null
        git clean -fxd >/dev/null
        prefix="$FFMPEG_BUILD_DIR/$c"
        build_to_prefix "$prefix" "$i" "$commits_count" "$c" \
            "./configure --prefix=\"$prefix.part\" --enable-shared --disable-static && make -j$MAKE_J && make install"
        [ -n "$DONE" ] && break
    done
    popd >/dev/null
}

build_cmus () {
    pushd "$CMUS_SRC_DIR" >/dev/null
    mkdir -p "$CMUS_BUILD_DIR"
    revdirs=$(find "$FFMPEG_BUILD_DIR" -mindepth 1 -maxdepth 1 -type d ! -name "*.part")
    revdirs_count=$(echo $revdirs | wc -w)
    i=0
    for revdir in $revdirs ; do
        i=$((i+1))
        rev=$(basename "$revdir")
        prefix="$CMUS_BUILD_DIR/$rev"
        make distclean >/dev/null 2>&1
        build_to_prefix "$prefix" "$i" "$revdirs_count" "$rev" \
            "CFLAGS=\"-I$revdir/include\" LDFLAGS=\"-L$revdir/lib\" ./configure prefix=\"$prefix\" CONFIG_FFMPEG=y DEBUG=2 && make -j$MAKE_J && make install DESTDIR=\"$prefix.part\""
        [ -n "$DONE" ] && break
    done
    popd >/dev/null
}

test_cmus () {
    mkdir -p "$CMUS_BUILD_DIR"
    revdirs=$(find "$CMUS_BUILD_DIR" -mindepth 1 -maxdepth 1 -type d ! -name "*.part")
    revdirs_count=$(echo $revdirs | wc -w)
    i=0
    for revdir in $revdirs ; do
        i=$((i+1))
        rev=$(basename "$revdir")
        tmpdir=$(mktemp -d)
        lib_prefix=$FFMPEG_BUILD_DIR/$rev
        echo -n "[$((i*100/revdirs_count))%] test $revdir: "
        if CMUS_HOME=$tmpdir LD_LIBRARY_PATH=$lib_prefix/lib:$LD_LIBRARY_PATH "$revdir"/bin/cmus --plugins | grep -q "^ *ffmpeg" ; then
            echo "working"
        else
            echo "not working: "
            echo "CMUS_HOME=$tmpdir LD_LIBRARY_PATH=$lib_prefix/lib:$LD_LIBRARY_PATH \"$revdir\"/bin/cmus --plugins"
            cat $tmpdir/cmus-debug.txt
        fi
        rm "$tmpdir"/cmus-debug.txt
        rmdir "$tmpdir"
        [ -n "$DONE" ] && break
    done
}

progname=$(basename "$0")

while [ $# -gt 0 ] ; do
    case "$1" in
        -h | --help)
            print_usage
            exit 0
            ;;
         --)
            shift ; break
            ;;
        -*)
            echo >&2 "$progname: unrecognized option \`$1'"
            echo >&2 "Try \`$0 --help' for more information."
            exit 1
            ;;
        *)
            break
            ;;
    esac
done

if [ $# -eq 0 ] ; then
    print_usage
    exit 0
elif [ $# -gt 1 ] ; then
    echo >&2 "$progname: too many arguments"
    echo >&2 "Try \`$0 --help' for more information."
    exit 1
fi

case "$1" in
    build_ffmpeg)
        build_revisions ffmpeg "$FFMPEG_CLONE_URL"
        ;;
    build_libav)
        build_revisions libav "$LIBAV_CLONE_URL"
        ;;
    build_cmus)
        build_cmus
        ;;
    test_cmus)
        test_cmus
        ;;
    *)
        echo >&2 "$progname: unrecognized command \`$1'"
        echo >&2 "Try \`$0 --help' for more information."
        exit 1
esac
